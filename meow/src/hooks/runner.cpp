#include <meow/hooks/runner.hpp>
#include <meow/error/error.hpp>
#include <meow/log/logger.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace meow::hooks {

namespace {
    const char* hookTypeName(HookType t) {
        switch (t) {
            case HookType::PreInstall:  return "pre_install";
            case HookType::PostInstall: return "post_install";
            case HookType::PreRemove:   return "pre_remove";
            case HookType::PostRemove:  return "post_remove";
        }
        return "hook";
    }

    std::filesystem::path stagingDir(const std::string& pkg, HookType t) {
        auto dir = std::filesystem::temp_directory_path() / "meow" / "hooks" / pkg / hookTypeName(t);
        std::filesystem::create_directories(dir);
        return dir;
    }

    // Build the controlled environment. When not inheriting, we start from a
    // minimal set so the hook cannot rely on the builder's ambient PATH/HOME.
    std::vector<std::string> buildEnv(const std::string& pkg, const std::string& version,
                                      HookType t, bool inherit) {
        std::vector<std::string> env;
        if (inherit) {
            for (char** ep = environ; ep && *ep; ++ep) env.emplace_back(*ep);
        }
        auto set = [&](const std::string& kv) {
            auto sep = kv.find('=');
            std::string key = sep == std::string::npos ? kv : kv.substr(0, sep);
            for (auto& ent : env) {
                if (ent.rfind(key + "=", 0) == 0) { ent = kv; return; }
            }
            env.push_back(kv);
        };
        set("HOME=" + (std::filesystem::temp_directory_path() / "meow" / "hook-home").string());
        set("PATH=/usr/bin:/bin");
        set("TMPDIR=" + std::filesystem::temp_directory_path().string());
        set("MEOW_PACKAGE=" + pkg);
        set("MEOW_VERSION=" + version);
        set(std::string("MEOW_HOOK_TYPE=") + hookTypeName(t));
        set("MEOW_HOOK_STAGING=" + stagingDir(pkg, t).string());
        return env;
    }

    // OS-level network isolation is not yet implemented (requires
    // namespaces/seccomp). We never silently allow it: warn the operator.
    void enforceNetworkPolicy(const HookPolicy& policy) {
        static bool warned = false;
        if (!policy.allowNetwork && !warned) {
            log::log(log::LogLevel::Warning,
                "network isolation unavailable: hooks run with network access; "
                "set hooks.network=false in a sandboxed environment");
            warned = true;
        }
    }

    std::vector<std::string> splitLines(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (c == '\n') { out.push_back(cur); cur.clear(); }
            else if (c != '\r') cur.push_back(c);
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }
}

HookResult runHook(const std::filesystem::path& script,
                   const std::string& packageName,
                   const std::string& version,
                   HookType type,
                   const HookPolicy& policy) {
    if (!std::filesystem::exists(script)) {
        throw error::MeowError(error::ErrorCode::HookDenied,
            "hook script not found: " + script.string());
    }

    enforceNetworkPolicy(policy);

    auto cwd = stagingDir(packageName, type);
    auto env = buildEnv(packageName, version, type, policy.inheritEnvironment);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        throw error::MeowError(error::ErrorCode::HookFailed,
            "cannot create hook output pipe");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        throw error::MeowError(error::ErrorCode::HookFailed, "fork failed for hook");
    }

    if (pid == 0) {
        // Child: redirect stdout+stderr to the pipe, chdir to staging, exec.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (chdir(cwd.c_str()) != 0) {
            _exit(127);
        }

        std::string scriptStr = script.string();
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("/bin/sh"));
        argv.push_back(const_cast<char*>("-e"));
        argv.push_back(const_cast<char*>(scriptStr.c_str()));
        argv.push_back(nullptr);

        std::vector<char*> envp;
        for (auto& ev : env) envp.push_back(const_cast<char*>(ev.c_str()));
        envp.push_back(nullptr);

        execvpe("/bin/sh", argv.data(), envp.data());
        _exit(127); // exec failed
    }

    // Parent.
    close(pipefd[1]);

    std::string output;
    auto deadline = std::chrono::steady_clock::now() + policy.timeout;
    bool timedOut = false;

    char buf[4096];
    while (true) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining < 0) remaining = 0;

        struct pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, static_cast<int>(remaining));
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) { // timeout
            timedOut = true;
            break;
        }
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n <= 0) break;
        output.append(buf, static_cast<std::size_t>(n));
    }

    int status = 0;
    if (timedOut) {
        kill(pid, SIGTERM);
        auto grace = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < grace) {
            if (waitpid(pid, &status, WNOHANG) > 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (waitpid(pid, &status, WNOHANG) == 0) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    } else {
        // Drain any remaining output.
        while (true) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf));
            if (n <= 0) break;
            output.append(buf, static_cast<std::size_t>(n));
        }
        waitpid(pid, &status, 0);
    }
    close(pipefd[0]);

    // Log captured output through the normal logger.
    for (auto& line : splitLines(output)) {
        if (!line.empty()) log::log(log::LogLevel::Info, std::string(hookTypeName(type)) + " output: " + line);
    }

    if (timedOut) {
        throw error::MeowError(error::ErrorCode::HookTimeout,
            "hook timed out after " + std::to_string(policy.timeout.count()) + "s: " + hookTypeName(type));
    }

    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (exitCode != 0) {
        throw error::MeowError(error::ErrorCode::HookFailed,
            "hook " + std::string(hookTypeName(type)) + " exited with code " + std::to_string(exitCode));
    }

    HookResult result;
    result.exitCode = exitCode;
    result.output = std::move(output);
    return result;
}

}
