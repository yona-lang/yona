//
// LLVM Code Generation for Yona â€” Type-directed codegen (core)
//
// Every expression produces a TypedValue (LLVM Value + CType tag).
// Types propagate structurally: codegen_integer returns {i64, INT},
// codegen_add checks operand CTypes to choose iadd vs fadd, etc.
// Functions are deferred until call sites where arg types are known.
//

#include "yona/Codegen/Codegen.h"

#include "yona/Codegen/DeriveEngine.h"
#include "yona/Model/ModuleIdentity.h"
#include "yona/Semantics/TypeChecker.h"

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Utils.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace yona::compiler::codegen {

using llvm::BasicBlock;
using llvm::CallInst;
using llvm::CodeGenFileType;
using llvm::ConstantFP;
using llvm::ConstantInt;
using llvm::ConstantPointerNull;
using llvm::DEBUG_METADATA_VERSION;
using llvm::DIBuilder;
using llvm::DILocation;
using llvm::DINode;
using llvm::DISubprogram;
using llvm::DISubroutineType;
using llvm::DIType;
using llvm::Function;
using llvm::FunctionPassManager;
using llvm::GlobalValue;
using llvm::IRBuilder;
using llvm::LoopAnalysisManager;
using llvm::Metadata;
using llvm::Module;
using llvm::OptimizationLevel;
using llvm::PassBuilder;
using llvm::PipelineTuningOptions;
using llvm::PointerType;
using llvm::raw_fd_ostream;
using llvm::raw_string_ostream;
using llvm::SmallVector;
using llvm::SMDiagnostic;
using llvm::StringRef;
using llvm::TargetLibraryInfoImpl;
using llvm::TargetTransformInfo;
using llvm::Value;
using llvm::legacy::PassManager;
namespace dwarf = llvm::dwarf;
namespace legacy = llvm::legacy;
namespace sys = llvm::sys;
using LType = llvm::Type; // avoid collision with yona::compiler::types::Type

// ===== Constructor / Init =====

Codegen::Codegen(const std::string &module_name,
                 compiler::DiagnosticEngine *diag)
    : Codegen(std::make_unique<CodegenSession>(module_name, diag)) {}

Codegen::Codegen(std::unique_ptr<CodegenSession> SessionValue)
    : Session(std::move(SessionValue)) {
  if (!Session)
    throw std::invalid_argument("Codegen requires a CodegenSession");
  context_ = &Session->context();
  module_ = &Session->module();
  builder_ = &Session->builder();
  target_machine_ = Session->targetMachine();
  declare_runtime();

  // Built-in Closeable trait (prelude) â€” enables `with` expression
  TraitInfo closeable;
  closeable.name = "Closeable";
  closeable.type_params = {"a"};
  closeable.method_names.push_back("close");
  types_.traits["Closeable"] = closeable;

  // Closeable Int â€” for file descriptors and socket handles
  TraitInstanceInfo closeable_int;
  closeable_int.trait_name = "Closeable";
  closeable_int.type_names = {"Int"};
  closeable_int.method_mangled_names["close"] = "YonaRuntimeClose";
  types_.trait_instances["Closeable:Int"] = closeable_int;
  // Register rt_close as the implementation
  compiled_functions_["YonaRuntimeClose"] = {
      rt_.close_, CType::UNIT, {CType::INT}};

  // Closeable FileHandle â€” for binary file handles
  TraitInstanceInfo closeable_fh;
  closeable_fh.trait_name = "Closeable";
  closeable_fh.type_names = {"FileHandle"};
  closeable_fh.method_mangled_names["close"] = "YonaStdFileCloseFileHandle";
  types_.trait_instances["Closeable:FileHandle"] = closeable_fh;
}
Codegen::~Codegen() = default;

static void append_yona_path_dirs(std::vector<std::string> &module_paths) {
  const char *env = std::getenv("YONA_PATH");
  if (!env || !*env)
    return;
#ifdef _WIN32
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::string cur;
  auto flush = [&]() {
    if (cur.empty())
      return;
    std::error_code ec;
    auto p = std::filesystem::path(cur);
    if (!std::filesystem::is_directory(p, ec)) {
      cur.clear();
      return;
    }
    auto canon = std::filesystem::canonical(p, ec);
    if (ec) {
      cur.clear();
      return;
    }
    std::string s = canon.string();
    if (std::find(module_paths.begin(), module_paths.end(), s) ==
        module_paths.end())
      module_paths.push_back(s);
    cur.clear();
  };
  for (const char *c = env; *c; ++c) {
    if (*c == sep)
      flush();
    else
      cur.push_back(*c);
  }
  flush();
}

void Codegen::loadPrelude() {
  append_yona_path_dirs(ModulePaths);
  load_module_interface(std::filesystem::path("Prelude"));
  register_all_imports("Prelude");
  register_trait_externs();
}
// ===== DWARF Debug Info =====

void Codegen::set_debug_info(bool enabled, const std::string &filename) {
  debug_.enabled = enabled;
  if (enabled)
    init_debug_info(filename);
}

void Codegen::init_debug_info(const std::string &filename) {
  debug_.builder = std::make_unique<DIBuilder>(*module_);
  // Use DW_LANG_C as closest match for Yona
  auto file_path = std::filesystem::path(filename);
  auto dir = file_path.parent_path().string();
  auto file = file_path.filename().string();
  if (dir.empty())
    dir = ".";
  debug_.file = debug_.builder->createFile(file, dir);
  debug_.cu = debug_.builder->createCompileUnit(dwarf::DW_LANG_C, debug_.file,
                                                "yonac", false, "", 0);
  debug_.scope = debug_.cu;
  // Add debug info flag to module
  module_->addModuleFlag(Module::Warning, "Debug Info Version",
                         DEBUG_METADATA_VERSION);
  module_->addModuleFlag(Module::Warning, "Dwarf Version", 4);
}

void Codegen::finalize_debug_info() {
  if (debug_.builder)
    debug_.builder->finalize();
}

void Codegen::set_debug_loc(const SourceRange &loc) {
  if (!debug_.enabled || !debug_.scope)
    return;
  if (loc.Line == 0)
    return; // skip unknown locations
  builder_->SetCurrentDebugLocation(
      DILocation::get(*context_, loc.Line, loc.Column, debug_.scope));
}

DIType *Codegen::di_type_for(CType ct) {
  if (!debug_.builder)
    return nullptr;
  switch (ct) {
  case CType::INT:
    return debug_.builder->createBasicType("Int", 64, dwarf::DW_ATE_signed);
  case CType::FLOAT:
    return debug_.builder->createBasicType("Float", 64, dwarf::DW_ATE_float);
  case CType::BOOL:
    return debug_.builder->createBasicType("Bool", 8, dwarf::DW_ATE_boolean);
  case CType::STRING:
    return debug_.builder->createPointerType(
        debug_.builder->createBasicType("Char", 8, dwarf::DW_ATE_signed_char),
        64);
  case CType::SYMBOL:
    return debug_.builder->createBasicType("Symbol", 64, dwarf::DW_ATE_signed);
  case CType::UNIT:
    return debug_.builder->createBasicType("Unit", 64, dwarf::DW_ATE_signed);
  case CType::FUNCTION:
    return debug_.builder->createPointerType(nullptr, 64);
  case CType::SEQ:
  case CType::SET:
  case CType::DICT:
  case CType::ADT:
  case CType::PROMISE:
  case CType::BYTE_ARRAY:
  case CType::INT_ARRAY:
  case CType::FLOAT_ARRAY:
  case CType::CHANNEL:
    return debug_.builder->createPointerType(
        debug_.builder->createBasicType("Opaque", 8, dwarf::DW_ATE_unsigned),
        64);
  case CType::TUPLE:
  case CType::SUM:
  case CType::RECORD:
    return debug_.builder->createBasicType("Tuple", 64, dwarf::DW_ATE_signed);
  }
  return debug_.builder->createBasicType("Unknown", 64, dwarf::DW_ATE_signed);
}

DISubroutineType *Codegen::di_func_type(const std::vector<CType> &param_types,
                                        CType ret_type) {
  SmallVector<Metadata *, 8> types;
  types.push_back(di_type_for(ret_type)); // return type first
  for (auto ct : param_types)
    types.push_back(di_type_for(ct));
  return debug_.builder->createSubroutineType(
      debug_.builder->getOrCreateTypeArray(types));
}

LType *Codegen::llvm_type(CType ct) {
  switch (ct) {
  case CType::INT:
    return LType::getInt64Ty(*context_);
  case CType::FLOAT:
    return LType::getDoubleTy(*context_);
  case CType::BOOL:
    return LType::getInt1Ty(*context_);
  case CType::STRING:
    return PointerType::get(*context_, 0);
  case CType::SEQ:
    return PointerType::get(*context_, 0);
  case CType::TUPLE:
    return LType::getInt64Ty(*context_); // ptrtoint'd boxed ptr
  case CType::UNIT:
    return LType::getInt64Ty(*context_);
  case CType::FUNCTION:
    return PointerType::get(*context_, 0);
  case CType::SYMBOL:
    return LType::getInt64Ty(*context_); // interned symbol ID
  case CType::SET:
    return PointerType::get(*context_, 0);
  case CType::DICT:
    return PointerType::get(*context_, 0);
  case CType::PROMISE:
    return PointerType::get(*context_, 0);
  case CType::ADT:
    return PointerType::get(*context_, 0);
  case CType::BYTE_ARRAY:
    return PointerType::get(*context_, 0);
  case CType::INT_ARRAY:
    return PointerType::get(*context_, 0);
  case CType::FLOAT_ARRAY:
    return PointerType::get(*context_, 0);
  case CType::CHANNEL:
    return PointerType::get(*context_, 0);
  case CType::SUM:
    return LType::getInt64Ty(*context_); // boxed tagged value (2-tuple)
  case CType::RECORD:
    return LType::getInt64Ty(*context_); // boxed tuple (ptrtoint'd)
  }
  return LType::getInt64Ty(*context_);
}

void Codegen::declare_runtime() {
  auto i64 = LType::getInt64Ty(*context_);
  auto f64 = LType::getDoubleTy(*context_);
  auto vd = LType::getVoidTy(*context_);
  auto i1 = LType::getInt1Ty(*context_);
  auto ptr = PointerType::get(*context_, 0);
  auto i64p = PointerType::get(*context_, 0);

  auto decl = [&](const char *name, LType *ret, std::vector<LType *> args) {
    return Function::Create(llvm::FunctionType::get(ret, args, false),
                            Function::ExternalLinkage, name, module_);
  };

  rt_.set_process_args_ = decl("YonaRuntimeProcessSetArguments", vd,
                               {LType::getInt32Ty(*context_), ptr});
  rt_.print_int_ = decl("YonaRuntimePrintInt", vd, {i64});
  rt_.print_float_ = decl("YonaRuntimePrintFloat", vd, {f64});
  rt_.print_string_ = decl("YonaRuntimePrintString", vd, {ptr});
  rt_.print_bool_ = decl("YonaRuntimePrintBool", vd, {i1});
  rt_.print_newline_ = decl("YonaRuntimePrintNewline", vd, {});
  rt_.print_seq_ = decl("YonaRuntimePrintSequence", vd, {i64p});
  rt_.string_concat_ = decl("YonaRuntimeStringConcatenate", ptr, {ptr, ptr});
  rt_.string_eq_ = decl("YonaRuntimeEqStringEq", i64, {ptr, ptr});
  rt_.seq_alloc_ = decl("YonaRuntimeSequenceAllocate", i64p, {i64});
  rt_.seq_set_ = decl("YonaRuntimeSequenceSet", vd, {i64p, i64, i64});
  rt_.seq_set_heap_ = decl("YonaRuntimeSequenceSetHeap", vd, {i64p, i64});
  rt_.seq_get_ = decl("YonaRuntimeSequenceGet", i64, {i64p, i64});
  rt_.seq_length_ = decl("YonaRuntimeSequenceLength", i64, {i64p});
  rt_.seq_cons_ = decl("YonaRuntimeSequencePrepend", i64p, {i64, i64p});
  rt_.seq_join_ = decl("YonaRuntimeSequenceJoin", i64p, {i64p, i64p});
  rt_.seq_head_ = decl("YonaRuntimeSequenceHead", i64, {i64p});
  rt_.seq_tail_ = decl("YonaRuntimeSequenceTail", i64p, {i64p});
  rt_.seq_tail_consume_ = decl("YonaRuntimeSequenceConsumeTail", i64p, {i64p});
  rt_.seq_is_empty_ = decl("YonaRuntimeSequenceIsEmpty", i64, {i64p});
  rt_.seq_snoc_ =
      decl("YonaRuntimeSequenceAppend", i64p, {i64p, i64}); // append to end
  rt_.seq_contains_ = decl("YonaRuntimeSequenceContains", i64, {i64p, i64});
  rt_.seq_difference_ =
      decl("YonaRuntimeSequenceDifference", i64p, {i64p, i64p});
  rt_.print_symbol_ =
      decl("YonaRuntimePrintSymbol", vd, {ptr}); // takes char* name

  // Set runtime
  rt_.set_alloc_ = decl("YonaRuntimeSetAllocate", i64p, {i64});
  rt_.set_insert_ = decl("YonaRuntimeSetInsert", i64p, {i64p, i64});
  rt_.set_set_heap_ = decl("YonaRuntimeSetSetHeap", vd, {i64p, i64});
  rt_.set_contains_ = decl("YonaRuntimeSetContains", i64, {i64p, i64});
  rt_.set_size_ = decl("YonaRuntimeSetSize", i64, {i64p});
  rt_.set_elements_ = decl("YonaRuntimeSetElements", i64p, {i64p});
  rt_.set_union_ = decl("YonaRuntimeSetUnion", i64p, {i64p, i64p});
  rt_.set_intersection_ =
      decl("YonaRuntimeSetIntersection", i64p, {i64p, i64p});
  rt_.set_difference_ = decl("YonaRuntimeSetDifference", i64p, {i64p, i64p});
  rt_.print_set_ = decl("YonaRuntimePrintSet", vd, {i64p});

  // Dict runtime
  rt_.dict_alloc_ = decl("YonaRuntimeDictionaryAllocate", i64p, {i64});
  rt_.dict_put_ = decl("YonaRuntimeDictionaryPut", i64p, {i64p, i64, i64});
  rt_.dict_set_heap_ =
      decl("YonaRuntimeDictionarySetHeap", vd, {i64p, i64, i64});
  rt_.dict_get_ = decl("YonaRuntimeDictionaryGet", i64, {i64p, i64, i64});
  rt_.dict_size_ = decl("YonaRuntimeDictionarySize", i64, {i64p});
  rt_.dict_contains_ = decl("YonaRuntimeDictionaryContains", i64, {i64p, i64});
  rt_.dict_keys_ = decl("YonaRuntimeDictionaryKeys", i64p, {i64p});
  rt_.print_dict_ = decl("YonaRuntimePrintDictionary", vd, {i64p});

  // Async runtime: promise = async_call(fn_ptr, arg), result =
  // async_await(promise) fn_ptr type: i64 (*)(i64) â€” function pointer taking
  // and returning i64
  auto fn_ptr_ty = PointerType::get(*context_, 0);
  auto promise_ptr = ptr; // opaque pointer to YonaTask
  rt_.async_call_ =
      decl("YonaRuntimeAsyncCall", promise_ptr, {fn_ptr_ty, i64, ptr});
  rt_.async_context_alloc_ =
      decl("YonaRuntimeAsyncContextAllocate", ptr, {i64});
  rt_.async_call_context_ =
      decl("YonaRuntimeAsyncCallContext", promise_ptr, {fn_ptr_ty, ptr, ptr});
  rt_.async_await_ = decl("YonaRuntimeTaskAwait", i64, {promise_ptr});
  rt_.async_await_keep_ = decl("YonaRuntimeTaskAwaitKeep", i64, {promise_ptr});

  // Task groups (structured concurrency)
  auto group_ptr = ptr; // opaque pointer to YonaTaskGroup
  rt_.group_begin_ = decl("YonaRuntimeTaskGroupBegin", group_ptr, {});
  rt_.group_register_ =
      decl("YonaRuntimeTaskGroupRegister", i64, {group_ptr, promise_ptr});
  rt_.group_register_io_ =
      decl("YonaRuntimeTaskGroupRegisterIo", i64, {group_ptr, i64});
  rt_.group_await_all_ = decl("YonaRuntimeTaskGroupAwaitAll", i64, {group_ptr});
  rt_.group_end_ = decl("YonaRuntimeTaskGroupEnd", vd, {group_ptr});
  rt_.group_cancel_ = decl("YonaRuntimeTaskGroupCancel", vd, {group_ptr});
  rt_.group_is_cancelled_ =
      decl("YonaRuntimeTaskGroupIsCancelled", i64, {group_ptr});
  rt_.async_call_grouped_ = decl("YonaRuntimeAsyncCallGrouped", promise_ptr,
                                 {fn_ptr_ty, i64, ptr, group_ptr});
  rt_.async_call_context_grouped_ =
      decl("YonaRuntimeAsyncCallContextGrouped", promise_ptr,
           {fn_ptr_ty, ptr, ptr, group_ptr});
  rt_.group_attach_arena_ =
      decl("YonaRuntimeTaskGroupAttachArena", vd, {group_ptr, ptr});
  rt_.group_arena_bind_push_ =
      decl("YonaRuntimeTaskGroupArenaBindPush", vd, {group_ptr});
  rt_.group_arena_bind_pop_ = decl("YonaRuntimeTaskGroupArenaBindPop", vd, {});

  // ADT runtime (recursive types)
  auto i8 = LType::getInt8Ty(*context_);
  rt_.adt_alloc_ = decl("YonaRuntimeAdtAllocate", ptr, {i64, i64});
  rt_.adt_get_tag_ = decl("YonaRuntimeAdtGetTag", i64, {ptr});
  rt_.adt_get_field_ = decl("YonaRuntimeAdtGetField", i64, {ptr, i64});
  rt_.adt_set_field_ = decl("YonaRuntimeAdtSetField", vd, {ptr, i64, i64});
  rt_.adt_set_heap_mask_ = decl("YonaRuntimeAdtSetHeapMask", vd, {ptr, i64});

  // General closures: {fn_ptr, ret_tag, arity, cap0, ...} with env-passing
  rt_.closure_create_ =
      decl("YonaRuntimeClosureCreate", ptr, {ptr, i64, i64, i64});
  rt_.closure_set_cap_ =
      decl("YonaRuntimeClosureSetCapture", vd, {ptr, i64, i64});
  rt_.closure_get_cap_ = decl("YonaRuntimeClosureGetCapture", i64, {ptr, i64});
  rt_.closure_set_heap_mask_ =
      decl("YonaRuntimeClosureSetHeapMask", vd, {ptr, i64});
  rt_.closure_set_borrow_mask_ =
      decl("YonaRuntimeClosureSetBorrowMask", vd, {ptr, i64});

  // Tuple allocation with metadata
  rt_.tuple_alloc_ = decl("YonaRuntimeTupleAllocate", ptr, {i64});
  rt_.tuple_set_ = decl("YonaRuntimeTupleSet", vd, {ptr, i64, i64});
  rt_.tuple_set_heap_mask_ =
      decl("YonaRuntimeTupleSetHeapMask", vd, {ptr, i64});

  // Reference counting
  rt_.rc_inc_ = decl("YonaRuntimeRetain", vd, {ptr});
  rt_.rc_dec_ = decl("YonaRuntimeRelease", vd, {ptr});

  // Arena allocator
  rt_.arena_create_ = decl("YonaRuntimeArenaCreate", ptr, {i64});
  rt_.arena_alloc_ = decl("YonaRuntimeArenaAllocate", ptr, {ptr, i64, i64});
  rt_.arena_destroy_ = decl("YonaRuntimeArenaDestroy", vd, {ptr});

  // io_uring await
  rt_.io_await_ = decl("YonaRuntimeIoAwait", i64, {i64});

  // Resource cleanup (with expression)
  // Bytes
  rt_.byte_array_alloc_ = decl("YonaRuntimeByteArrayAllocate", ptr, {i64});
  rt_.byte_array_length_ = decl("YonaRuntimeByteArrayLength", i64, {ptr});
  rt_.byte_array_get_ = decl("YonaRuntimeByteArrayGet", i64, {ptr, i64});
  rt_.byte_array_set_ = decl("YonaRuntimeByteArraySet", vd, {ptr, i64, i64});
  rt_.byte_array_concat_ =
      decl("YonaRuntimeByteArrayConcatenate", ptr, {ptr, ptr});
  rt_.byte_array_slice_ =
      decl("YonaRuntimeByteArraySlice", ptr, {ptr, i64, i64});
  rt_.byte_array_from_string_ =
      decl("YonaRuntimeByteArrayFromString", ptr, {ptr});
  rt_.byte_array_to_string_ = decl("YonaRuntimeByteArrayToString", ptr, {ptr});
  rt_.byte_array_from_seq_ =
      decl("YonaRuntimeByteArrayFromSequence", ptr, {ptr});
  rt_.byte_array_to_seq_ = decl("YonaRuntimeByteArrayToSequence", ptr, {ptr});
  rt_.print_byte_array_ = decl("YonaRuntimePrintByteArray", vd, {ptr});

  // IntArray
  rt_.int_array_alloc_ = decl("YonaRuntimeIntArrayAllocate", ptr, {i64});
  rt_.int_array_length_ = decl("YonaRuntimeIntArrayLength", i64, {ptr});
  rt_.int_array_get_ = decl("YonaRuntimeIntArrayGet", i64, {ptr, i64});
  rt_.int_array_set_ = decl("YonaRuntimeIntArraySet", vd, {ptr, i64, i64});
  rt_.int_array_head_ = decl("YonaRuntimeIntArrayHead", i64, {ptr});
  rt_.int_array_tail_ = decl("YonaRuntimeIntArrayTail", ptr, {ptr});
  rt_.int_array_cons_ = decl("YonaRuntimeIntArrayPrepend", ptr, {i64, ptr});
  rt_.int_array_join_ = decl("YonaRuntimeIntArrayJoin", ptr, {ptr, ptr});
  rt_.print_int_array_ = decl("YonaRuntimePrintIntArray", vd, {ptr});

  // Channels
  rt_.channel_new_ = decl("YonaStdChannelChannel", ptr, {i64, ptr});
  rt_.channel_send_ = decl("YonaRuntimeChannelSend", vd, {ptr, i64});
  rt_.channel_recv_ = decl("YonaRuntimeChannelReceive", i64, {ptr});
  rt_.channel_try_recv_ = decl("YonaRuntimeChannelTryReceive", i64, {ptr});
  rt_.channel_close_ = decl("YonaRuntimeChannelClose", vd, {ptr});
  rt_.channel_is_closed_ = decl("YonaRuntimeChannelIsClosed", i64, {ptr});
  rt_.channel_length_ = decl("YonaRuntimeChannelLength", i64, {ptr});
  rt_.channel_capacity_ = decl("YonaRuntimeChannelCapacity", i64, {ptr});

  // FloatArray
  auto dbl = LType::getDoubleTy(*context_);
  rt_.float_array_alloc_ = decl("YonaRuntimeFloatArrayAllocate", ptr, {i64});
  rt_.float_array_length_ = decl("YonaRuntimeFloatArrayLength", i64, {ptr});
  rt_.float_array_get_ = decl("YonaRuntimeFloatArrayGet", dbl, {ptr, i64});
  rt_.float_array_set_ = decl("YonaRuntimeFloatArraySet", vd, {ptr, i64, dbl});
  rt_.float_array_head_ = decl("YonaRuntimeFloatArrayHead", dbl, {ptr});
  rt_.float_array_tail_ = decl("YonaRuntimeFloatArrayTail", ptr, {ptr});
  rt_.float_array_cons_ = decl("YonaRuntimeFloatArrayPrepend", ptr, {dbl, ptr});
  rt_.float_array_join_ = decl("YonaRuntimeFloatArrayJoin", ptr, {ptr, ptr});
  rt_.print_float_array_ = decl("YonaRuntimePrintFloatArray", vd, {ptr});

  rt_.box_ = decl("YonaRuntimeBox", ptr, {ptr, i64});
  rt_.close_ = decl("YonaRuntimeClose", vd, {i64});

  // Exception handling (SJLJ via llvm.eh.sjlj.setjmp + __builtin_longjmp).
  // Codegen emits the SJLJ intrinsic in the user's stack frame; runtime
  // raise() does __builtin_longjmp into that buffer. See exceptions.c.
  auto i32 = LType::getInt32Ty(*context_);
  rt_.try_begin_ = decl("YonaRuntimeTryBegin", ptr, {}); // returns void*[5]
  rt_.try_end_ = decl("YonaRuntimeTryEnd", vd, {});
  rt_.raise_ = decl("YonaRuntimeRaise", vd, {i64, ptr});
  rt_.get_exc_sym_ = decl("YonaRuntimeGetExceptionSymbol", i64, {});
  rt_.get_exc_msg_ = decl("YonaRuntimeGetExceptionMessage", ptr, {});
  rt_.raise_->addFnAttr(llvm::Attribute::NoReturn);

  // Perceus phase 3: frame-scoped heap cleanup on raise. See
  // src/Runtime/Core/Exceptions.c for the runtime layout.
  rt_.frame_push_ = decl("YonaRuntimeFramePush", vd, {ptr});
  rt_.frame_pop_ = decl("YonaRuntimeFramePop", vd, {ptr});
  rt_.frame_transfer_ = decl("YonaRuntimeFrameTransfer", vd, {ptr});
  rt_.try_depth_ = decl("YonaRuntimeTryDepth", i32, {});
}

void Codegen::report_error(const SourceRange &loc, const std::string &message) {
  Session->recordError();
  Session->diagnostics().error(loc, message);
}

std::string Codegen::suggest_similar(const std::string &name) const {
  // Levenshtein distance for "did you mean?" suggestions
  auto edit_distance = [](const std::string &a, const std::string &b) -> int {
    int m = a.size(), n = b.size();
    std::vector<int> dp(n + 1);
    for (int j = 0; j <= n; j++)
      dp[j] = j;
    for (int i = 1; i <= m; i++) {
      int prev = dp[0];
      dp[0] = i;
      for (int j = 1; j <= n; j++) {
        int tmp = dp[j];
        dp[j] = std::min(
            {dp[j] + 1, dp[j - 1] + 1, prev + (a[i - 1] != b[j - 1] ? 1 : 0)});
        prev = tmp;
      }
    }
    return dp[n];
  };

  std::string best;
  int best_dist = 4; // max distance threshold
  for (auto &[k, _] : named_values_) {
    int d = edit_distance(name, k);
    if (d < best_dist) {
      best = k;
      best_dist = d;
    }
  }
  for (auto &[k, _] : deferred_functions_) {
    int d = edit_distance(name, k);
    if (d < best_dist) {
      best = k;
      best_dist = d;
    }
  }
  for (auto &[k, _] : imports_.extern_functions) {
    int d = edit_distance(name, k);
    if (d < best_dist) {
      best = k;
      best_dist = d;
    }
  }
  for (auto &[k, _] : types_.adt_constructors) {
    int d = edit_distance(name, k);
    if (d < best_dist) {
      best = k;
      best_dist = d;
    }
  }
  return best;
}

// ===== Public API =====

std::string Codegen::mangle_name(const std::string &module_fqn,
                                 const std::string &func_name) {
  return model::mangleExport(module_fqn, func_name);
}

std::string Codegen::trait_instance_local_name(const InstanceDeclNode *instance,
                                               const std::string &method_name) {
  std::string symbol = instance->trait_name;
  for (const auto &head : instance->type_names)
    symbol += "_" + head;
  return symbol + "_" + method_name;
}

std::string
Codegen::mangle_trait_instance_method(const std::string &module_fqn,
                                      const InstanceDeclNode *instance,
                                      const std::string &method_name) {
  return mangle_name(module_fqn,
                     trait_instance_local_name(instance, method_name));
}

Module *Codegen::compile(AstNode *node) {
  auto fn = codegen_main(node);
  if (!fn)
    return nullptr;
  // Transfer reconciliation records drops while nested control-flow is
  // still under construction. Materialize them only after every generated
  // function has a complete CFG, so LLVM dominance analysis never sees a
  // detached or unterminated successor.
  flush_pending_transfer_drops();
  finalize_debug_info();
  std::string err;
  raw_string_ostream os(err);
  if (verifyModule(*module_, &os)) {
    std::cerr << "Module verification failed:\n" << err << "\n";
    return nullptr;
  }
  optimize();
  err.clear();
  if (verifyModule(*module_, &os)) {
    std::cerr << "Module verification failed after optimization:\n"
              << err << "\n";
    return nullptr;
  }
  return module_;
}

// Forward declarations for type annotation support
static CType yona_type_to_ctype(const types::Type &t);
static std::pair<std::vector<CType>, CType>
uncurry_type_signature(const types::Type &t);

static std::optional<CType>
checked_type_to_ctype(typechecker::TypeChecker &checker,
                      typechecker::MonoTypePtr type) {
  type = checker.zonk(type);
  if (!type)
    return std::nullopt;
  if (type->tag == typechecker::MonoType::Con) {
    switch (type->con) {
    case typechecker::TyCon::Int:
    case typechecker::TyCon::Char:
    case typechecker::TyCon::Byte:
      return CType::INT;
    case typechecker::TyCon::Float:
      return CType::FLOAT;
    case typechecker::TyCon::Bool:
      return CType::BOOL;
    case typechecker::TyCon::String:
      return CType::STRING;
    case typechecker::TyCon::Symbol:
      return CType::SYMBOL;
    case typechecker::TyCon::Unit:
      return CType::UNIT;
    case typechecker::TyCon::Seq:
      return CType::SEQ;
    case typechecker::TyCon::Set:
      return CType::SET;
    case typechecker::TyCon::Dict:
      return CType::DICT;
    case typechecker::TyCon::Tuple:
      return CType::TUPLE;
    case typechecker::TyCon::Function:
      return CType::FUNCTION;
    case typechecker::TyCon::Promise:
      return CType::PROMISE;
    case typechecker::TyCon::ByteArray:
      return CType::BYTE_ARRAY;
    }
  }
  if (type->tag == typechecker::MonoType::Arrow)
    return CType::FUNCTION;
  if (type->tag == typechecker::MonoType::MTuple)
    return CType::TUPLE;
  if (type->tag == typechecker::MonoType::App) {
    if (type->type_name == "Seq")
      return CType::SEQ;
    if (type->type_name == "Set")
      return CType::SET;
    if (type->type_name == "Dict")
      return CType::DICT;
    if (type->type_name == "Promise")
      return CType::PROMISE;
    if (type->type_name == "ByteArray")
      return CType::BYTE_ARRAY;
    if (type->type_name == "IntArray")
      return CType::INT_ARRAY;
    if (type->type_name == "FloatArray")
      return CType::FLOAT_ARRAY;
    if (type->type_name == "Channel")
      return CType::CHANNEL;
    return CType::ADT;
  }
  return std::nullopt;
}

static std::optional<CType>
checked_function_result_ctype(typechecker::TypeChecker &checker,
                              typechecker::MonoTypePtr type) {
  type = checker.zonk(type);
  if (!type || type->tag != typechecker::MonoType::Arrow)
    return std::nullopt;
  auto *result = checker.zonk(type->return_type);
  while (result && result->tag == typechecker::MonoType::Arrow)
    result = checker.zonk(result->return_type);
  return checked_type_to_ctype(checker, result);
}

Module *Codegen::compile_module(ModuleDecl *mod) {
  imports_.interface_symbols.clear();
  interface_export_filter_active_ = true;
  interface_exported_types_.clear();
  interface_opaque_types_.clear();
  interface_trait_names_.clear();
  interface_instance_keys_.clear();
  interface_exported_types_.insert(mod->exported_types.begin(),
                                   mod->exported_types.end());
  interface_opaque_types_.insert(mod->opaque_exported_types.begin(),
                                 mod->opaque_exported_types.end());

  // Build the module FQN string
  std::string fqn;
  if (mod->fqn->packageName.has_value()) {
    auto *pkg = mod->fqn->packageName.value();
    for (size_t i = 0; i < pkg->parts.size(); i++) {
      if (i > 0)
        fqn += "\\";
      fqn += pkg->parts[i]->value;
    }
    fqn += "\\";
  }
  fqn += mod->fqn->moduleName->value;
  current_module_fqn_ = fqn;

  // Build export set for visibility control
  std::unordered_set<std::string> export_set(mod->exports.begin(),
                                             mod->exports.end());

  // Build exported types set â€” constructors will be added after ADT
  // processing
  std::unordered_set<std::string> exported_type_set(mod->exported_types.begin(),
                                                    mod->exported_types.end());

  // First pass: collect ADTs in this module that contain a function-typed
  // field. Any ADT that references one of these in a field must also be
  // heap-allocated, because the closure ABI for function fields returns
  // a pointer (i64), and a flat-struct ADT containing such a function
  // field would not survive a closure-returning-it round trip. We then
  // iterate to a fixed point so mutual references like
  // `Stream a = Stream (() -> Step a)` / `Step a = Yield a (Stream a) | Done`
  // both end up heap-allocated.
  std::unordered_set<std::string> heap_adts;
  for (auto *adt : mod->adt_declarations) {
    for (auto *ctor : adt->variants) {
      for (auto &ft : ctor->field_type_names) {
        if (ft.is_function_type) {
          heap_adts.insert(adt->name);
          break;
        }
      }
      if (heap_adts.count(adt->name))
        break;
    }
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto *adt : mod->adt_declarations) {
      if (heap_adts.count(adt->name))
        continue;
      for (auto *ctor : adt->variants) {
        for (auto &ft : ctor->field_type_names) {
          std::string head = ft.name;
          auto sp = head.find(' ');
          if (sp != std::string::npos)
            head = head.substr(0, sp);
          if (heap_adts.count(head)) {
            heap_adts.insert(adt->name);
            changed = true;
            break;
          }
        }
        if (heap_adts.count(adt->name))
          break;
      }
    }
  }

  // Process ADT declarations: register constructors, detect recursion
  for (auto *adt : mod->adt_declarations) {
    types_.adt_type_params[adt->name] = adt->type_params;
    int max_arity = 0;
    bool is_recursive = heap_adts.count(adt->name) > 0;
    for (auto *ctor : adt->variants) {
      int a = static_cast<int>(ctor->field_type_names.size());
      if (a > max_arity)
        max_arity = a;
      // Check if any field type references the ADT itself or contains
      // a function type â€” function fields make ADTs heap-allocated because
      // closures may return values of the same type (lazy data structures)
      for (auto &ft : ctor->field_type_names) {
        if (ft.is_function_type || ft.name == adt->name) {
          is_recursive = true;
          break;
        }
      }
    }

    for (size_t ci = 0; ci < adt->variants.size(); ci++) {
      auto *ctor = adt->variants[ci];
      int arity = static_cast<int>(ctor->field_type_names.size());
      // Map field types to CTypes
      std::vector<CType> ftypes;
      std::vector<CType> fn_rets;
      std::vector<std::string> fn_ret_adt_names;
      auto field_name_to_ctype = [](std::string name) -> CType {
        auto sp = name.find(' ');
        if (sp != std::string::npos)
          name = name.substr(0, sp);
        if (name == "Int" || name == "a" || name == "b" || name == "e" ||
            name == "s")
          return CType::INT;
        if (name == "Float")
          return CType::FLOAT;
        if (name == "String")
          return CType::STRING;
        if (name == "Bool")
          return CType::BOOL;
        if (name == "Symbol")
          return CType::SYMBOL;
        if (name == "Seq")
          return CType::SEQ;
        if (name == "Set")
          return CType::SET;
        if (name == "Dict")
          return CType::DICT;
        if (name == "Channel")
          return CType::CHANNEL;
        return CType::ADT;
      };
      for (auto &ft : ctor->field_type_names) {
        if (ft.is_function_type) {
          ftypes.push_back(CType::FUNCTION);
          if (!ft.return_types.empty()) {
            std::string ret_name = ft.return_types[0].name;
            auto ret_ct = field_name_to_ctype(ret_name);
            auto sp = ret_name.find(' ');
            if (sp != std::string::npos)
              ret_name = ret_name.substr(0, sp);
            fn_rets.push_back(ret_ct);
            fn_ret_adt_names.push_back(ret_ct == CType::ADT ? ret_name : "");
          } else {
            fn_rets.push_back(CType::INT);
            fn_ret_adt_names.push_back("");
          }
        } else {
          auto ct =
              ft.name == adt->name ? CType::ADT : field_name_to_ctype(ft.name);
          ftypes.push_back(ct);
          fn_rets.push_back(CType::INT);
          fn_ret_adt_names.push_back("");
        }
      }

      std::vector<AdtInfo::FieldShape> field_shapes;
      field_shapes.reserve(ctor->field_type_names.size());
      for (size_t fi = 0; fi < ctor->field_type_names.size(); ++fi) {
        auto shape = field_shape_from_field_type(ctor->field_type_names[fi]);
        ftypes[fi] = shape.type;
        fn_rets[fi] = shape.function_return_type;
        fn_ret_adt_names[fi] = shape.function_return_adt_name;
        field_shapes.push_back(std::move(shape));
      }

      types_.adt_constructors[ctor->name] = {
          adt->name,
          static_cast<int>(ci),
          arity,
          static_cast<int>(adt->variants.size()),
          max_arity,
          is_recursive,
          ctor->field_names,
          ftypes,
          fn_rets,
          fn_ret_adt_names,
          field_shapes};
      types_.adt_constructors[ctor->name].declared_field_types =
          ctor->field_type_names;
    }
  }

  // Expand exported types: add all constructors of exported types to export_set
  for (auto *adt : mod->adt_declarations) {
    if (exported_type_set.count(adt->name) > 0 &&
        interface_opaque_types_.count(adt->name) == 0) {
      for (auto *ctor : adt->variants) {
        export_set.insert(ctor->name);
      }
    }
  }

  // Existing modules may export constructors directly without `export type`.
  // Their ADT metadata is still required by importers; the opaque form only
  // suppresses constructors that are hidden by an opaque type export.
  for (const auto &name : export_set) {
    auto ctor = types_.adt_constructors.find(name);
    if (ctor != types_.adt_constructors.end())
      interface_exported_types_.insert(ctor->second.type_name);
  }

  // Load re-export source modules so their functions are available
  // for use within this module's own functions
  for (auto &re : mod->re_exports) {
    std::string path_str;
    for (char c : re.source_module)
      path_str += (c == '\\') ? '/' : c;
    load_module_interface(std::filesystem::path(path_str));
    for (auto &name : re.names)
      register_import(re.source_module, name, name);
  }

  // Ensure Prelude trait instance methods (Show_Int__show, etc.) are declared
  // as extern functions before derive expansion can call them.
  register_trait_externs();

  // ===== Auto-derive expansion =====
  // For each ADT with a `deriving` clause, look up the registered strategy
  // and generate + compile trait instance methods. Fully registry-driven â€”
  // no hardcoded trait names here.
  // Keep reparsed modules alive so deferred function AST pointers don't dangle.
  std::vector<std::unique_ptr<ast::ModuleDecl>> derived_modules;
  for (auto *adt : mod->adt_declarations) {
    if (adt->derive_traits.empty())
      continue;

    // Collect constructor metadata
    DeriveAdtInfo dai;
    dai.type_name = adt->name;
    dai.type_params = adt->type_params;
    for (size_t ci = 0; ci < adt->variants.size(); ci++) {
      auto *ctor = adt->variants[ci];
      DeriveCtorInfo dci;
      dci.name = ctor->name;
      dci.tag = static_cast<int>(ci);
      dci.arity = static_cast<int>(ctor->field_type_names.size());
      dci.field_names = ctor->field_names;
      for (auto &ft : ctor->field_type_names) {
        bool is_param = false;
        for (auto &tp : adt->type_params)
          if (ft.name == tp) {
            is_param = true;
            break;
          }
        dci.field_type_refs.push_back(is_param ? ft.name : "");
        dci.field_type_names.push_back(ft.name);
      }
      dai.constructors.push_back(dci);
    }
    auto adt_it = types_.adt_constructors.find(adt->variants[0]->name);
    if (adt_it != types_.adt_constructors.end())
      dai.is_recursive = adt_it->second.is_recursive;

    for (auto &trait_name : adt->derive_traits) {
      const bool marker_trait =
          trait_name == "Send" || trait_name == "Shareable";
      auto *strategy = Session->derivations().getStrategy(trait_name);
      if (!strategy && !marker_trait) {
        auto available = Session->derivations().allDerivableTraits();
        std::string avail_str;
        for (size_t i = 0; i < available.size(); i++) {
          if (i > 0)
            avail_str += ", ";
          avail_str += available[i];
        }
        Session->diagnostics().error(adt->Range, compiler::ErrorCode::E0400,
                                     "unknown derivable trait '" + trait_name +
                                         "'; available: " + avail_str);
        continue;
      }

      // The source deriving clause is authoritative. Replace the bootstrap
      // contract once instead of appending constraints during regeneration.
      std::string instance_key = trait_name + ":" + adt->name;
      TraitInstanceInfo derived_instance;
      derived_instance.trait_name = trait_name;
      derived_instance.type_names = {adt->name};
      derived_instance.type_params = adt->type_params;
      for (const auto &parameter : adt->type_params)
        derived_instance.constraints.emplace_back(trait_name, parameter);
      types_.trait_instances[instance_key] = std::move(derived_instance);
      interface_instance_keys_.insert(instance_key);

      // Marker traits are compile-time evidence only. Their instance
      // contracts are serialized, but they intentionally generate no
      // method, dictionary, symbol, or runtime call.
      if (marker_trait)
        continue;

      // Generate and compile each method from the strategy
      for (auto &method_name : strategy->method_names) {
        std::string method_source = strategy->generator(dai);
        if (method_source.empty())
          continue;

        const std::string local_name =
            trait_name + "_" + adt->name + "_" + method_name;
        std::string mangled = mangle_name(fqn, local_name);
        imports_.export_identities[mangled] = {fqn, local_name};
        imports_.module_exports[fqn][local_name] = mangled;
        auto mod_result = reparse_genfn(method_name, method_source);
        if (!mod_result)
          continue;

        // A bootstrap compile of Prelude may already have an extern
        // declaration for this derived symbol from the previous
        // Prelude.yonai. The new source definition must replace that
        // declaration or the emitted object contains no implementation.
        compiled_functions_.erase(mangled);
        deferred_functions_.erase(mangled);
        // Keep a matching bootstrap declaration in the module.
        // compile_function fills that canonical Function in place so
        // any users created while loading the previous interface keep
        // a valid LLVM use-list.

        // Register the trait instance
        auto &tii = types_.trait_instances[instance_key];
        tii.method_mangled_names[method_name] = mangled;

        // Compile the reparsed function
        for (auto *fn_decl : mod_result->functions)
          codegen_function_def(fn_decl, mangled);

        // Store GENFN source for cross-module monomorphization
        imports_.imported_sources[mangled] = {method_source, method_name, fqn};
        imports_.function_source[mangled] = {method_source, method_name, fqn};

        // Keep module alive (deferred functions hold AST pointers)
        derived_modules.push_back(std::move(mod_result));
      }
    }
  }

  // Register trait declarations (Phase 3: with superclasses and default impls)
  for (auto *trait : mod->trait_declarations) {
    TraitInfo ti;
    ti.name = trait->name;
    ti.type_params = trait->type_params;
    ti.superclasses = std::vector<std::pair<std::string, std::string>>(
        trait->superclasses.begin(), trait->superclasses.end());
    for (auto &m : trait->methods) {
      ti.method_names.push_back(m.name);
      ti.method_type_descriptors[m.name] =
          source_type_descriptor(m.type_signature);
      if (m.default_impl) {
        ti.default_impls[m.name] = m.default_impl;
      }
    }
    types_.traits[trait->name] = ti;
    interface_trait_names_.insert(trait->name);
  }

  // Register trait instances: mangle method names and register as deferred
  // functions Phase 2: handle constrained instances with ADT type names Phase
  // 3: fill in default methods from trait declaration
  for (auto *inst : mod->instance_declarations) {
    // Multi-param key: "Trait:Type1:Type2"
    std::string key = inst->trait_name;
    for (auto &tn : inst->type_names)
      key += ":" + tn;
    TraitInstanceInfo tii;
    tii.trait_name = inst->trait_name;
    tii.type_names = inst->type_names;
    tii.type_params = inst->type_params;
    tii.constraints = std::vector<std::pair<std::string, std::string>>(
        inst->constraints.begin(), inst->constraints.end());

    // Phase 3: Verify superclass instances exist
    auto trait_it = types_.traits.find(inst->trait_name);
    if (trait_it != types_.traits.end() && !inst->type_names.empty()) {
      const auto &instance_type = inst->type_names.front();
      for (auto &[sc_trait, sc_var] : trait_it->second.superclasses) {
        std::string sc_key = sc_trait + ":" + instance_type;
        if (types_.trait_instances.find(sc_key) ==
            types_.trait_instances.end()) {
          std::cerr << "Warning: instance " << inst->trait_name << " "
                    << instance_type << " requires " << sc_trait << " "
                    << instance_type
                    << " (superclass), but no such instance found yet\n";
        }
      }
    }

    // Collect provided method names
    std::unordered_set<std::string> provided_methods;
    for (auto *method : inst->methods) {
      const std::string local_name =
          trait_instance_local_name(inst, method->name);
      std::string mangled =
          mangle_trait_instance_method(fqn, inst, method->name);
      imports_.export_identities[mangled] = {fqn, local_name};
      imports_.module_exports[fqn][local_name] = mangled;
      tii.method_mangled_names[method->name] = mangled;
      provided_methods.insert(method->name);

      // A previous interface may have bootstrapped this exact symbol as
      // an extern declaration. Source is authoritative: replace the
      // stale declaration so rebuilding Prelude or another module emits
      // the implementation described by the current source.
      compiled_functions_.erase(mangled);
      deferred_functions_.erase(mangled);
      if (auto *stale = module_->getFunction(mangled);
          stale && stale->isDeclaration() && stale->use_empty())
        stale->eraseFromParent();

      // Register as deferred function
      codegen_function_def(method, mangled);
      // Standalone consumers do not link a separately compiled Prelude
      // object. Every Yona-defined instance body therefore needs GENFN
      // source, including unconstrained concrete instances; only the
      // primitive extern called by such a body is native.
      if (!method->source_text.empty()) {
        imports_.imported_sources[mangled] = {method->source_text, method->name,
                                              fqn};
        imports_.function_source[mangled] = {method->source_text, method->name,
                                             fqn};
        imports_.private_genfn_symbols.insert(mangled);
      }
    }

    // Phase 3: Fill in default implementations for missing methods
    if (trait_it != types_.traits.end()) {
      for (auto &[method_name, default_fn] : trait_it->second.default_impls) {
        if (provided_methods.find(method_name) == provided_methods.end()) {
          // Method not provided by instance â€” use default from trait
          std::string mangled =
              mangle_trait_instance_method(fqn, inst, method_name);
          const std::string local_name =
              trait_instance_local_name(inst, method_name);
          imports_.export_identities[mangled] = {fqn, local_name};
          imports_.module_exports[fqn][local_name] = mangled;
          tii.method_mangled_names[method_name] = mangled;
          codegen_function_def(default_fn, mangled);
        }
      }
    }

    types_.trait_instances[key] = tii;
    interface_instance_keys_.insert(key);
  }

  // Process module-level extern declarations
  for (auto *ext : mod->extern_declarations) {
    codegen_extern_decl(ext);
    // Add to export set if exported
    if (export_set.count(ext->name)) {
      std::string mangled = mangle_name(fqn, ext->name);
      imports_.export_identities[mangled] = {fqn, ext->name};
      imports_.module_exports[fqn][ext->name] = mangled;
      // The extern is already declared; create a wrapper with the mangled name
      auto cf_it = compiled_functions_.find(ext->name);
      if (cf_it != compiled_functions_.end()) {
        auto &cf = cf_it->second;
        imports_.meta[mangled] = module_meta_from_compiled(cf);
        imports_.interface_symbols.insert(mangled);
        // Create a forwarding wrapper
        if (cf.fn->getName() != mangled) {
          auto *wrapper =
              Function::Create(cf.fn->getFunctionType(),
                               Function::ExternalLinkage, mangled, module_);
          auto *bb = BasicBlock::Create(*context_, "entry", wrapper);
          builder_->SetInsertPoint(bb);
          std::vector<Value *> args;
          for (auto &arg : wrapper->args())
            args.push_back(&arg);
          auto *result = builder_->CreateCall(cf.fn, args);
          builder_->CreateRet(result);
        }
      }
    }
  }

  // First pass: register all functions as deferred
  for (auto *func : mod->functions) {
    std::string fn_name = func->name;
    codegen_function_def(func, fn_name);

    // Store source text for exported generic functions (.yonai emission)
    // Every source-defined export needs its source in the interface.  The
    // importing compiler may have to monomorphize it locally, including
    // when it has an explicit type signature; omitting annotated exports
    // left callers with only a lossy ABI row and could silently drop
    // arguments during recompilation.
    if (export_set.count(fn_name) && !func->source_text.empty()) {
      std::string mangled = mangle_name(fqn, fn_name);
      imports_.function_source[mangled] = {func->source_text, fn_name, fqn};
      imports_.export_identities[mangled] = {fqn, fn_name};
      imports_.module_exports[fqn][fn_name] = mangled;
      imports_.interface_symbols.insert(mangled);
    }
  }

  // Unexported helpers referenced by exported GENFN bodies must also be
  // emitted so importers can remonomorphize the export without E0104.
  {
    std::unordered_map<std::string, FunctionExpr *> module_fns;
    for (auto *func : mod->functions)
      module_fns[func->name] = func;

    std::vector<FunctionExpr *> work;
    for (auto *func : mod->functions) {
      if (export_set.count(func->name))
        work.push_back(func);
    }
    // Instance GENFN bodies are public dictionary implementations even
    // though their method names are not ordinary module exports. Include
    // their private helper closure in the same dependency walk.
    for (auto *instance : mod->instance_declarations)
      for (auto *method : instance->methods)
        if (method && !method->source_text.empty())
          work.push_back(method);
    std::unordered_set<FunctionExpr *> visited;
    while (!work.empty()) {
      FunctionExpr *func = work.back();
      work.pop_back();
      if (!visited.insert(func).second)
        continue;

      std::unordered_set<std::string> bound, free;
      for (auto *pat : func->patterns) {
        if (pat->get_type() == ast::AST_PATTERN_VALUE) {
          auto *pv = static_cast<PatternValue *>(pat);
          if (auto *id = std::get_if<IdentifierExpr *>(&pv->expr))
            bound.insert((*id)->name->value);
        }
      }
      if (!func->bodies.empty()) {
        if (auto *bwg = dynamic_cast<BodyWithoutGuards *>(func->bodies[0]))
          collect_free_vars(bwg->expr, bound, free);
      }
      for (const auto &fv : free) {
        auto it = module_fns.find(fv);
        if (it == module_fns.end() || export_set.count(fv))
          continue;
        if (it->second->source_text.empty())
          continue;
        std::string dep_mangled = mangle_name(fqn, fv);
        imports_.function_source[dep_mangled] = {it->second->source_text, fv,
                                                 fqn};
        imports_.export_identities[dep_mangled] = {fqn, fv};
        imports_.module_exports[fqn][fv] = dep_mangled;
        imports_.private_genfn_symbols.insert(dep_mangled);
        work.push_back(it->second);
      }
    }
  }

  // Second pass: compile only EXPORTED functions with inferred types.
  // Non-exported functions stay deferred and compile on demand at call sites
  // with correct argument types (monomorphization).
  for (auto *func : mod->functions) {
    std::string fn_name = func->name;
    bool is_exported = export_set.count(fn_name) > 0;
    if (!is_exported)
      continue; // internal functions compile at call sites

    auto def_it = deferred_functions_.find(fn_name);
    if (def_it == deferred_functions_.end())
      continue;

    // Use explicit type annotation if present, otherwise infer from
    // patterns/body
    std::vector<CType> annotated_param_types;
    std::vector<std::vector<CType>> annotated_param_subtypes;
    std::vector<std::string> annotated_param_adt_names;
    if (func->type_signature.has_value()) {
      auto [params, ret] = uncurry_type_signature(*func->type_signature);
      annotated_param_types = params;
      const types::Type *current_type = &*func->type_signature;
      while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
          *current_type)) {
        auto ft = std::get<std::shared_ptr<types::FunctionType>>(*current_type);
        if (std::holds_alternative<std::shared_ptr<types::NamedType>>(
                ft->argumentType))
          annotated_param_adt_names.push_back(
              std::get<std::shared_ptr<types::NamedType>>(ft->argumentType)
                  ->name);
        else
          annotated_param_adt_names.push_back("");
        std::vector<CType> subtypes;
        if (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
                ft->argumentType)) {
          auto arg_ft =
              std::get<std::shared_ptr<types::FunctionType>>(ft->argumentType);
          types::Type result_type = arg_ft->returnType;
          while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
              result_type))
            result_type =
                std::get<std::shared_ptr<types::FunctionType>>(result_type)
                    ->returnType;
          subtypes.push_back(yona_type_to_ctype(result_type));
        }
        annotated_param_subtypes.push_back(std::move(subtypes));
        current_type = &ft->returnType;
      }
    }

    auto inferred =
        func->type_signature.has_value()
            ? std::vector<InferredParamType>() // not needed when annotated
            : infer_param_types(func);

    // The checked HM type is authoritative for parameter roles.  The AST
    // heuristic remains useful for layout clues, but it cannot reliably
    // see a higher-order parameter whose only use is inside a nested
    // closure.  Preserve callable return tags as well so closure calls do
    // not fall back to "same type as the first argument".
    std::vector<std::optional<CType>> checked_param_types;
    std::vector<std::vector<CType>> checked_param_subtypes;
    if (!func->type_signature.has_value() && type_checker_) {
      auto *checked = type_checker_->zonk(type_checker_->type_of(func));
      for (size_t i = 0; i < func->patterns.size() && checked &&
                         checked->tag == typechecker::MonoType::Arrow;
           ++i) {
        auto *argument = type_checker_->zonk(checked->param_type);
        checked_param_types.push_back(
            checked_type_to_ctype(*type_checker_, argument));
        std::vector<CType> subtypes;
        if (auto result =
                checked_function_result_ctype(*type_checker_, argument))
          subtypes.push_back(*result);
        checked_param_subtypes.push_back(std::move(subtypes));
        checked = type_checker_->zonk(checked->return_type);
      }
    }

    std::vector<TypedValue> typed_args;
    for (size_t i = 0; i < func->patterns.size(); i++) {
      CType ct;
      if (!annotated_param_types.empty() && i < annotated_param_types.size())
        ct = annotated_param_types[i];
      else if (i < checked_param_types.size() &&
               checked_param_types[i].has_value())
        ct = *checked_param_types[i];
      else
        ct = (i < inferred.size()) ? inferred[i].type : CType::INT;
      auto *dummy_val = ConstantInt::get(LType::getInt64Ty(*context_), 0);

      if (ct == CType::TUPLE) {
        // Tuples are i64 (ptrtoint'd heap pointers). Extract subtypes from
        // pattern.
        PatternNode *src =
            (i < inferred.size()) ? inferred[i].source_pattern : nullptr;
        auto *tp = src ? dynamic_cast<TuplePattern *>(src) : nullptr;
        if (tp) {
          std::vector<CType> elem_ctypes;
          for (size_t j = 0; j < tp->patterns.size(); j++)
            elem_ctypes.push_back(infer_type_from_pattern(
                static_cast<PatternNode *>(tp->patterns[j])));
          typed_args.push_back({dummy_val, CType::TUPLE, elem_ctypes});
        } else {
          typed_args.push_back({dummy_val, ct});
        }
      } else if (ct == CType::SEQ) {
        auto *ptr_type = PointerType::get(*context_, 0);
        std::vector<CType> elem_ctypes =
            (i < inferred.size()) ? inferred[i].subtypes : std::vector<CType>{};
        typed_args.push_back(
            {ConstantPointerNull::get(ptr_type), CType::SEQ, elem_ctypes});
      } else if (ct == CType::STRING) {
        auto *ptr_type = PointerType::get(*context_, 0);
        typed_args.push_back(
            {ConstantPointerNull::get(ptr_type), CType::STRING});
      } else if (ct == CType::FLOAT) {
        typed_args.push_back(
            {ConstantFP::get(LType::getDoubleTy(*context_), 0.0),
             CType::FLOAT});
      } else if (ct == CType::BOOL) {
        typed_args.push_back(
            {ConstantInt::get(LType::getInt1Ty(*context_), 0), CType::BOOL});
      } else if (ct == CType::SYMBOL) {
        typed_args.push_back(
            {ConstantInt::get(LType::getInt64Ty(*context_), 0), CType::SYMBOL});
      } else if (ct == CType::FUNCTION) {
        auto *ptr_type = PointerType::get(*context_, 0);
        std::vector<CType> subtypes =
            (!annotated_param_subtypes.empty() &&
             i < annotated_param_subtypes.size())
                ? annotated_param_subtypes[i]
                : (i < checked_param_subtypes.size() ? checked_param_subtypes[i]
                                                     : std::vector<CType>{});
        typed_args.push_back(
            {ConstantPointerNull::get(ptr_type), CType::FUNCTION, subtypes});
      } else if (ct == CType::ADT) {
        if (!annotated_param_adt_names.empty() &&
            i < annotated_param_adt_names.size() &&
            !annotated_param_adt_names[i].empty()) {
          typed_args.push_back(
              {ConstantPointerNull::get(PointerType::get(*context_, 0)),
               CType::ADT});
          typed_args.back().adt_type_name = annotated_param_adt_names[i];
          continue;
        }
        // Build the ADT struct type based on the constructor pattern
        // Find which ADT type by looking at the case patterns
        PatternNode *src =
            (i < inferred.size()) ? inferred[i].source_pattern : nullptr;
        // Find constructor name from either ConstructorPattern or RecordPattern
        std::string ctor_name_lookup;
        if (auto *cp = src ? dynamic_cast<ConstructorPattern *>(src) : nullptr)
          ctor_name_lookup = cp->constructor_name;
        else if (auto *rp = src ? dynamic_cast<RecordPattern *>(src) : nullptr)
          ctor_name_lookup = rp->recordType;
        if (!ctor_name_lookup.empty()) {
          auto ctor_it = types_.adt_constructors.find(ctor_name_lookup);
          if (ctor_it != types_.adt_constructors.end()) {
            TypedValue tv;
            tv.type = CType::ADT;
            tv.adt_type_name = ctor_it->second.type_name;
            tv.val = ConstantPointerNull::get(PointerType::get(*context_, 0));
            typed_args.push_back(tv);
          } else {
            typed_args.push_back({dummy_val, ct});
          }
        } else {
          // No constructor pattern â€” inferred from field access or other
          // usage. Field-access inference represents parameters with the heap
          // ABI (i64 pointer value) because exported/imported ADTs cross module
          // boundaries boxed even when local constructors use flat structs.
          bool found = false;
          for (auto &[cname, cinfo] : types_.adt_constructors) {
            bool matches_fields = true;
            const auto &accessed_fields = (i < inferred.size())
                                              ? inferred[i].accessed_fields
                                              : std::vector<std::string>{};
            for (const auto &field : accessed_fields) {
              if (std::find(cinfo.field_names.begin(), cinfo.field_names.end(),
                            field) == cinfo.field_names.end()) {
                matches_fields = false;
                break;
              }
            }
            if (!cinfo.field_names.empty() && matches_fields) {
              TypedValue tv;
              tv.type = CType::ADT;
              tv.adt_type_name = cinfo.type_name;
              tv.val = ConstantPointerNull::get(PointerType::get(*context_, 0));
              typed_args.push_back(tv);
              found = true;
              break;
            }
          }
          if (!found)
            typed_args.push_back({dummy_val, ct});
        }
      } else {
        typed_args.push_back({dummy_val, ct});
      }
    }

    compiling_unhandled_perform_ok_ = true;
    auto cf = compile_function(fn_name, def_it->second, typed_args);
    compiling_unhandled_perform_ok_ = false;

    if (is_exported) {
      // Store type metadata for importers
      std::string mangled = mangle_name(fqn, fn_name);
      imports_.export_identities[mangled] = {fqn, fn_name};
      imports_.module_exports[fqn][fn_name] = mangled;
      imports_.meta[mangled] = module_meta_from_compiled(cf);
      imports_.interface_symbols.insert(mangled);

      // Check if the function already has the right linkage
      if (cf.fn->getName() != mangled) {
        // Create a wrapper function with external linkage and mangled name
        auto *wrapper =
            Function::Create(cf.fn->getFunctionType(),
                             Function::ExternalLinkage, mangled, module_);

        auto *bb = BasicBlock::Create(*context_, "entry", wrapper);
        builder_->SetInsertPoint(bb);

        std::vector<Value *> args;
        for (auto &arg : wrapper->args())
          args.push_back(&arg);
        auto *result = builder_->CreateCall(cf.fn, args);
        builder_->CreateRet(result);
      } else {
        cf.fn->setLinkage(Function::ExternalLinkage);
      }
    }
  }

  // Third pass: compile trait instance methods with ExternalLinkage
  // so importing modules can call them via resolved trait dispatch.
  for (auto &[key, inst] : types_.trait_instances) {
    if (!interface_instance_keys_.count(key))
      continue;
    for (auto &[method_name, mangled] : inst.method_mangled_names) {
      auto cf_it = compiled_functions_.find(mangled);
      if (cf_it != compiled_functions_.end()) {
        if (inst.trait_name == "Array" && !cf_it->second.param_types.empty()) {
          cf_it->second.borrowed_params.resize(cf_it->second.param_types.size(),
                                               false);
          cf_it->second.borrowed_params[0] = true;
        }
        cf_it->second.fn->setLinkage(Function::ExternalLinkage);
        // Emit FN metadata for the .yonai file
        auto meta = module_meta_from_compiled(cf_it->second);
        if (const auto declared = deferred_functions_.find(mangled);
            declared != deferred_functions_.end() &&
            declared->second.ast->type_signature.has_value()) {
          auto [params, result] =
              uncurry_type_signature(*declared->second.ast->type_signature);
          meta.param_types = std::move(params);
          meta.return_type = result;
          meta.param_type_descriptors.clear();
          const types::Type *current = &*declared->second.ast->type_signature;
          while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
              *current)) {
            const auto &function =
                std::get<std::shared_ptr<types::FunctionType>>(*current);
            meta.param_type_descriptors.push_back(
                source_type_descriptor(function->argumentType));
            current = &function->returnType;
          }
          meta.return_type_descriptor = source_type_descriptor(*current);
        }
        imports_.meta[mangled] = std::move(meta);
        imports_.interface_symbols.insert(mangled);
        continue;
      }
      auto def_it = deferred_functions_.find(mangled);
      if (def_it != deferred_functions_.end()) {
        // An annotated Yona instance is a source-level dictionary,
        // not one concrete machine function. Compiling it here with
        // placeholder arguments can erase both instance variables and
        // method-local variables (for example Foldable's accumulator)
        // and produce an invalid recursive ABI. Export its exact
        // contract and GENFN source; selected concrete call sites
        // perform the only valid monomorphization.
        if (def_it->second.ast->type_signature.has_value()) {
          ModuleFunctionMeta meta;
          auto [params, result] =
              uncurry_type_signature(*def_it->second.ast->type_signature);
          meta.param_types = std::move(params);
          meta.return_type = result;
          const types::Type *current = &*def_it->second.ast->type_signature;
          while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
              *current)) {
            const auto &function =
                std::get<std::shared_ptr<types::FunctionType>>(*current);
            meta.param_type_descriptors.push_back(
                source_type_descriptor(function->argumentType));
            current = &function->returnType;
          }
          meta.return_type_descriptor = source_type_descriptor(*current);
          meta.borrowed_params = def_it->second.ast->param_borrow;
          meta.borrowed_params.resize(meta.param_types.size(), false);
          if ((inst.trait_name == "Eq" || inst.trait_name == "Ord") &&
              meta.borrowed_params.size() >= 2) {
            meta.borrowed_params[0] = true;
            meta.borrowed_params[1] = true;
          } else if ((inst.trait_name == "Hash" || inst.trait_name == "Show") &&
                     !meta.borrowed_params.empty()) {
            meta.borrowed_params[0] = true;
          }
          imports_.meta[mangled] = std::move(meta);
          imports_.interface_symbols.insert(mangled);
          continue;
        }
        // Array is an observational contract: indexing and length do
        // not consume the collection. Preserve that law even for
        // instance clauses reparsed from interfaces where an explicit
        // @borrow marker is unavailable.
        if (inst.trait_name == "Array" &&
            !def_it->second.ast->param_borrow.empty())
          def_it->second.ast->param_borrow[0] = true;
        // Compile with inferred types, using correct LLVM types for ADTs
        auto inferred = infer_param_types(def_it->second.ast);
        std::vector<CType> annotated;
        std::vector<bool> annotation_variables;
        if (def_it->second.ast->type_signature.has_value()) {
          annotated =
              uncurry_type_signature(*def_it->second.ast->type_signature).first;
          const types::Type *current = &*def_it->second.ast->type_signature;
          while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
              *current)) {
            const auto &function =
                std::get<std::shared_ptr<types::FunctionType>>(*current);
            const auto *named = std::get_if<std::shared_ptr<types::NamedType>>(
                &function->argumentType);
            annotation_variables.push_back(
                named && *named && !(*named)->name.empty() &&
                std::islower(
                    static_cast<unsigned char>((*named)->name.front())));
            current = &function->returnType;
          }
        }
        std::vector<TypedValue> typed_args;
        for (size_t i = 0; i < def_it->second.param_names.size(); i++) {
          CType ct =
              i < annotated.size() && !(i < annotation_variables.size() &&
                                        annotation_variables[i])
                  ? annotated[i]
              : (i < inferred.size()) ? inferred[i].type
                                      : CType::INT;
          TypedValue tv;
          tv.type = ct;
          if (ct == CType::ADT) {
            // Find the ADT type from the instance or pattern
            PatternNode *src =
                (i < inferred.size()) ? inferred[i].source_pattern : nullptr;
            std::string ctor_name;
            if (auto *cp =
                    src ? dynamic_cast<ConstructorPattern *>(src) : nullptr)
              ctor_name = cp->constructor_name;
            if (!ctor_name.empty()) {
              auto ctor_it = types_.adt_constructors.find(ctor_name);
              if (ctor_it != types_.adt_constructors.end()) {
                tv.adt_type_name = ctor_it->second.type_name;
                tv.val =
                    ConstantPointerNull::get(PointerType::get(*context_, 0));
              } else {
                tv.val =
                    ConstantPointerNull::get(PointerType::get(*context_, 0));
              }
            } else {
              // Try the instance type name
              const std::string InstanceType = inst.type_names.empty()
                                                   ? std::string{}
                                                   : inst.type_names.front();
              tv.adt_type_name = InstanceType;
              // Find any constructor for this type
              for (auto &[cn, ci] : types_.adt_constructors) {
                if (ci.type_name == InstanceType) {
                  tv.val =
                      ConstantPointerNull::get(PointerType::get(*context_, 0));
                  break;
                }
              }
              if (!tv.val)
                tv.val =
                    ConstantPointerNull::get(PointerType::get(*context_, 0));
            }
          } else if (ct == CType::FLOAT) {
            tv.val = ConstantFP::get(LType::getDoubleTy(*context_), 0.0);
          } else if (ct == CType::BOOL) {
            tv.val = ConstantInt::get(LType::getInt1Ty(*context_), 0);
          } else if (ct == CType::STRING || ct == CType::SEQ ||
                     ct == CType::SET || ct == CType::DICT ||
                     ct == CType::FUNCTION || ct == CType::CHANNEL) {
            tv.val = ConstantPointerNull::get(PointerType::get(*context_, 0));
          } else {
            tv.val = ConstantInt::get(LType::getInt64Ty(*context_), 0);
          }
          typed_args.push_back(tv);
        }
        auto cf = compile_function(mangled, def_it->second, typed_args);
        if (cf.fn) {
          if (inst.trait_name == "Array" && !cf.param_types.empty()) {
            cf.borrowed_params.resize(cf.param_types.size(), false);
            cf.borrowed_params[0] = true;
            compiled_functions_[mangled].borrowed_params = cf.borrowed_params;
          }
          cf.fn->setLinkage(Function::ExternalLinkage);
          auto meta = module_meta_from_compiled(cf);
          // Placeholder compilation exists only to emit one native
          // body. Its inferred specialization must never narrow the
          // public generic contract. The source annotation is the
          // authoritative interface type for every Yona instance,
          // constrained or not.
          if (def_it->second.ast->type_signature.has_value()) {
            auto [params, result] =
                uncurry_type_signature(*def_it->second.ast->type_signature);
            meta.param_types = std::move(params);
            meta.return_type = result;
            meta.param_type_descriptors.clear();
            const types::Type *current = &*def_it->second.ast->type_signature;
            while (std::holds_alternative<std::shared_ptr<types::FunctionType>>(
                *current)) {
              const auto &function =
                  std::get<std::shared_ptr<types::FunctionType>>(*current);
              meta.param_type_descriptors.push_back(
                  source_type_descriptor(function->argumentType));
              current = &function->returnType;
            }
            meta.return_type_descriptor = source_type_descriptor(*current);
          }
          imports_.meta[mangled] = std::move(meta);
          imports_.interface_symbols.insert(mangled);
        }
      }
    }
  }

  // Process re-exports: load source module interfaces and create forwarding
  // wrappers
  for (auto &re : mod->re_exports) {
    // Build filesystem path from FQN
    std::filesystem::path mod_path;
    std::string src_fqn = re.source_module;
    for (char &c : mod_path.string()) { /* unused, build below */
    }
    // Convert backslash FQN to filesystem path
    std::string path_str;
    for (char c : src_fqn)
      path_str += (c == '\\') ? '/' : c;
    mod_path = std::filesystem::path(path_str);

    // Load the source module's interface
    load_module_interface(mod_path);

    for (auto &name : re.names) {
      // Check if it's an ADT constructor
      auto ctor_it = types_.adt_constructors.find(name);
      if (ctor_it != types_.adt_constructors.end()) {
        // ADT constructors are re-exported via the interface file (no wrapper
        // needed)
        continue;
      }

      // Function re-export: create a forwarding wrapper
      std::string src_mangled = mangle_name(src_fqn, name);
      std::string dst_mangled = mangle_name(fqn, name);
      imports_.export_identities[dst_mangled] = {fqn, name};
      imports_.module_exports[fqn][name] = dst_mangled;

      // Look up source function metadata
      auto meta_it = imports_.meta.find(src_mangled);
      if (meta_it == imports_.meta.end()) {
        report_error(mod->Range, "re-export: function '" + name +
                                     "' not found in module '" + src_fqn + "'");
        continue;
      }

      auto &meta = meta_it->second;

      // Build function type from metadata
      std::vector<LType *> param_types;
      for (auto ct : meta.param_types)
        param_types.push_back(llvm_type(ct));
      auto *ret_llvm = llvm_type(meta.return_type);
      auto *fn_type = llvm::FunctionType::get(ret_llvm, param_types, false);

      // Declare the source function (external)
      auto *src_fn = module_->getFunction(src_mangled);
      if (!src_fn)
        src_fn = Function::Create(fn_type, Function::ExternalLinkage,
                                  src_mangled, module_);

      // Create forwarding wrapper with this module's mangled name
      auto *wrapper = Function::Create(fn_type, Function::ExternalLinkage,
                                       dst_mangled, module_);
      auto *bb = BasicBlock::Create(*context_, "entry", wrapper);
      builder_->SetInsertPoint(bb);
      std::vector<Value *> args;
      for (auto &arg : wrapper->args())
        args.push_back(&arg);
      auto *result = builder_->CreateCall(src_fn, args);
      builder_->CreateRet(result);

      // Register in imports_.meta so the interface file includes it
      imports_.meta[dst_mangled] = meta;
      imports_.interface_symbols.insert(dst_mangled);
    }
  }

  // Clear builder insert point after module compilation
  builder_->ClearInsertionPoint();

  // A diagnostic-producing lowering may intentionally leave the current
  // function incomplete.  Verification and optimization are meaningful
  // only for a successful module; running LLVM over recovery IR can assert
  // before the CLI gets a chance to return the diagnostic status.
  if (Session->errorCount() > 0 || Session->diagnostics().has_errors())
    return nullptr;

  finalize_debug_info();

  // Verify
  std::string err;
  raw_string_ostream os(err);
  if (verifyModule(*module_, &os)) {
    std::cerr << "Module verification failed:\n" << err << "\n";
    return nullptr;
  }
  optimize();
  err.clear();
  if (verifyModule(*module_, &os)) {
    std::cerr << "Module verification failed after optimization:\n"
              << err << "\n";
    return nullptr;
  }
  return module_;
}

bool Codegen::emit_object_file(const std::string &path) {
  if (!target_machine_)
    return false;
  // Ensure parent directory exists
  auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
  std::error_code ec;
  raw_fd_ostream dest(path, ec, sys::fs::OF_None);
  if (ec)
    return false;
  legacy::PassManager pass;
  if (target_machine_->addPassesToEmitFile(pass, dest, nullptr,
                                           CodeGenFileType::ObjectFile))
    return false;
  pass.run(*module_);
  dest.flush();
  return true;
}

static std::string ctype_to_string(CType ct) {
  switch (ct) {
  case CType::INT:
    return "INT";
  case CType::FLOAT:
    return "FLOAT";
  case CType::BOOL:
    return "BOOL";
  case CType::STRING:
    return "STRING";
  case CType::SEQ:
    return "SEQ";
  case CType::TUPLE:
    return "TUPLE";
  case CType::UNIT:
    return "UNIT";
  case CType::FUNCTION:
    return "FUNCTION";
  case CType::SYMBOL:
    return "SYMBOL";
  case CType::PROMISE:
    return "PROMISE";
  case CType::SET:
    return "SET";
  case CType::DICT:
    return "DICT";
  case CType::ADT:
    return "ADT";
  case CType::BYTE_ARRAY:
    return "BYTE_ARRAY";
  case CType::INT_ARRAY:
    return "INT_ARRAY";
  case CType::FLOAT_ARRAY:
    return "FLOAT_ARRAY";
  case CType::CHANNEL:
    return "CHANNEL";
  case CType::SUM:
    return "SUM";
  case CType::RECORD:
    return "RECORD";
  }
  return "INT";
}

std::string Codegen::emit_ir() {
  std::string ir;
  raw_string_ostream os(ir);
  module_->print(os, nullptr);
  return ir;
}

bool Codegen::link_runtime_bitcode(const std::string &bc_path) {
  if (!module_)
    return false;

  llvm::SMDiagnostic err;
  auto rt_module = llvm::parseIRFile(bc_path, err, *context_);
  if (!rt_module)
    return false;

  // Link the runtime module into our module.
  // OverrideFromSrc: if both modules define a function, keep the
  // runtime's definition (it has the body, ours has just a declaration).
  return !llvm::Linker::linkModules(*module_, std::move(rt_module),
                                    llvm::Linker::OverrideFromSrc);
}

void Codegen::apply_fastcc() {
  if (!module_)
    return;

  for (auto &fn : *module_) {
    if (fn.isDeclaration())
      continue;
    if (fn.getLinkage() != Function::InternalLinkage)
      continue;
    if (fn.hasAddressTaken())
      continue;

    // fastcc for internal functions not used as HOF values
    fn.setCallingConv(llvm::CallingConv::Fast);
    for (auto *user : fn.users()) {
      if (auto *call = dyn_cast<CallInst>(user))
        call->setCallingConv(llvm::CallingConv::Fast);
    }

    // Inlining hints for small functions (â‰¤20 basic blocks).
    // Helps LLVM inline recursive functions more aggressively.
    if (fn.size() <= 20)
      fn.addFnAttr(llvm::Attribute::InlineHint);
  }
}

void Codegen::optimize() {
  if (!module_)
    return;

  // Apply fastcc before optimization so LLVM can exploit it
  apply_fastcc();

  // Use the new PassManager for all optimization levels
  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  // Select optimization level
  llvm::OptimizationLevel level;
  switch (opt_level_) {
  case 0:
    level = llvm::OptimizationLevel::O0;
    break;
  case 1:
    level = llvm::OptimizationLevel::O1;
    break;
  case 3:
    level = llvm::OptimizationLevel::O3;
    break;
  default:
    level = llvm::OptimizationLevel::O2;
    break;
  }

  llvm::ModulePassManager MPM;
  if (opt_level_ == 0) {
    // O0: only run AlwaysInliner for marked functions
    MPM = PB.buildO0DefaultPipeline(level);
  } else {
    // O1-O3: full pipeline including:
    // - Function inlining (cost-based at O2+)
    // - SROA (scalar replacement of aggregates â€” decomposes structs to
    // registers)
    // - Loop optimizations (LICM, unrolling at O2+, vectorization at O3)
    // - Dead argument elimination
    // - Tail call elimination
    // - GVN, instcombine, CFG simplification, mem2reg
    MPM = PB.buildPerModuleDefaultPipeline(level);
  }

  MPM.run(*module_, MAM);
}

// ===== Entry Point =====

Function *Codegen::codegen_main(AstNode *node) {
  auto i32 = LType::getInt32Ty(*context_);
  auto ptr = PointerType::get(*context_, 0);
  auto fn = Function::Create(llvm::FunctionType::get(i32, {i32, ptr}, false),
                             Function::ExternalLinkage, "main", module_);
  // Create debug info for main function
  if (debug_.enabled && debug_.builder && debug_.file) {
    auto *di_func_ty = debug_.builder->createSubroutineType(
        debug_.builder->getOrCreateTypeArray({debug_.builder->createBasicType(
            "Int", 32, dwarf::DW_ATE_signed)}));
    auto *di_sp = debug_.builder->createFunction(
        debug_.file, "main", "main", debug_.file, node->Range.Line, di_func_ty,
        node->Range.Line, DINode::FlagZero, DISubprogram::SPFlagDefinition);
    fn->setSubprogram(di_sp);
    debug_.scope = di_sp;
  }
  auto bb = BasicBlock::Create(*context_, "entry", fn);
  builder_->SetInsertPoint(bb);
  set_debug_loc(node->Range);
  builder_->CreateCall(rt_.set_process_args_, {fn->getArg(0), fn->getArg(1)});

  auto result = codegen(node);
  // Don't add print/ret if the block is already terminated (e.g., by raise)
  if (!current_block_terminated()) {
    if (result)
      codegen_print(result);
    builder_->CreateRet(ConstantInt::get(i32, 0));
  }
  return fn;
}

// ===== Print (type-directed) =====

void Codegen::codegen_print_value(const TypedValue &tv) {
  if (!tv.val)
    return;
  Value *v = tv.val;
  switch (tv.type) {
  case CType::INT:
    builder_->CreateCall(rt_.print_int_, {v});
    break;
  case CType::FLOAT:
    builder_->CreateCall(rt_.print_float_, {v});
    break;
  case CType::BOOL:
    builder_->CreateCall(rt_.print_bool_, {v});
    break;
  case CType::STRING:
    builder_->CreateCall(rt_.print_string_, {v});
    break;
  case CType::SEQ:
    builder_->CreateCall(rt_.print_seq_, {v});
    break;
  case CType::TUPLE: {
    builder_->CreateCall(rt_.print_string_,
                         {builder_->CreateGlobalString("(")});
    if (!tv.subtypes.empty()) {
      // Boxed tuple (i64 = ptrtoint'd ptr to i64 array): GEP + load
      auto i64_ty_local = LType::getInt64Ty(*context_);
      Value *tuple_ptr = tv.val;
      if (tuple_ptr->getType()->isIntegerTy())
        tuple_ptr =
            builder_->CreateIntToPtr(tuple_ptr, PointerType::get(*context_, 0));
      for (unsigned i = 0; i < tv.subtypes.size(); i++) {
        if (i > 0)
          builder_->CreateCall(rt_.print_string_,
                               {builder_->CreateGlobalString(", ")});
        auto *gep = builder_->CreateGEP(
            i64_ty_local, tuple_ptr,
            {ConstantInt::get(i64_ty_local, i + 2)}); // +2 for tuple header
        auto *elem = builder_->CreateLoad(i64_ty_local, gep);
        CType et = tv.subtypes[i];
        // Tuple slots are always i64; restore the LLVM type print
        // helpers expect (i1 for Bool, ptr for String, f64, â€¦).
        Value *typed = elem;
        LType *want = llvm_type(et);
        if (typed->getType() != want) {
          if (want->isPointerTy())
            typed = builder_->CreateIntToPtr(typed, want);
          else if (want->isDoubleTy())
            typed = builder_->CreateBitCast(typed, want);
          else if (want->isIntegerTy() && want->getIntegerBitWidth() < 64)
            typed = builder_->CreateTrunc(typed, want);
        }
        TypedValue element{typed, et};
        if (i < tv.semantic_subtypes.size()) {
          const auto &identity = tv.semantic_subtypes[i];
          element.adt_type_name = identity.adt_name;
          element.semantic_subtypes = identity.arguments;
          element.adt_semantic_arguments = identity.arguments;
          for (const auto &argument : identity.arguments) {
            element.subtypes.push_back(argument.type);
            element.adt_type_arguments.push_back(argument.type);
            element.adt_type_argument_names.push_back(argument.adt_name);
          }
        }
        codegen_print_value(element);
      }
    }
    builder_->CreateCall(rt_.print_string_,
                         {builder_->CreateGlobalString(")")});
    break;
  }
  case CType::SET:
    builder_->CreateCall(rt_.print_set_, {tv.val});
    break;
  case CType::DICT:
    builder_->CreateCall(rt_.print_dict_, {tv.val});
    break;
  case CType::UNIT:
    builder_->CreateCall(rt_.print_string_,
                         {builder_->CreateGlobalString("()")});
    break;
  case CType::FUNCTION:
    builder_->CreateCall(rt_.print_string_,
                         {builder_->CreateGlobalString("<function>")});
    break;
  case CType::SYMBOL: {
    // Symbol is an interned i64 ID. Look up the string for printing.
    if (auto *ci = dyn_cast<ConstantInt>(tv.val)) {
      int64_t id = ci->getSExtValue();
      if (id >= 0 && id < (int64_t)symbols_.strings.size()) {
        builder_->CreateCall(rt_.print_symbol_, {symbols_.strings[id]});
      }
    } else {
      // Runtime symbol value â€” need a table lookup.
      // Emit a GEP into the symbol names table (emitted at finalization).
      // For now, emit a placeholder.
      builder_->CreateCall(rt_.print_string_,
                           {builder_->CreateGlobalString(":<dynamic>")});
    }
    break;
  }
  case CType::ADT: {
    builder_->CreateCall(rt_.print_string_,
                         {builder_->CreateGlobalString("<adt>")});
    break;
  }
  case CType::BYTE_ARRAY: {
    builder_->CreateCall(rt_.print_byte_array_, {tv.val});
    break;
  }
  case CType::INT_ARRAY: {
    builder_->CreateCall(rt_.print_int_array_, {tv.val});
    break;
  }
  case CType::FLOAT_ARRAY: {
    builder_->CreateCall(rt_.print_float_array_, {tv.val});
    break;
  }
  default:
    break;
  }
}

void Codegen::codegen_print(const TypedValue &tv) {
  auto resolved = auto_await(tv);
  codegen_print_value(resolved);
  builder_->CreateCall(rt_.print_newline_, {});
}

// ===== Core Dispatch =====

TypedValue Codegen::codegen(AstNode *node) {
  if (!node)
    return {};
  switch (node->get_type()) {
  case ast::AST_MAIN:
    return codegen_main_node(static_cast<MainNode *>(node));
  case ast::AST_INTEGER_EXPR:
    return codegen_integer(static_cast<IntegerExpr *>(node));
  case ast::AST_FLOAT_EXPR:
    return codegen_float(static_cast<FloatExpr *>(node));
  case ast::AST_TRUE_LITERAL_EXPR:
    return codegen_bool_true(static_cast<TrueLiteralExpr *>(node));
  case ast::AST_FALSE_LITERAL_EXPR:
    return codegen_bool_false(static_cast<FalseLiteralExpr *>(node));
  case ast::AST_STRING_EXPR:
    return codegen_string(static_cast<StringExpr *>(node));
  case ast::AST_UNIT_EXPR:
    return codegen_unit(static_cast<UnitExpr *>(node));
  case ast::AST_SYMBOL_EXPR:
    return codegen_symbol(static_cast<SymbolExpr *>(node));
  case ast::AST_ADD_EXPR:
    return codegen_binary(static_cast<AddExpr *>(node)->left,
                          static_cast<AddExpr *>(node)->right, "+");
  case ast::AST_SUBTRACT_EXPR:
    return codegen_binary(static_cast<SubtractExpr *>(node)->left,
                          static_cast<SubtractExpr *>(node)->right, "-");
  case ast::AST_MULTIPLY_EXPR:
    return codegen_binary(static_cast<MultiplyExpr *>(node)->left,
                          static_cast<MultiplyExpr *>(node)->right, "*");
  case ast::AST_DIVIDE_EXPR:
    return codegen_binary(static_cast<DivideExpr *>(node)->left,
                          static_cast<DivideExpr *>(node)->right, "/");
  case ast::AST_MODULO_EXPR:
    return codegen_binary(static_cast<ModuloExpr *>(node)->left,
                          static_cast<ModuloExpr *>(node)->right, "%");
  case ast::AST_EQ_EXPR:
    return codegen_comparison(static_cast<EqExpr *>(node)->left,
                              static_cast<EqExpr *>(node)->right, "==");
  case ast::AST_NEQ_EXPR:
    return codegen_comparison(static_cast<NeqExpr *>(node)->left,
                              static_cast<NeqExpr *>(node)->right, "!=");
  case ast::AST_LT_EXPR:
    return codegen_comparison(static_cast<LtExpr *>(node)->left,
                              static_cast<LtExpr *>(node)->right, "<");
  case ast::AST_GT_EXPR:
    return codegen_comparison(static_cast<GtExpr *>(node)->left,
                              static_cast<GtExpr *>(node)->right, ">");
  case ast::AST_LTE_EXPR:
    return codegen_comparison(static_cast<LteExpr *>(node)->left,
                              static_cast<LteExpr *>(node)->right, "<=");
  case ast::AST_GTE_EXPR:
    return codegen_comparison(static_cast<GteExpr *>(node)->left,
                              static_cast<GteExpr *>(node)->right, ">=");
  case ast::AST_LOGICAL_AND_EXPR: {
    auto l = codegen(static_cast<LogicalAndExpr *>(node)->left);
    auto r = codegen(static_cast<LogicalAndExpr *>(node)->right);
    if (!l || !r)
      return {};
    auto normalize = [&](Value *value) -> Value * {
      if (value->getType()->isIntegerTy(1))
        return value;
      if (value->getType()->isIntegerTy())
        return builder_->CreateICmpNE(
            value, ConstantInt::get(value->getType(), 0), "logical_bool");
      return value;
    };
    return {builder_->CreateAnd(normalize(l.val), normalize(r.val)),
            CType::BOOL};
  }
  case ast::AST_LOGICAL_OR_EXPR: {
    auto l = codegen(static_cast<LogicalOrExpr *>(node)->left);
    auto r = codegen(static_cast<LogicalOrExpr *>(node)->right);
    if (!l || !r)
      return {};
    auto normalize = [&](Value *value) -> Value * {
      if (value->getType()->isIntegerTy(1))
        return value;
      if (value->getType()->isIntegerTy())
        return builder_->CreateICmpNE(
            value, ConstantInt::get(value->getType(), 0), "logical_bool");
      return value;
    };
    return {builder_->CreateOr(normalize(l.val), normalize(r.val)),
            CType::BOOL};
  }
  case ast::AST_LOGICAL_NOT_OP_EXPR: {
    auto v = codegen(static_cast<LogicalNotOpExpr *>(node)->expr);
    if (!v)
      return {};
    Value *value = v.val;
    if (!value->getType()->isIntegerTy(1) && value->getType()->isIntegerTy())
      value = builder_->CreateICmpNE(
          value, ConstantInt::get(value->getType(), 0), "logical_bool");
    return {builder_->CreateNot(value), CType::BOOL};
  }
  case ast::AST_PIPE_RIGHT_EXPR: {
    // x |> f          â†’  f x
    // x |> f a b      â†’  f a b x   (append lhs as the last argument
    //                                of the rhs apply chain)
    //
    // We don't construct a synthetic ApplyExpr â€” its destructor
    // would delete the borrowed children. Instead, evaluate all
    // arguments (rhs's existing args plus pe->left) directly and
    // dispatch through resolve_apply_function + emit_direct_call,
    // matching codegen_apply's flow.
    auto *pe = static_cast<PipeRightExpr *>(node);

    // Determine the root function name. For `x |> f`, that's `f`.
    // For `x |> f a b`, walk the rhs's call chain to its root.
    std::string fn_name;
    std::vector<std::variant<ExprNode *, ValueExpr *>> rhs_args;
    if (pe->right->get_type() == ast::AST_IDENTIFIER_EXPR) {
      fn_name = static_cast<IdentifierExpr *>(pe->right)->name->value;
    } else if (pe->right->get_type() == ast::AST_APPLY_EXPR) {
      // Walk inner ExprCall chain to find the innermost NameCall
      // and gather args in surface order (innermost first, then
      // outer). This mirrors codegen_apply::flatten_apply_chain.
      std::vector<ApplyExpr *> chain;
      ApplyExpr *cur = static_cast<ApplyExpr *>(pe->right);
      while (cur) {
        chain.push_back(cur);
        if (auto *nc = dynamic_cast<NameCall *>(cur->call)) {
          fn_name = nc->name->value;
          break;
        } else if (auto *ec = dynamic_cast<ExprCall *>(cur->call)) {
          if (auto *inner = dynamic_cast<ApplyExpr *>(ec->expr)) {
            cur = inner;
          } else
            break;
        } else
          break;
      }
      for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        for (auto &a : (*it)->args)
          rhs_args.push_back(a);
      }
    }

    if (!fn_name.empty()) {
      // Evaluate rhs args (allowing nullptr val for deferred
      // FUNCTION args, which precompile_function_args / wrap step
      // resolves), then pe->left as the final argument.
      EvaluatedArgs eval;
      for (auto &a : rhs_args) {
        last_lambda_name_.clear();
        TypedValue tv;
        if (std::holds_alternative<ExprNode *>(a))
          tv = codegen(std::get<ExprNode *>(a));
        else
          tv = codegen(std::get<ValueExpr *>(a));
        if (tv.type != CType::FUNCTION && !tv)
          return {};
        if (tv.type == CType::PROMISE)
          tv = auto_await(tv);
        eval.all_args.push_back(tv);
        eval.arg_lambda_names.push_back(last_lambda_name_);
      }
      {
        last_lambda_name_.clear();
        auto tv = codegen(pe->left);
        if (tv.type != CType::FUNCTION && !tv)
          return {};
        if (tv.type == CType::PROMISE)
          tv = auto_await(tv);
        eval.all_args.push_back(tv);
        eval.arg_lambda_names.push_back(last_lambda_name_);
      }
      precompile_function_args(eval, fn_name);
      wrap_function_args_in_closures(eval.all_args);

      auto &all_args = eval.all_args;

      // ADT constructor as the pipe target â€” `[1,2,3] |> Some`.
      auto adt_it = types_.adt_constructors.find(fn_name);
      if (adt_it != types_.adt_constructors.end() && adt_it->second.arity > 0)
        return codegen_adt_construct(fn_name, all_args);

      auto cf_it = resolve_apply_function(fn_name, all_args);
      if (cf_it != compiled_functions_.end()) {
        auto &cf = cf_it->second;
        size_t func_arity = cf.param_types.size() - cf.capture_names.size();
        if (all_args.size() < func_arity)
          return codegen_partial_apply(fn_name, cf, all_args);
        if (all_args.size() > func_arity)
          return codegen_curry_apply(fn_name, cf, all_args);
        return emit_direct_call(fn_name, cf, all_args);
      }

      auto ext_it = imports_.extern_functions.find(fn_name);
      if (ext_it != imports_.extern_functions.end())
        return codegen_extern_call(nullptr, fn_name, all_args);

      // Higher-order call via named_values_
      auto var_it = named_values_.find(fn_name);
      if (var_it != named_values_.end() &&
          var_it->second.type == CType::FUNCTION && var_it->second.val)
        return codegen_higher_order_call(fn_name, all_args);
    }

    // rhs is some other expression (lambda literal, parens, etc.)
    // â€” codegen it as a value, then call it via indirect call.
    auto arg = codegen(pe->left);
    auto fn = codegen(pe->right);
    if (!arg || !fn)
      return {};
    if (fn.type != CType::FUNCTION) {
      report_error(pe->Range, "pipe: right side must be a function");
      return {};
    }
    CType ret_ct = !fn.subtypes.empty() ? fn.subtypes[0] : CType::INT;
    auto *ret_llvm = llvm_type(ret_ct);
    if (fn.val->getType()->isPointerTy() &&
        !llvm::isa<llvm::Function>(fn.val)) {
      auto i64_ty = llvm::Type::getInt64Ty(*context_);
      auto ptr_ty = llvm::PointerType::get(*context_, 0);
      auto *fn_i64 =
          builder_->CreateLoad(i64_ty, fn.val, "pipe_closure_fn_i64");
      auto *fn_ptr =
          builder_->CreateIntToPtr(fn_i64, ptr_ty, "pipe_closure_fn");
      auto *fn_type =
          llvm::FunctionType::get(i64_ty, {ptr_ty, arg.val->getType()}, false);
      Value *raw =
          builder_->CreateCall(fn_type, fn_ptr, {fn.val, arg.val}, "pipe_call");
      if (ret_llvm->isPointerTy())
        raw = builder_->CreateIntToPtr(raw, ret_llvm);
      else if (ret_llvm->isIntegerTy() && raw->getType() != ret_llvm)
        raw = builder_->CreateZExtOrTrunc(raw, ret_llvm);
      return {raw, ret_ct};
    }
    auto *fn_type =
        llvm::FunctionType::get(ret_llvm, {arg.val->getType()}, false);
    return {builder_->CreateCall(fn_type, fn.val, {arg.val}, "pipe_call"),
            ret_ct};
  }
  case ast::AST_PIPE_LEFT_EXPR: {
    // f <| x  â†’  f(x) â€” same logic, swapped sides
    auto *pe = static_cast<PipeLeftExpr *>(node);
    auto arg = codegen(pe->right);
    if (!arg)
      return {};
    std::string fn_name;
    if (pe->left->get_type() == ast::AST_IDENTIFIER_EXPR)
      fn_name = static_cast<IdentifierExpr *>(pe->left)->name->value;
    if (!fn_name.empty()) {
      std::vector<TypedValue> all_args = {arg};
      auto cf_it = compiled_functions_.find(fn_name);
      if (cf_it == compiled_functions_.end()) {
        auto def_it = deferred_functions_.find(fn_name);
        if (def_it != deferred_functions_.end()) {
          compile_function(fn_name, def_it->second, all_args);
          cf_it = compiled_functions_.find(fn_name);
        }
      }
      if (cf_it != compiled_functions_.end())
        return {builder_->CreateCall(cf_it->second.fn, {arg.val}),
                cf_it->second.return_type};
      auto ext_it = imports_.extern_functions.find(fn_name);
      if (ext_it != imports_.extern_functions.end()) {
        auto *ext_fn = module_->getFunction(ext_it->second);
        if (!ext_fn) {
          auto fn_type = llvm::FunctionType::get(arg.val->getType(),
                                                 {arg.val->getType()}, false);
          ext_fn = Function::Create(fn_type, Function::ExternalLinkage,
                                    ext_it->second, module_);
        }
        return {builder_->CreateCall(ext_fn, {arg.val}), CType::INT};
      }
      auto nv_it = named_values_.find(fn_name);
      if (nv_it != named_values_.end() &&
          nv_it->second.type == CType::FUNCTION && nv_it->second.val) {
        auto fn_type = llvm::FunctionType::get(arg.val->getType(),
                                               {arg.val->getType()}, false);
        return {builder_->CreateCall(fn_type, nv_it->second.val, {arg.val}),
                arg.type};
      }
    }
    report_error(pe->Range, "pipe: left side must be a function");
    return {};
  }
  case ast::AST_LET_EXPR:
    return codegen_let(static_cast<LetExpr *>(node));
  case ast::AST_IF_EXPR:
    return codegen_if(static_cast<IfExpr *>(node));
  case ast::AST_CASE_EXPR:
    return codegen_case(static_cast<CaseExpr *>(node));
  case ast::AST_DO_EXPR:
    return codegen_do(static_cast<DoExpr *>(node));
  case ast::AST_RAISE_EXPR:
    return codegen_raise(static_cast<RaiseExpr *>(node));
  case ast::AST_TRY_CATCH_EXPR:
    return codegen_try_catch(static_cast<TryCatchExpr *>(node));
  case ast::AST_WITH_EXPR:
    return codegen_with(static_cast<WithExpr *>(node));
  case ast::AST_PERFORM_EXPR:
    return codegen_perform(static_cast<PerformExpr *>(node));
  case ast::AST_HANDLE_EXPR:
    return codegen_handle(static_cast<HandleExpr *>(node));
  case ast::AST_IDENTIFIER_EXPR:
    return codegen_identifier(static_cast<IdentifierExpr *>(node));
  case ast::AST_FUNCTION_EXPR:
    return codegen_function_def(static_cast<FunctionExpr *>(node), "");
  case ast::AST_APPLY_EXPR:
    return codegen_apply(static_cast<ApplyExpr *>(node));
  case ast::AST_LAMBDA_ALIAS:
    return codegen_lambda_alias(static_cast<LambdaAlias *>(node));
  case ast::AST_IMPORT_EXPR:
    return codegen_import(static_cast<ImportExpr *>(node));
  case ast::AST_EXTERN_DECL:
    return codegen_extern_decl(static_cast<ExternDeclExpr *>(node));
  case ast::AST_FIELD_UPDATE_EXPR: {
    auto *fu = static_cast<FieldUpdateExpr *>(node);
    auto obj = codegen(fu->identifier);
    if (!obj || obj.type != CType::ADT) {
      report_error(fu->Range, "field update requires ADT value");
      return {};
    }
    // Find the constructor with the matching field names
    for (auto &[ctor_name, info] : types_.adt_constructors) {
      if (info.field_names.empty())
        continue;
      // Copy the struct, replace updated fields
      Value *result = obj.val;
      for (auto &[name_expr, val_expr] : fu->updates) {
        auto new_val = codegen(val_expr);
        if (!new_val)
          return {};
        for (size_t fi = 0; fi < info.field_names.size(); fi++) {
          if (info.field_names[fi] == name_expr->value) {
            if (info.is_recursive) {
              // Heap ADT: create new node, copy all fields, replace one
              // For simplicity, not supported yet for recursive types
              report_error(fu->Range,
                           "field update on recursive ADT not supported");
              return {};
            }
            Value *store_val = new_val.val;
            if (store_val->getType() != LType::getInt64Ty(*context_)) {
              if (store_val->getType()->isPointerTy())
                store_val = builder_->CreatePtrToInt(
                    store_val, LType::getInt64Ty(*context_));
            }
            result = builder_->CreateInsertValue(result, store_val,
                                                 {(unsigned)(fi + 1)});
            break;
          }
        }
      }
      return {result, CType::ADT};
    }
    report_error(fu->Range, "no ADT constructor found for field update");
    return {};
  }
  case ast::AST_FIELD_ACCESS_EXPR: {
    auto *fa = static_cast<FieldAccessExpr *>(node);
    auto obj = codegen(fa->identifier);
    if (!obj)
      return {};
    std::string field_name = fa->name->value;
    if (obj.type == CType::ADT) {
      for (auto &[ctor_name, info] : types_.adt_constructors) {
        if (!obj.adt_type_name.empty() && info.type_name != obj.adt_type_name)
          continue;
        for (size_t fi = 0; fi < info.field_names.size(); fi++) {
          if (info.field_names[fi] == field_name) {
            CType ftype = (fi < info.field_types.size()) ? info.field_types[fi]
                                                         : CType::INT;
            bool use_heap_layout =
                info.is_recursive ||
                (obj.val && obj.val->getType()->isPointerTy()) ||
                (obj.val && obj.val->getType()->isIntegerTy());
            if (use_heap_layout) {
              Value *obj_ptr = obj.val;
              if (obj_ptr->getType()->isIntegerTy())
                obj_ptr = builder_->CreateIntToPtr(
                    obj_ptr, PointerType::get(*context_, 0));
              auto val = builder_->CreateCall(
                  rt_.adt_get_field_,
                  {obj_ptr,
                   ConstantInt::get(LType::getInt64Ty(*context_), fi)});
              if (ftype == CType::STRING || ftype == CType::SEQ)
                return {builder_->CreateIntToPtr(
                            val, PointerType::get(*context_, 0)),
                        ftype};
              return {val, ftype};
            } else {
              auto val =
                  builder_->CreateExtractValue(obj.val, {(unsigned)(fi + 1)});
              // Cast i64 to ptr if field type is pointer-based
              if (ftype == CType::STRING || ftype == CType::SEQ ||
                  ftype == CType::SET || ftype == CType::DICT ||
                  ftype == CType::FUNCTION) {
                val = builder_->CreateIntToPtr(val,
                                               PointerType::get(*context_, 0));
              }
              return {val, ftype};
            }
          }
        }
      }
    }
    // Record field access: look up field index from the record's field map
    if (obj.type == CType::RECORD && !obj.record_fields.empty()) {
      for (size_t fi = 0; fi < obj.record_fields.size(); fi++) {
        if (obj.record_fields[fi] == field_name) {
          Value *tuple_ptr = obj.val;
          if (tuple_ptr->getType()->isIntegerTy())
            tuple_ptr = builder_->CreateIntToPtr(
                tuple_ptr, PointerType::get(*context_, 0));
          auto *gep = builder_->CreateGEP(
              LType::getInt64Ty(*context_), tuple_ptr,
              {ConstantInt::get(LType::getInt64Ty(*context_), fi + 2)},
              "rec_field_gep");
          auto *val = builder_->CreateLoad(LType::getInt64Ty(*context_), gep,
                                           "rec_field");
          CType ftype =
              (fi < obj.subtypes.size()) ? obj.subtypes[fi] : CType::INT;
          if (ftype == CType::STRING || ftype == CType::FUNCTION ||
              ftype == CType::BYTE_ARRAY || ftype == CType::PROMISE)
            return {
                builder_->CreateIntToPtr(val, PointerType::get(*context_, 0)),
                ftype};
          if (ftype == CType::SEQ || ftype == CType::SET ||
              ftype == CType::DICT)
            return {
                builder_->CreateIntToPtr(val, PointerType::get(*context_, 0)),
                ftype};
          if (ftype == CType::FLOAT)
            return {builder_->CreateBitCast(val, LType::getDoubleTy(*context_)),
                    ftype};
          if (ftype == CType::BOOL)
            return {builder_->CreateTrunc(val, LType::getInt1Ty(*context_)),
                    ftype};
          return {val, ftype};
        }
      }
    }
    report_error(fa->Range, "unknown field '" + field_name + "'");
    return {};
  }
  case ast::AST_RECORD_LITERAL_EXPR: {
    auto *rec = static_cast<RecordLiteralExpr *>(node);
    set_debug_loc(rec->Range);
    auto i64_ty = LType::getInt64Ty(*context_);

    // Compile each field value
    std::vector<Value *> elems;
    std::vector<CType> field_ctypes;
    std::vector<std::string> field_names;
    for (auto &[name, expr] : rec->fields) {
      auto tv = codegen(expr);
      if (!tv)
        return {};
      Value *i64_val = tv.val;
      if (i64_val->getType()->isPointerTy())
        i64_val = builder_->CreatePtrToInt(i64_val, i64_ty);
      else if (i64_val->getType()->isDoubleTy())
        i64_val = builder_->CreateBitCast(i64_val, i64_ty);
      else if (i64_val->getType()->isIntegerTy() &&
               i64_val->getType() != i64_ty)
        i64_val = builder_->CreateZExtOrTrunc(i64_val, i64_ty);
      elems.push_back(i64_val);
      field_ctypes.push_back(tv.type);
      field_names.push_back(name);
    }

    // Allocate as tuple
    auto *tuple_ptr = builder_->CreateCall(
        rt_.tuple_alloc_, {ConstantInt::get(i64_ty, elems.size())}, "record");
    int64_t heap_mask = 0;
    for (size_t i = 0; i < elems.size(); i++) {
      builder_->CreateCall(rt_.tuple_set_,
                           {tuple_ptr, ConstantInt::get(i64_ty, i), elems[i]});
      // Only set heap_mask for non-constant heap values (constants aren't
      // RC-managed)
      if (is_heap_type(field_ctypes[i]) && i < 64 &&
          !llvm::isa<llvm::Constant>(elems[i]))
        heap_mask |= ((int64_t)1 << i);
    }
    if (heap_mask != 0)
      builder_->CreateCall(rt_.tuple_set_heap_mask_,
                           {tuple_ptr, ConstantInt::get(i64_ty, heap_mask)});
    auto *rec_i64 = builder_->CreatePtrToInt(tuple_ptr, i64_ty, "record_i64");

    TypedValue result = {rec_i64, CType::RECORD, field_ctypes};
    result.record_fields = field_names;
    return result;
  }
  case ast::AST_TUPLE_EXPR:
    return codegen_tuple(static_cast<TupleExpr *>(node));
  case ast::AST_VALUES_SEQUENCE_EXPR:
    return codegen_seq(static_cast<ValuesSequenceExpr *>(node));
  case ast::AST_SET_EXPR:
    return codegen_set(static_cast<SetExpr *>(node));
  case ast::AST_DICT_EXPR:
    return codegen_dict(static_cast<DictExpr *>(node));
  case ast::AST_CONS_LEFT_EXPR:
    return codegen_cons(static_cast<ConsLeftExpr *>(node));
  case ast::AST_CONS_RIGHT_EXPR:
    return codegen_cons_right(static_cast<ConsRightExpr *>(node));
  case ast::AST_JOIN_EXPR:
    return codegen_join(static_cast<JoinExpr *>(node));
  case ast::AST_IN_EXPR:
    return codegen_in(static_cast<InExpr *>(node));
  case ast::AST_REMOVE_EXPR:
    return codegen_remove(static_cast<RemoveExpr *>(node));
  case ast::AST_SEQ_GENERATOR_EXPR:
    return codegen_seq_generator(static_cast<SeqGeneratorExpr *>(node));
  case ast::AST_SET_GENERATOR_EXPR:
    return codegen_set_generator(static_cast<SetGeneratorExpr *>(node));
  case ast::AST_DICT_GENERATOR_EXPR:
    return codegen_dict_generator(static_cast<DictGeneratorExpr *>(node));
  case ast::AST_ADT_DECL:
    return {}; // handled at module level
  case ast::AST_ADT_CONSTRUCTOR:
    return {};
  case ast::AST_CONSTRUCTOR_PATTERN:
    return {};
  default:
    report_error(node->Range, "unsupported expression type");
    return {};
  }
}

// ===== Symbol interning =====

int64_t Codegen::intern_symbol(const std::string &name) {
  auto it = symbols_.ids.find(name);
  if (it != symbols_.ids.end())
    return it->second;
  int64_t id = static_cast<int64_t>(symbols_.strings.size());
  symbols_.ids[name] = id;
  symbols_.strings.push_back(builder_->CreateGlobalString(name, "sym." + name));
  return id;
}

// ===== CFFI =====

void Codegen::register_cffi_signatures() {
  // TODO(toolchain): Register known C library function signatures.
}

bool Codegen::is_cffi_import(const std::string &mod_fqn) {
  return mod_fqn.size() >= 2 && mod_fqn[0] == 'C' && mod_fqn[1] == '\\';
}

// ===== Type annotation helpers (local to this TU for compile_module) =====

static CType yona_type_to_ctype(const types::Type &t) {
  if (std::holds_alternative<types::BuiltinType>(t)) {
    switch (std::get<types::BuiltinType>(t)) {
    case types::SignedInt64:
    case types::SignedInt32:
    case types::SignedInt16:
    case types::SignedInt128:
    case types::UnsignedInt64:
    case types::UnsignedInt32:
    case types::UnsignedInt16:
    case types::UnsignedInt128:
      return CType::INT;
    case types::Float64:
    case types::Float32:
    case types::Float128:
      return CType::FLOAT;
    case types::Bool:
      return CType::BOOL;
    case types::String:
      return CType::STRING;
    case types::Symbol:
      return CType::SYMBOL;
    case types::Unit:
      return CType::UNIT;
    case types::Seq:
      return CType::SEQ;
    case types::Set:
      return CType::SET;
    case types::Dict:
      return CType::DICT;
    default:
      return CType::INT;
    }
  }
  if (std::holds_alternative<std::shared_ptr<types::FunctionType>>(t))
    return CType::FUNCTION;
  if (std::holds_alternative<std::shared_ptr<types::SingleItemCollectionType>>(
          t)) {
    auto &col = std::get<std::shared_ptr<types::SingleItemCollectionType>>(t);
    return (col->kind == types::SingleItemCollectionType::Seq) ? CType::SEQ
                                                               : CType::SET;
  }
  if (std::holds_alternative<std::shared_ptr<types::DictCollectionType>>(t))
    return CType::DICT;
  if (std::holds_alternative<std::shared_ptr<types::ProductType>>(t))
    return CType::TUPLE;
  if (std::holds_alternative<std::shared_ptr<types::NamedType>>(t)) {
    auto &nt = std::get<std::shared_ptr<types::NamedType>>(t);
    if (nt->name == "Channel")
      return CType::CHANNEL;
    if (nt->name == "FloatArray")
      return CType::FLOAT_ARRAY;
    if (nt->name == "IntArray")
      return CType::INT_ARRAY;
    if (nt->name == "ByteArray")
      return CType::BYTE_ARRAY;
    return CType::ADT;
  }
  if (std::holds_alternative<std::shared_ptr<types::PromiseType>>(t))
    return CType::PROMISE;
  if (std::holds_alternative<std::shared_ptr<types::SumType>>(t))
    return CType::SUM;
  if (std::holds_alternative<std::shared_ptr<types::RefinedType>>(t))
    return yona_type_to_ctype(
        std::get<std::shared_ptr<types::RefinedType>>(t)->base_type);
  return CType::INT;
}

// Decompose a curried function type (Int -> Int -> Int) into param types +
// return type
static std::pair<std::vector<CType>, CType>
uncurry_type_signature(const types::Type &t) {
  std::vector<CType> params;
  const types::Type *current = &t;
  while (
      std::holds_alternative<std::shared_ptr<types::FunctionType>>(*current)) {
    auto &ft = std::get<std::shared_ptr<types::FunctionType>>(*current);
    params.push_back(yona_type_to_ctype(ft->argumentType));
    current = &ft->returnType;
  }
  return {params, yona_type_to_ctype(*current)};
}

} // namespace yona::compiler::codegen
