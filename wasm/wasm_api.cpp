// WebAssembly entry points. The page writes a command (UTF-8, NUL-terminated)
// into memory from sb_alloc, calls sb_command, and reads back a JSON string.

#include <cstdlib>
#include <string>

#include "session.h"

static sb::Session* g_session = nullptr;
static std::string g_out;

extern "C" {

__attribute__((export_name("sb_alloc"))) char* sb_alloc(int n) { return static_cast<char*>(std::malloc(n)); }

__attribute__((export_name("sb_free"))) void sb_free(char* p) { std::free(p); }

__attribute__((export_name("sb_command"))) const char* sb_command(const char* line) {
    if (!g_session) g_session = new sb::Session();
    g_out = g_session->command(line);
    return g_out.c_str();
}
}
