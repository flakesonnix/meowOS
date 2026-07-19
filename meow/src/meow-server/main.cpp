// meow-server: minimal static repository HTTP server for meowOS.
//
// Serves a meow repository directory (as produced by `meow-repo`) over HTTP
// so that clients can fetch repository metadata, package manifests, and
// package artifacts. This is intentionally a thin, dependency-free static
// file server: it performs no signing, no transformation, and no indexing
// beyond mapping a request path to a file under the served root.
//
// Supported features:
//   * GET and HEAD
//   * byte-range requests (single range) for large artifacts
//   * ETag + 304 Not Modified for client-side caching
//   * correct content types for .toml, .sig, and package archives
//   * serving repository.toml at the root path "/"

#include <arpa/inet.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr size_t kReadBuf = 65536;

std::string contentTypeFor(const fs::path& p) {
    std::string ext = p.extension().string();
    if (ext == ".toml") return "application/toml; charset=utf-8";
    if (ext == ".sig") return "application/octet-stream";
    if (ext == ".zst") return "application/octet-stream";
    return "application/octet-stream";
}

std::string percentDecode(std::string s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = 0, lo = 0;
            if (std::sscanf(s.substr(i + 1, 2).c_str(), "%2x", &hi) == 1) {
                out.push_back(static_cast<char>(hi));
                i += 2;
            } else {
                out.push_back(s[i]);
            }
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

fs::path resolvePath(const fs::path& root, const std::string& rawPath) {
    std::string decoded = percentDecode(rawPath);
    // Strip query string.
    auto q = decoded.find('?');
    if (q != std::string::npos) decoded = decoded.substr(0, q);
    if (decoded.empty() || decoded[0] != '/') decoded = "/" + decoded;
    fs::path rel(decoded);
    if (!rel.empty() && rel.begin()->string() == "/") {
        // Drop the leading slash so it joins onto the root instead of
        // resolving to an absolute path.
        std::vector<std::string> parts;
        for (auto it = ++rel.begin(); it != rel.end(); ++it) parts.push_back(it->string());
        rel = fs::path();
        for (const auto& p : parts) rel /= p;
    }
    fs::path base = fs::weakly_canonical(root);
    // Prevent path traversal outside the served root.
    auto canonical = fs::weakly_canonical(base / rel);
    if (canonical != base && !canonical.string().starts_with(base.string() + "/")) {
        return fs::path();
    }
    if (fs::is_directory(canonical)) {
        fs::path index = canonical / "repository.toml";
        if (fs::exists(index)) return index;
    }
    return canonical;
}

void sendStatus(int fd, int code, const std::string& reason) {
    std::string line = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n";
    ::send(fd, line.c_str(), line.size(), 0);
}

void sendHeader(int fd, const std::string& key, const std::string& value) {
    std::string line = key + ": " + value + "\r\n";
    ::send(fd, line.c_str(), line.size(), 0);
}

void handleClient(int fd, const fs::path& root) {
    char buf[kReadBuf];
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        ::close(fd);
        return;
    }
    buf[n] = '\0';
    std::string_view request(buf, static_cast<size_t>(n));

    std::string method;
    std::string path;
    size_t sp1 = request.find(' ');
    if (sp1 == std::string::npos) {
        ::close(fd);
        return;
    }
    method = std::string(request.substr(0, sp1));
    size_t sp2 = request.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        ::close(fd);
        return;
    }
    path = std::string(request.substr(sp1 + 1, sp2 - sp1 - 1));

    // Parse Range header if present.
    std::string range;
    size_t hpos = 0;
    while (true) {
        auto nl = request.find("\r\n", hpos);
        if (nl == std::string::npos) break;
        std::string_view hdr = request.substr(hpos, nl - hpos);
        auto colon = hdr.find(':');
        if (colon != std::string::npos) {
            std::string_view k = hdr.substr(0, colon);
            std::string_view v = hdr.substr(colon + 1);
            while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
            if (k == "Range") range = std::string(v);
        }
        hpos = nl + 2;
        if (hpos >= request.size()) break;
    }

    if (method != "GET" && method != "HEAD") {
        sendStatus(fd, 405, "Method Not Allowed");
        sendHeader(fd, "Content-Length", "0");
        sendHeader(fd, "Connection", "close");
        ::send(fd, "\r\n", 2, 0);
        ::close(fd);
        return;
    }

    fs::path target = resolvePath(root, path);
    if (target.empty() || !fs::exists(target) || fs::is_directory(target)) {
        sendStatus(fd, 404, "Not Found");
        sendHeader(fd, "Content-Length", "0");
        sendHeader(fd, "Connection", "close");
        ::send(fd, "\r\n", 2, 0);
        ::close(fd);
        return;
    }

    std::ifstream in(target, std::ios::binary);
    in.seekg(0, std::ios::end);
    long long total = static_cast<long long>(in.tellg());
    in.seekg(0, std::ios::beg);

    std::string etag = "\"" + std::to_string(fs::file_size(target)) + "-" +
                       std::to_string(fs::last_write_time(target).time_since_epoch().count()) + "\"";

    long long start = 0;
    long long end = total - 1;
    bool hasRange = false;
    if (!range.empty() && range.rfind("bytes=", 0) == 0) {
        std::string spec = range.substr(6);
        size_t dash = spec.find('-');
        if (dash != std::string::npos) {
            std::string s = spec.substr(0, dash);
            std::string e = spec.substr(dash + 1);
            if (!s.empty()) start = std::stoll(s);
            if (!e.empty()) end = std::stoll(e);
            else end = total - 1;
            if (start <= end && start < total) hasRange = true;
        }
    }
    if (hasRange && end >= total) end = total - 1;

    int code = hasRange ? 206 : 200;
    const char* reason = hasRange ? "Partial Content" : "OK";

    sendStatus(fd, code, reason);
    sendHeader(fd, "Content-Type", contentTypeFor(target));
    sendHeader(fd, "Accept-Ranges", "bytes");
    sendHeader(fd, "ETag", etag);
    if (hasRange) {
        long long len = end - start + 1;
        sendHeader(fd, "Content-Range",
                   "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" +
                       std::to_string(total));
        sendHeader(fd, "Content-Length", std::to_string(len));
    } else {
        sendHeader(fd, "Content-Length", std::to_string(total));
    }
    sendHeader(fd, "Connection", "close");
    ::send(fd, "\r\n", 2, 0);

    if (method == "HEAD") {
        ::close(fd);
        return;
    }

    if (hasRange) in.seekg(start, std::ios::beg);
    long long remaining = hasRange ? (end - start + 1) : total;
    std::vector<char> chunk(kReadBuf);
    while (remaining > 0) {
        long long toRead = std::min<long long>(remaining, static_cast<long long>(chunk.size()));
        in.read(chunk.data(), toRead);
        std::streamsize got = in.gcount();
        if (got <= 0) break;
        ::send(fd, chunk.data(), static_cast<size_t>(got), 0);
        remaining -= got;
    }
    ::close(fd);
}

}  // namespace

int main(int argc, char** argv) {
    fs::path root = ".";
    int port = 8080;

    auto parsePort = [&](const char* value) {
        try {
            port = std::stoi(value);
        } catch (const std::exception&) {
            std::cerr << "error: invalid port: " << value << "\n";
            std::exit(1);
        }
    };

    for (int i = 1; i < argc - 1; ++i) {
        std::string_view a = argv[i];
        if (a == "--repo") root = argv[++i];
        else if (a == "--port") parsePort(argv[++i]);
    }
    if (argc >= 2 && std::string_view(argv[1]) != "--repo" &&
        std::string_view(argv[1]) != "--port") {
        // Allow `meow-server serve ./repo` form.
        if (std::string_view(argv[1]) == "serve") {
            if (argc >= 3) root = argv[2];
            for (int i = 3; i < argc - 1; ++i) {
                if (std::string_view(argv[i]) == "--port") parsePort(argv[++i]);
            }
        }
    }

    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::cerr << "error: repo directory does not exist: " << root.string() << "\n";
        return 1;
    }

    int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        std::cerr << "error: cannot create socket\n";
        return 1;
    }
    int yes = 1;
    ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "error: cannot bind to port " << port << "\n";
        return 1;
    }
    if (::listen(listenFd, 16) < 0) {
        std::cerr << "error: cannot listen\n";
        return 1;
    }

    std::cout << "meow-server: serving " << fs::absolute(root).string() << " on http://0.0.0.0:"
              << port << "\n";
    std::cout.flush();

    while (true) {
        int clientFd = ::accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) continue;
        std::thread(handleClient, clientFd, root).detach();
    }
    return 0;
}
