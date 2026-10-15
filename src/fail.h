#pragma once
// Error reporting that works with and without C++ exceptions.
// Native builds throw std::logic_error. The WebAssembly build has no exception
// support, so it traps; the GUI re-creates the engine and replays its command log.

#include <string>

#if defined(__cpp_exceptions)
#include <stdexcept>
#endif

namespace sb {

[[noreturn]] inline void fail(const std::string& msg) {
#if defined(__cpp_exceptions)
    throw std::logic_error(msg);
#else
    (void)msg;
    __builtin_trap();
#endif
}

}  // namespace sb
