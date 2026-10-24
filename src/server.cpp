#include "astra/engine.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#    define NOMINMAX
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#    include <shellapi.h>
using Socket = SOCKET;
constexpr Socket InvalidSocket = INVALID_SOCKET;
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
using Socket = int;
constexpr Socket InvalidSocket = -1;
#endif

namespace {

void closeSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool writeAll(Socket socket, std::string_view value) {
    while (!value.empty()) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const auto written = send(socket, value.data(), static_cast<int>(std::min<std::size_t>(value.size(), 32768)), flags);
        if (written <= 0) return false;
        value.remove_prefix(static_cast<std::size_t>(written));
    }
    return true;
}

std::string jsonEscape(std::string_view text) {
    std::string result;
    result.reserve(text.size() + 16);
    constexpr char digits[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 32) {
                    result += "\\u00";
                    result += digits[c >> 4];
                    result += digits[c & 15];
                } else result += static_cast<char>(c);
        }
    }
    return result;
}

std::string serialize(const astra::Result& result) {
    std::ostringstream stream;
    stream << "{\"ok\":" << (result.ok ? "true" : "false")
           << ",\"output\":\"" << jsonEscape(result.output)
           << "\",\"disassembly\":\"" << jsonEscape(result.disassembly)
           << "\",\"error\":\"" << jsonEscape(result.error)
           << "\",\"errorLine\":" << result.errorLine
           << ",\"instructions\":" << result.instructions
           << ",\"functions\":" << result.functions
           << ",\"steps\":" << result.steps
           << ",\"elapsedMs\":" << result.elapsedMs << "}";
    return stream.str();
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void respond(Socket client, int code, std::string_view status,
             std::string_view mime, const std::string& body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << code << ' ' << status << "\r\n"
           << "Content-Type: " << mime << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Cache-Control: no-store\r\n"
           << "X-Content-Type-Options: nosniff\r\n"
           << "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; frame-ancestors 'none'\r\n"
           << "Connection: close\r\n\r\n";
    writeAll(client, header.str());
    writeAll(client, body);
}

void handleClient(Socket client, const std::filesystem::path& webDir, unsigned short port) {
    std::string request;
    request.reserve(4096);
    constexpr std::size_t maxHeaders = 16384;
    constexpr std::size_t maxBody = 131072;
    std::array<char, 4096> buffer{};
    std::size_t headEnd = std::string::npos;
    while ((headEnd = request.find("\r\n\r\n")) == std::string::npos) {
        if (request.size() > maxHeaders) {
            respond(client, 431, "Request Header Fields Too Large", "text/plain", "Headers too large");
            return;
        }
        const int count = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count <= 0) return;
        request.append(buffer.data(), static_cast<std::size_t>(count));
    }
    const std::string head = request.substr(0, headEnd + 2);
    std::istringstream lineReader(head);
    std::string method, path, protocol;
    lineReader >> method >> path >> protocol;
    if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0") {
        respond(client, 400, "Bad Request", "text/plain", "Invalid request"); return;
    }
    // Reject unexpected Host values, including DNS rebinding requests.
    const std::string allowedHostA = "Host: 127.0.0.1:" + std::to_string(port);
    const std::string allowedHostB = "Host: localhost:" + std::to_string(port);
    if (head.find(allowedHostA) == std::string::npos && head.find(allowedHostB) == std::string::npos &&
        head.find("host: 127.0.0.1:" + std::to_string(port)) == std::string::npos &&
        head.find("host: localhost:" + std::to_string(port)) == std::string::npos) {
        respond(client, 403, "Forbidden", "text/plain", "Invalid Host header"); return;
    }
    if (method == "GET") {
        if (path == "/api/health") {
            respond(client, 200, "OK", "application/json; charset=utf-8", "{\"ok\":true,\"version\":\"0.1.0\"}");
            return;
        }
        std::string filename, mime;
        if (path == "/" || path == "/index.html") { filename = "index.html"; mime = "text/html; charset=utf-8"; }
        else if (path == "/style.css") { filename = "style.css"; mime = "text/css; charset=utf-8"; }
        else if (path == "/app.js") { filename = "app.js"; mime = "application/javascript; charset=utf-8"; }
        else { respond(client, 404, "Not Found", "text/plain", "Not found"); return; }
        const std::string content = readFile(webDir / filename);
        if (content.empty()) respond(client, 500, "Internal Server Error", "text/plain", "Web assets not found: check installation");
        else respond(client, 200, "OK", mime, content);
        return;
    }
    if (method != "POST" || (path != "/api/run" && path != "/api/analyze")) {
        respond(client, 404, "Not Found", "text/plain", "Unknown route"); return;
    }
    // Reject hostile browser-originated submissions. This does not make the VM a sandbox.
    std::size_t length = 0;
    bool foundLength = false;
    bool originRejected = false;
    bool hostRejected = false;
    bool hasHost = false;
    const std::string allowedA = "127.0.0.1:" + std::to_string(port);
    const std::string allowedB = "localhost:" + std::to_string(port);
    std::string headerLine;
    while (std::getline(lineReader, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') headerLine.pop_back();
        const std::size_t delim = headerLine.find(':');
        if (delim == std::string::npos) continue;
        std::string key = headerLine.substr(0, delim);
        for (char& c : key) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        std::string value = headerLine.substr(delim + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        if (key == "content-length") {
            try {
                const auto parsed = std::stoull(value);
                if (parsed > maxBody) { respond(client, 413, "Content Too Large", "text/plain", "Source limit is 128 KiB"); return; }
                length = static_cast<std::size_t>(parsed);
                foundLength = true;
            } catch (...) { respond(client, 400, "Bad Request", "text/plain", "Invalid Content-Length"); return; }
        }
        if (key == "origin" && value != "http://" + allowedA && value != "http://" + allowedB)
            originRejected = true;
        if (key == "host") {
            hasHost = true;
            if (value != allowedA && value != allowedB) hostRejected = true;
        }
    }
    if (originRejected || hostRejected || !hasHost) {
        respond(client, 403, "Forbidden", "text/plain", "Invalid request origin or host"); return;
    }
    if (!foundLength) { respond(client, 411, "Length Required", "text/plain", "Content-Length required"); return; }
    std::string source = request.substr(headEnd + 4);
    if (source.size() > length) source.resize(length);
    while (source.size() < length) {
        const int count = recv(client, buffer.data(), static_cast<int>(std::min<std::size_t>(buffer.size(), length - source.size())), 0);
        if (count <= 0) return;
        source.append(buffer.data(), static_cast<std::size_t>(count));
    }
    auto result = astra::execute(source, path == "/api/run");
    respond(client, 200, "OK", "application/json; charset=utf-8", serialize(result));
}

void openBrowser(unsigned short port) {
    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::system(("open '" + url + "' >/dev/null 2>&1").c_str());
#else
    std::system(("xdg-open '" + url + "' >/dev/null 2>&1").c_str());
#endif
}

}  // namespace

int main(int argc, char** argv) {
    unsigned short port = 4747;
    bool noBrowser = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--no-browser") noBrowser = true;
        else if (arg == "--port" && i + 1 < argc) {
            try {
                const int requested = std::stoi(argv[++i]);
                if (requested < 1024 || requested > 65535) throw std::out_of_range("port");
                port = static_cast<unsigned short>(requested);
            } catch (...) { std::cerr << "Port must be between 1024 and 65535\n"; return 1; }
        } else { std::cerr << "Usage: astra-studio [--port 4747] [--no-browser]\n"; return 1; }
    }
#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { std::cerr << "Winsock initialization failed\n"; return 1; }
#endif
    const Socket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == InvalidSocket) { std::cerr << "Cannot create server socket\n"; return 1; }
#ifndef _WIN32
    const int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(server, 8) != 0) {
        std::cerr << "Could not start on 127.0.0.1:" << port << " (port in use?)\n";
        closeSocket(server); return 1;
    }
    const auto executable = std::filesystem::absolute(argv[0]);
    const std::filesystem::path webDir = executable.parent_path() / "web";
    if (!std::filesystem::exists(webDir / "index.html")) {
        std::cerr << "Missing web assets next to executable: " << webDir << "\n";
        closeSocket(server); return 1;
    }
    std::cout << "ASTRA STUDIO | C++20 bytecode IDE\n"
              << "Open http://127.0.0.1:" << port << "/ in your browser\n"
              << "Press Ctrl+C to shut down.\n" << std::flush;
    if (!noBrowser) std::thread([port] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); openBrowser(port); }).detach();
    while (true) {
        sockaddr_in clientAddress{};
#ifdef _WIN32
        int addressLength = sizeof(clientAddress);
#else
        socklen_t addressLength = sizeof(clientAddress);
#endif
        Socket client = accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &addressLength);
        if (client == InvalidSocket) continue;
#ifdef _WIN32
        DWORD timeout = 4000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        timeval timeout{4, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
        try { handleClient(client, webDir, port); }
        catch (const std::exception& e) { std::cerr << "Request error: " << e.what() << '\n'; }
        closeSocket(client);
    }
}
