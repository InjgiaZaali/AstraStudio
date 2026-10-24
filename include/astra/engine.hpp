#pragma once

#include <cstddef>
#include <string>

namespace astra {

struct Result {
    bool ok = false;
    std::string output;
    std::string disassembly;
    std::string error;
    int errorLine = 0;
    std::size_t instructions = 0;
    std::size_t functions = 0;
    std::size_t steps = 0;
    double elapsedMs = 0.0;
};

// Compile source into a bytecode program and optionally execute it.
// The runtime is deliberately limited to one million instructions and 1 MiB output.
// It is an educational VM, not a security boundary for hostile native callers.
Result execute(const std::string& source, bool run = true);

}  // namespace astra
