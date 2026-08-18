#pragma once

#include <string>
#include <vector>

namespace yona::toolchain {

struct InProcessLldResult {
    bool ok = false;
    int ret_code = 1;
    bool can_run_again = true;
    std::string stdout_text;
    std::string stderr_text;

    std::string diagnostic_text() const {
        if (stderr_text.empty())
            return stdout_text;
        if (stdout_text.empty())
            return stderr_text;
        return stderr_text + "\n" + stdout_text;
    }
};

bool run_inprocess_lld(const std::vector<std::string>& args, InProcessLldResult& result);

} // namespace yona::toolchain
