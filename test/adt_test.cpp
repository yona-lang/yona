#include <doctest/doctest.h>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <filesystem>
#include <vector>
#include "Parser.h"
#include "Codegen.h"
#include "repo_paths.h"
#include "yona_link_util.hpp"

using namespace std;
using namespace yona;
using namespace yona::compiler::codegen;
namespace fs = std::filesystem;

// ===== Helpers =====

static string compile_and_run_adt(const string& mod_source, const string& expr_source) {
    // Step 1: Compile the module
    parser::Parser p1;
    auto mod_result = p1.parse_module(mod_source, "adt_test.yona");
    if (!mod_result.has_value()) return "MOD_PARSE_ERROR";

    Codegen mod_codegen("adt_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    if (!mod) return "MOD_CODEGEN_ERROR";

    fs::path mod_obj = yona::test::link::scratch_root() / "adt_mod_test.o";
    if (!mod_codegen.emit_object_file(mod_obj.string())) return "MOD_EMIT_ERROR";

    // Step 2: Compile the expression
    parser::Parser p2;
    istringstream stream(expr_source);
    auto expr_result = p2.parse_input(stream);
    if (!expr_result.node) return "EXPR_PARSE_ERROR";

    Codegen expr_codegen("adt_test");
    auto expr_mod = expr_codegen.compile(expr_result.node.get());
    if (!expr_mod) return "EXPR_CODEGEN_ERROR";

    fs::path expr_obj = yona::test::link::scratch_root() / "adt_expr_test.o";
    if (!expr_codegen.emit_object_file(expr_obj.string())) return "EXPR_EMIT_ERROR";

    std::vector<fs::path> link_objs = {mod_obj, expr_obj};
    if (!yona::test::link::append_runtime_objects(link_objs)) return "RT_COMPILE_ERROR";

    fs::path exe_path = yona::test::link::scratch_root() / ("adt_test_exe" + yona::test::link::exe_suffix());
    if (!yona::test::link::link_objs_to_exe(link_objs, exe_path)) return "LINK_ERROR";

    return yona::test::link::popen_read_all(exe_path);
}

// ===== ADT Parsing Tests =====

TEST_SUITE("ADT") {

TEST_CASE("ADT declaration parses correctly") {
    parser::Parser parser;
    string source = R"(
module Test\Opt

export unwrap

type Option a = Some a | None
unwrap x = x
)";
    auto result = parser.parse_module(source, "opt.yona");
    REQUIRE(result.has_value());
    auto* mod = result.value().get();
    REQUIRE(mod != nullptr);
    CHECK(mod->adt_declarations.size() == 1);
    CHECK(mod->adt_declarations[0]->name == "Option");
    CHECK(mod->adt_declarations[0]->type_params.size() == 1);
    CHECK(mod->adt_declarations[0]->type_params[0] == "a");
    CHECK(mod->adt_declarations[0]->variants.size() == 2);
    CHECK(mod->adt_declarations[0]->variants[0]->name == "Some");
    CHECK(mod->adt_declarations[0]->variants[0]->field_type_names.size() == 1);
    CHECK(mod->adt_declarations[0]->variants[1]->name == "None");
    CHECK(mod->adt_declarations[0]->variants[1]->field_type_names.size() == 0);
}

TEST_CASE("ADT with no type parameters parses") {
    parser::Parser parser;
    string source = R"(
module Test\Color

export id

type Color = Red | Green | Blue
id x = x
)";
    auto result = parser.parse_module(source, "color.yona");
    REQUIRE(result.has_value());
    auto* mod = result.value().get();
    CHECK(mod->adt_declarations.size() == 1);
    CHECK(mod->adt_declarations[0]->name == "Color");
    CHECK(mod->adt_declarations[0]->type_params.empty());
    CHECK(mod->adt_declarations[0]->variants.size() == 3);
    CHECK(mod->adt_declarations[0]->variants[0]->name == "Red");
    CHECK(mod->adt_declarations[0]->variants[1]->name == "Green");
    CHECK(mod->adt_declarations[0]->variants[2]->name == "Blue");
}

TEST_CASE("Multiple ADT declarations in one module") {
    parser::Parser parser;
    string source = R"(
module Test\Types

export id

type Option a = Some a | None
type Result a e = Ok a | Err e
id x = x
)";
    auto result = parser.parse_module(source, "types.yona");
    REQUIRE(result.has_value());
    auto* mod = result.value().get();
    CHECK(mod->adt_declarations.size() == 2);
    CHECK(mod->adt_declarations[0]->name == "Option");
    CHECK(mod->adt_declarations[1]->name == "Result");
    CHECK(mod->adt_declarations[1]->type_params.size() == 2);
    CHECK(mod->adt_declarations[1]->variants.size() == 2);
    CHECK(mod->adt_declarations[1]->variants[0]->name == "Ok");
    CHECK(mod->adt_declarations[1]->variants[0]->field_type_names.size() == 1);
    CHECK(mod->adt_declarations[1]->variants[1]->name == "Err");
    CHECK(mod->adt_declarations[1]->variants[1]->field_type_names.size() == 1);
}

TEST_CASE("ADT module compiles to IR") {
    parser::Parser parser;
    string source = R"(
module Test\Opt

export wrap, unwrap

type Option a = Some a | None
wrap x = Some x
unwrap opt = case opt of
    Some v -> v
    None -> 0
end
)";
    auto result = parser.parse_module(source, "opt.yona");
    REQUIRE(result.has_value());

    Codegen codegen("adt_test");
    auto mod = codegen.compile_module(result.value().get());
    CHECK(mod != nullptr);
}

TEST_CASE("Zero-arity constructor compiles to constant") {
    parser::Parser parser;
    string source = R"(
module Test\Color

export red

type Color = Red | Green | Blue
red = Red
)";
    auto result = parser.parse_module(source, "color.yona");
    REQUIRE(result.has_value());

    Codegen codegen("color_test");
    auto mod = codegen.compile_module(result.value().get());
    CHECK(mod != nullptr);

    string ir = codegen.emit_ir();
    // The IR should contain the mangled export
    CHECK(ir.find("yona_Test_Color__red") != string::npos);
}

TEST_CASE("Constructor pattern matching compiles") {
    parser::Parser parser;
    string source = R"(
module Test\Match

export check

type Option a = Some a | None
check opt = case opt of
    Some v -> v
    None -> 0
end
)";
    auto result = parser.parse_module(source, "match.yona");
    REQUIRE(result.has_value());

    Codegen codegen("match_test");
    auto mod = codegen.compile_module(result.value().get());
    CHECK(mod != nullptr);
}

TEST_CASE("ADT with function type field parses correctly") {
    parser::Parser parser;
    string source = R"(
module Test\Cb

export id

type Callback = MkCallback (Int -> Int)
id x = x
)";
    auto result = parser.parse_module(source, "callback.yona");
    REQUIRE(result.has_value());
    auto* mod = result.value().get();
    REQUIRE(mod->adt_declarations.size() == 1);
    auto& ft = mod->adt_declarations[0]->variants[0]->field_type_names[0];
    CHECK(ft.is_function_type == true);
    CHECK(ft.param_types.size() == 1);
    CHECK(ft.param_types[0].name == "Int");
    CHECK(ft.return_types.size() == 1);
    CHECK(ft.return_types[0].name == "Int");
}

TEST_CASE("Scalar positional ADT fields preserve constructor arity") {
    parser::Parser parser;
    auto result = parser.parse_module(R"(
module Test\Point

type Point = Point Int Int
)", "point.yona");
    REQUIRE(result.has_value());
    REQUIRE(result.value()->adt_declarations.size() == 1);
    const auto* constructor = result.value()->adt_declarations[0]->variants[0];
    REQUIRE(constructor->field_type_names.size() == 2);
    CHECK(constructor->field_type_names[0].name == "Int");
    CHECK(constructor->field_type_names[1].name == "Int");
}

TEST_CASE("ADT with thunk function type parses correctly") {
    parser::Parser parser;
    string source = R"(
module Test\Str

export id

type Stream a = Next a (() -> Stream a) | End
id x = x
)";
    auto result = parser.parse_module(source, "stream.yona");
    REQUIRE(result.has_value());
    auto* mod = result.value().get();
    auto& next = mod->adt_declarations[0]->variants[0];
    CHECK(next->name == "Next");
    CHECK(next->field_type_names.size() == 2);
    CHECK(next->field_type_names[0].name == "a");
    CHECK(next->field_type_names[0].is_function_type == false);
    auto& fn_field = next->field_type_names[1];
    CHECK(fn_field.is_function_type == true);
    CHECK(fn_field.param_types.size() == 1);
    CHECK(fn_field.param_types[0].name == "()");
    CHECK(fn_field.return_types.size() == 1);
    CHECK(fn_field.return_types[0].name == "Stream a");
}

TEST_CASE("ADT with multi-param function type parses") {
    parser::Parser parser;
    string source = R"(
module Test\Red

export id

type Reducer a b = MkReducer (a -> b -> a)
id x = x
)";
    auto result = parser.parse_module(source, "reducer.yona");
    REQUIRE(result.has_value());
    auto* mod = result.value().get();
    auto& ft = mod->adt_declarations[0]->variants[0]->field_type_names[0];
    CHECK(ft.is_function_type == true);
    CHECK(ft.param_types.size() == 2);
    CHECK(ft.param_types[0].name == "a");
    CHECK(ft.param_types[1].name == "b");
    CHECK(ft.return_types.size() == 1);
    CHECK(ft.return_types[0].name == "a");
}

TEST_CASE("Closure stored in ADT field") {
    // A thunk (zero-arg closure capturing a value) stored in a recursive ADT field
    // and extracted via pattern matching, then called
    string mod_source = R"(
module Test\Thunk

export MkBox, unbox

type Box = MkBox Fn
unbox b = case b of
    MkBox f -> f ()
end
)";

    // Step 1: Compile module + emit interface file
    parser::Parser p1;
    auto mod_result = p1.parse_module(mod_source, "thunk.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("thunk_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "thunk_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));

    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib";
    fs::create_directories(yona_lib / "Test");
    REQUIRE(mod_codegen.emit_interface_file((yona_lib / "Test" / "Thunk.yonai").string()));

    // Step 2: Compile expression that imports from the module
    parser::Parser p2;
    string expr_source = "import Test\\Thunk in let n = 99 in let t = \\-> n in unbox (MkBox t)";
    istringstream stream(expr_source);
    auto expr_result = p2.parse_input(stream);
    REQUIRE(expr_result.node != nullptr);

    Codegen expr_codegen("thunk_test");
    expr_codegen.module_paths_.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result.node.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "thunk_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    std::vector<fs::path> thunk_objs = {mod_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(thunk_objs));
    fs::path exe_path = yona::test::link::scratch_root() / ("thunk_test_exe" + yona::test::link::exe_suffix());
    REQUIRE(yona::test::link::link_objs_to_exe(thunk_objs, exe_path));

    string result = yona::test::link::popen_read_all(exe_path);

    CHECK(result == "99");
}

TEST_CASE("Lazy stream takeStream") {
    string mod_source = R"(
module Test\Stream2

export Next, End, naturals, takeStream

type Stream a = Next a (() -> Stream a) | End
naturals n = let tail = \-> naturals (n + 1) in Next n tail
takeStream n s = if n <= 0 then [] else case s of End -> []; Next h t -> h :: (takeStream (n - 1) (t ())) end
)";

    parser::Parser p1;
    auto mod_result = p1.parse_module(mod_source, "stream2.yona");
    REQUIRE(mod_result.has_value());

    Codegen mod_codegen("stream2_mod");
    auto mod = mod_codegen.compile_module(mod_result.value().get());
    REQUIRE(mod != nullptr);
    fs::path mod_obj = yona::test::link::scratch_root() / "stream2_mod_test.o";
    REQUIRE(mod_codegen.emit_object_file(mod_obj.string()));

    fs::path yona_lib = yona::test::link::scratch_root() / "yona_lib_stream2";
    fs::create_directories(yona_lib / "Test");
    REQUIRE(mod_codegen.emit_interface_file((yona_lib / "Test" / "Stream2.yonai").string()));

    // take 5 naturals starting from 1
    parser::Parser p2;
    string expr_source = "import Test\\Stream2 in takeStream 5 (naturals 1)";
    istringstream stream(expr_source);
    auto expr_result = p2.parse_input(stream);
    REQUIRE(expr_result.node != nullptr);

    Codegen expr_codegen("stream2_test");
    expr_codegen.module_paths_.push_back(yona_lib.string());
    auto expr_mod = expr_codegen.compile(expr_result.node.get());
    REQUIRE(expr_mod != nullptr);
    fs::path expr_obj = yona::test::link::scratch_root() / "stream2_expr_test.o";
    REQUIRE(expr_codegen.emit_object_file(expr_obj.string()));

    std::vector<fs::path> s2_objs = {mod_obj, expr_obj};
    REQUIRE(yona::test::link::append_runtime_objects(s2_objs));
    fs::path exe_path = yona::test::link::scratch_root() / ("stream2_test_exe" + yona::test::link::exe_suffix());
    REQUIRE(yona::test::link::link_objs_to_exe(s2_objs, exe_path));

    string result = yona::test::link::popen_read_all(exe_path);

    CHECK(result == "[1, 2, 3, 4, 5]");
}

} // ADT
