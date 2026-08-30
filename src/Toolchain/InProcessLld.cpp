#include "yona/Toolchain/InProcessLld.h"

#include "yona/Toolchain/LinkerPlan.h"

#include <vector>

#if defined(YONA_ENABLE_INPROCESS_LLD) && YONA_ENABLE_INPROCESS_LLD
#include "lld/Common/Driver.h"
#include "llvm/Support/raw_ostream.h"

#ifdef _WIN32
LLD_HAS_DRIVER(coff)
#elif defined(__APPLE__)
LLD_HAS_DRIVER(macho)
#elif defined(__linux__)
LLD_HAS_DRIVER(elf)
#endif

#endif

namespace yona::toolchain {

bool run_inprocess_lld(const std::vector<std::string> &args,
                       InProcessLldResult &result) {
  result = {};
  if (!inProcessLldAvailable()) {
    result.stderr_text = inProcessLldUnavailableReason();
    return false;
  }

#if defined(YONA_ENABLE_INPROCESS_LLD) && YONA_ENABLE_INPROCESS_LLD
  std::vector<const char *> argv;
  argv.reserve(args.size());
  for (const auto &s : args)
    argv.push_back(s.c_str());

  std::string out_s;
  std::string err_s;
  llvm::raw_string_ostream out_os(out_s);
  llvm::raw_string_ostream err_os(err_s);

  lld::Result r{1, true};
#ifdef _WIN32
  const lld::DriverDef drivers[] = {{lld::WinLink, &lld::coff::link}};
  r = lld::lldMain(argv, out_os, err_os, drivers);
#elif defined(__APPLE__)
  const lld::DriverDef drivers[] = {{lld::Darwin, &lld::macho::link}};
  r = lld::lldMain(argv, out_os, err_os, drivers);
#elif defined(__linux__)
  const lld::DriverDef drivers[] = {{lld::Gnu, &lld::elf::link}};
  r = lld::lldMain(argv, out_os, err_os, drivers);
#else
  result.stderr_text =
      "embedded LLD backend is not wired for this platform yet";
  return false;
#endif
  out_os.flush();
  err_os.flush();

  result.ret_code = r.retCode;
  result.can_run_again = r.canRunAgain;
  result.stdout_text = std::move(out_s);
  result.stderr_text = std::move(err_s);
  result.ok = (r.retCode == 0);
  return result.ok;
#else
  (void)args;
  result.stderr_text = inProcessLldUnavailableReason();
  return false;
#endif
}

} // namespace yona::toolchain
