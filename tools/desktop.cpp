// Spectacular Bid desktop app (Windows).
//
// One small .exe: it serves the same GUI as the browser version from 127.0.0.1 and
// opens it in an Edge app window (no tabs or address bar). The page keeps game
// state with its built-in WebAssembly engine, but sends every search to this
// process, where the engine runs natively and on several cores at once.
//
//   GET  /            the GUI (with window.SB_NATIVE = true)
//   POST /api/run     body: "<command>\n<log line>\n<log line>..."; replays the log in a
//                     fresh session, runs the command, returns its JSON
//   GET  /api/ping    keep-alive from the page; the app exits about 20 s after the window closes
//
// The same file builds on Linux for testing (it prints the address instead of opening a window).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#include "../src/session.h"
#include "gui_html.h"  // generated: const unsigned char kGuiHtml[]; const unsigned long kGuiHtmlLen;

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
using sock_t = SOCKET;
static void close_sock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static constexpr sock_t INVALID_SOCKET = -1;
static void close_sock(sock_t s) { close(s); }
#endif

namespace {

constexpr int kPort = 47615;  // fixed, so the page's saved game survives restarts
std::atomic<long long> g_last_ping{0};
std::atomic<bool> g_pinged{false};

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void send_all(sock_t s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const int n = send(s, data.data() + off, static_cast<int>(data.size() - off), 0);
        if (n <= 0) return;
        off += static_cast<size_t>(n);
    }
}

void reply(sock_t s, int code, const char* type, const std::string& body) {
    std::ostringstream h;
    h << "HTTP/1.1 " << code << (code == 200 ? " OK" : " Error") << "\r\nContent-Type: " << type
      << "\r\nContent-Length: " << body.size() << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(s, h.str() + body);
}

std::string run_command(const std::string& body) {
    std::istringstream in(body);
    std::string cmd, line;
    std::getline(in, cmd);
    sb::Session session;
    try {
        while (std::getline(in, line))
            if (!line.empty()) session.command(line);
        return session.command(cmd);
    } catch (const std::exception& e) {
        return std::string("{\"ok\":false,\"error\":\"engine error: ") + e.what() + "\"}";
    }
}

void handle(sock_t s) {
    std::string req;
    char buf[65536];
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        const int n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) { close_sock(s); return; }
        req.append(buf, static_cast<size_t>(n));
        header_end = req.find("\r\n\r\n");
        if (req.size() > (1 << 20)) { close_sock(s); return; }
    }
    size_t content_length = 0;
    {
        std::string lower = req.substr(0, header_end);
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto p = lower.find("content-length:");
        if (p != std::string::npos) content_length = std::strtoul(lower.c_str() + p + 15, nullptr, 10);
    }
    while (req.size() < header_end + 4 + content_length) {
        const int n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, static_cast<size_t>(n));
    }
    const std::string first = req.substr(0, req.find("\r\n"));
    const std::string body = req.substr(header_end + 4, content_length);

    if (first.rfind("GET / ", 0) == 0 || first.rfind("GET /index.html", 0) == 0) {
        std::string page(reinterpret_cast<const char*>(kGuiHtml), kGuiHtmlLen);
        const std::string flag = "<script>window.SB_NATIVE = true;</script>\n";
        const auto at = page.find("<title>");
        page.insert(at == std::string::npos ? 0 : at, flag);
        reply(s, 200, "text/html; charset=utf-8", page);
    } else if (first.rfind("GET /api/ping", 0) == 0) {
        g_last_ping = now_ms();
        g_pinged = true;
        reply(s, 200, "text/plain", "ok");
    } else if (first.rfind("POST /api/run", 0) == 0) {
        g_last_ping = now_ms();
        reply(s, 200, "application/json", run_command(body));
    } else {
        reply(s, 404, "text/plain", "not found");
    }
    close_sock(s);
}

void open_window(const std::string& url) {
#ifdef _WIN32
    // Edge app window with its own profile folder so it opens as a separate app.
    char appdata[MAX_PATH] = {0};
    std::string profile;
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata)))
        profile = std::string(appdata) + "\\SpectacularBid\\edge-profile";
    std::string args = "--app=" + url + " --window-size=1360,900";
    if (!profile.empty()) args += " --user-data-dir=\"" + profile + "\"";
    const auto r = reinterpret_cast<intptr_t>(ShellExecuteA(nullptr, "open", "msedge.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
    if (r <= 32) ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::printf("Spectacular Bid is running at %s\n", url.c_str());
    std::fflush(stdout);
#endif
}

}  // namespace

// Folder holding this executable.
std::string exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
#else
    char buf[4096] = {0};
    if (readlink("/proc/self/exe", buf, sizeof buf - 1) < 0) return ".";
#endif
    std::string p(buf);
    const auto cut = p.find_last_of("/\\");
    return cut == std::string::npos ? "." : p.substr(0, cut);
}

int main() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    // Trained models from sbtrain next to the exe, if any: the round-2 folder
    // (training2\promoted) when it has models, otherwise training\promoted.
    std::string models_dir = exe_dir() + "/training/promoted";
    if (FILE* f = std::fopen((exe_dir() + "/training2/promoted/info.bin").c_str(), "rb")) {
        std::fclose(f);
        models_dir = exe_dir() + "/training2/promoted";
    }
    const std::string loaded = sb::load_models_from(models_dir);
    std::printf("Engine models: %s\n", loaded.c_str());
    const std::string url = "http://127.0.0.1:" + std::to_string(kPort) + "/";
    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
#ifndef _WIN32
    int yes = 1;  // allow quick restarts on Linux (the Windows default already does)
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // local only
    if (srv == INVALID_SOCKET || bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(srv, 16) != 0) {
        // Already running: just show the existing window.
        open_window(url);
        return 0;
    }
    open_window(url);

    // Exit once the window has been closed for a while.
    const long long started = now_ms();
    std::thread([started] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const long long idle = now_ms() - (g_pinged ? g_last_ping.load() : started);
            if (idle > (g_pinged ? 20000 : 120000)) std::exit(0);
        }
    }).detach();

    for (;;) {
        sock_t c = accept(srv, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        std::thread(handle, c).detach();
    }
}
