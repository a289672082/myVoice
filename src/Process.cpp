#include "Process.h"

#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <thread>
#endif

namespace myvoice::detail {

#ifdef _WIN32
namespace {
// Windows CRT 的 argv 转义规则：反斜杠出现在双引号前时必须翻倍。
std::wstring quote_windows(const std::wstring& arg) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result += L'"';
        } else {
            result.append(slashes, L'\\');
            result += c;
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
struct Handle {
    HANDLE h = nullptr;
    explicit Handle(HANDLE value = nullptr) : h(value) {}
    ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
}  // namespace

int run_process(const std::filesystem::path& executable,
                const std::vector<std::filesystem::path>& args,
                const std::filesystem::path& working_directory,
                std::chrono::milliseconds timeout) {
    std::wstring command = quote_windows(executable.wstring());
    for (const auto& a : args) command += L" " + quote_windows(a.wstring());
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring cwd = working_directory.wstring();
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, cwd.c_str(), &startup, &process)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "CreateProcessW failed (check Python executable)");
    }
    Handle thread(process.hThread);
    Handle child(process.hProcess);
    // 若超过时限则结束子进程；不声称能够终止 Python 自行派生的整棵进程树。
    const auto ms = static_cast<ULONGLONG>(timeout.count());
    const DWORD wait_ms = static_cast<DWORD>(ms > 0xFFFFFFFEull ? 0xFFFFFFFEull : ms);
    DWORD status = WaitForSingleObject(child.h, wait_ms);
    if (status == WAIT_TIMEOUT) {
        TerminateProcess(child.h, 124);
        WaitForSingleObject(child.h, INFINITE);
        throw std::runtime_error("Voice conversion process timed out");
    }
    if (status != WAIT_OBJECT_0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WaitForSingleObject failed");
    }
    DWORD exit_code = 1;
    if (!GetExitCodeProcess(child.h, &exit_code)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "GetExitCodeProcess failed");
    }
    return static_cast<int>(exit_code);
}
#else
int run_process(const std::filesystem::path& executable,
                const std::vector<std::filesystem::path>& args,
                const std::filesystem::path& working_directory,
                std::chrono::milliseconds timeout) {
    const auto exec = executable.string();
    std::vector<std::string> strings;
    strings.reserve(args.size() + 1);
    strings.push_back(exec);
    for (const auto& a : args) strings.push_back(a.string());
    std::vector<char*> argv;
    for (auto& s : strings) argv.push_back(s.data());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) throw std::system_error(errno, std::generic_category(), "fork failed");
    if (pid == 0) {
        // 独立进程组，超时时能连同子进程一起清理。
        setpgid(0, 0);
        if (chdir(working_directory.c_str()) != 0) _exit(126);
        execvp(exec.c_str(), argv.data());
        _exit(127);
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (true) {
        pid_t got = waitpid(pid, &status, WNOHANG);
        if (got == pid) break;
        if (got < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "waitpid failed");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error("Voice conversion process timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
#endif

}  // namespace myvoice::detail
