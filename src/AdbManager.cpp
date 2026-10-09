#include "AdbManager.h"
#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <regex>

namespace AdbTransfer {

AdbManager::AdbManager() {
    m_adbPath = DetectDefaultAdbPath();
}

AdbManager::AdbManager(const std::wstring& adbExePath)
    : m_adbPath(adbExePath) {
}

std::wstring AdbManager::DetectDefaultAdbPath() const {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path = exePath;
    size_t pos = path.find_last_of(L"\\/");
    std::wstring dir = (pos != std::wstring::npos) ? path.substr(0, pos + 1) : L"";

    // 1. 同一ディレクトリの adb.exe
    if (GetFileAttributesW((dir + L"adb.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
        return dir + L"adb.exe";
    }
    // 2. bin/platform-tools/adb.exe
    if (GetFileAttributesW((dir + L"bin\\platform-tools\\adb.exe").c_str()) != INVALID_FILE_ATTRIBUTES) {
        return dir + L"bin\\platform-tools\\adb.exe";
    }
    // 3. カレントディレクトリ配下
    if (GetFileAttributesW(L"bin\\platform-tools\\adb.exe") != INVALID_FILE_ATTRIBUTES) {
        return L"bin\\platform-tools\\adb.exe";
    }
    if (GetFileAttributesW(L"adb.exe") != INVALID_FILE_ATTRIBUTES) {
        return L"adb.exe";
    }

    return L"adb.exe";
}

static std::wstring BuildCommandLine(const std::wstring& exe, const std::vector<std::string>& args) {
    std::wstring cmd = L"\"" + exe + L"\"";
    for (const auto& a : args) {
        cmd += L" ";
        // UTF-8 -> UTF-16
        int len = MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> w(len);
        MultiByteToWideChar(CP_UTF8, 0, a.c_str(), -1, w.data(), len);
        std::wstring warg = w.data();

        bool needQuote = (warg.find(L' ') != std::wstring::npos || warg.empty());
        if (needQuote) cmd += L"\"";
        cmd += warg;
        if (needQuote) cmd += L"\"";
    }
    return cmd;
}

int AdbManager::Execute(const std::vector<std::string>& args,
                        std::string& outStd,
                        std::string& outErr,
                        int timeoutMs) const {
    outStd.clear();
    outErr.clear();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
    HANDLE hStdErrRead = nullptr, hStdErrWrite = nullptr;

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) return -1;
    if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0)) {
        CloseHandle(hStdOutRead);
        CloseHandle(hStdOutWrite);
        return -1;
    }

    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdErrWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = BuildCommandLine(m_adbPath, args);
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    BOOL success = CreateProcessW(
        nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hStdOutWrite);
    CloseHandle(hStdErrWrite);

    if (!success) {
        CloseHandle(hStdOutRead);
        CloseHandle(hStdErrRead);
        return -1;
    }

    // 読み取りバッファ
    auto readPipe = [](HANDLE h, std::string& out) {
        char buf[4096];
        DWORD bytesRead = 0;
        while (ReadFile(h, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buf[bytesRead] = '\0';
            out.append(buf, bytesRead);
        }
    };

    std::thread thStd(readPipe, hStdOutRead, std::ref(outStd));
    std::thread thErr(readPipe, hStdErrRead, std::ref(outErr));

    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
    }

    if (thStd.joinable()) thStd.join();
    if (thErr.joinable()) thErr.join();

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hStdOutRead);
    CloseHandle(hStdErrRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
}

int AdbManager::ExecuteWithProgress(const std::vector<std::string>& args,
                                    std::function<void(const std::string& line)> onOutput,
                                    int timeoutMs) const {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hStdOutRead = nullptr, hStdOutWrite = nullptr;
    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0)) return -1;
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdOutWrite;
    si.hStdError = hStdOutWrite; // stderrも結合
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = BuildCommandLine(m_adbPath, args);
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    BOOL success = CreateProcessW(
        nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hStdOutWrite);
    if (!success) {
        CloseHandle(hStdOutRead);
        return -1;
    }

    std::thread thReader([hStdOutRead, onOutput]() {
        char buf[512];
        DWORD bytesRead = 0;
        std::string currentLine;
        while (ReadFile(hStdOutRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buf[bytesRead] = '\0';
            for (DWORD i = 0; i < bytesRead; ++i) {
                if (buf[i] == '\r' || buf[i] == '\n') {
                    if (!currentLine.empty()) {
                        if (onOutput) onOutput(currentLine);
                        currentLine.clear();
                    }
                } else {
                    currentLine += buf[i];
                }
            }
        }
        if (!currentLine.empty() && onOutput) {
            onOutput(currentLine);
        }
    });

    WaitForSingleObject(pi.hProcess, timeoutMs);
    if (thReader.joinable()) thReader.join();

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hStdOutRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exitCode);
}

std::vector<AdbDevice> AdbManager::GetDevices() const {
    std::string out, err;
    std::vector<AdbDevice> result;
    if (Execute({"devices", "-l"}, out, err) != 0) {
        return result;
    }

    std::istringstream stream(out);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.find("List of devices attached") != std::string::npos) {
            continue;
        }

        std::istringstream lineStream(line);
        std::string serial, state;
        if (!(lineStream >> serial >> state)) continue;

        AdbDevice dev;
        dev.serial = serial;
        dev.state = state;
        dev.isWifi = (serial.find(':') != std::string::npos);
        dev.isUsb = !dev.isWifi;

        std::string token;
        while (lineStream >> token) {
            if (token.rfind("model:", 0) == 0) dev.model = token.substr(6);
            if (token.rfind("product:", 0) == 0) dev.product = token.substr(8);
        }

        result.push_back(dev);
    }
    return result;
}

std::string AdbManager::GetDeviceModel(const std::string& serial) const {
    std::string out, err;
    std::vector<std::string> args = {"-s", serial, "shell", "getprop", "ro.product.model"};
    if (Execute(args, out, err) == 0) {
        // 末尾改行トリム
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) out.pop_back();
        return out;
    }
    return "";
}

std::string AdbManager::GetDeviceWifiIp(const std::string& serial) const {
    std::string out, err;
    std::vector<std::string> args = {"-s", serial, "shell", "ip", "-f", "inet", "addr", "show", "wlan0"};
    if (Execute(args, out, err) == 0) {
        std::regex ipRegex(R"(inet\s+([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+))");
        std::smatch match;
        if (std::regex_search(out, match, ipRegex) && match.size() > 1) {
            return match[1].str();
        }
    }
    return "";
}

bool AdbManager::EnableTcpip(const std::string& serial, int port) const {
    std::string out, err;
    std::vector<std::string> args = {"-s", serial, "tcpip", std::to_string(port)};
    return (Execute(args, out, err) == 0);
}

bool AdbManager::ConnectWifi(const std::string& ip, int port, std::string* outMsg) const {
    std::string out, err;
    std::string target = ip + ":" + std::to_string(port);
    std::vector<std::string> args = {"connect", target};
    int ret = Execute(args, out, err);
    if (outMsg) *outMsg = out.empty() ? err : out;
    return (ret == 0 && out.find("connected to") != std::string::npos);
}

bool AdbManager::DisconnectWifi(const std::string& ip, int port) const {
    std::string out, err;
    std::string target = ip + ":" + std::to_string(port);
    std::vector<std::string> args = {"disconnect", target};
    return (Execute(args, out, err) == 0);
}

int AdbManager::Push(const std::string& targetDevice,
                     const std::wstring& localPath,
                     const std::string& remotePath,
                     std::function<void(const std::string&)> progressCb) const {
    std::vector<std::string> args;
    if (!targetDevice.empty()) {
        args.push_back("-s");
        args.push_back(targetDevice);
    }
    args.push_back("push");

    // localPath (wstring) -> UTF-8 string
    int len = WideCharToMultiByte(CP_UTF8, 0, localPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> mb(len);
    WideCharToMultiByte(CP_UTF8, 0, localPath.c_str(), -1, mb.data(), len, nullptr, nullptr);
    args.push_back(mb.data());

    args.push_back(remotePath);

    if (progressCb) {
        return ExecuteWithProgress(args, progressCb);
    } else {
        std::string o, e;
        return Execute(args, o, e, 600000);
    }
}

std::string AdbManager::ShellExec(const std::string& targetDevice, const std::string& shellCmd) const {
    std::vector<std::string> args;
    if (!targetDevice.empty()) {
        args.push_back("-s");
        args.push_back(targetDevice);
    }
    args.push_back("shell");
    args.push_back(shellCmd);

    std::string out, err;
    Execute(args, out, err);
    return out;
}

int64_t AdbManager::GetRemoteFileSize(const std::string& targetDevice, const std::string& remotePath) const {
    std::string cmd = "stat -c %s \"" + remotePath + "\" 2>/dev/null";
    std::string out = ShellExec(targetDevice, cmd);
    try {
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) out.pop_back();
        if (!out.empty()) {
            return std::stoll(out);
        }
    } catch (...) {}
    return -1;
}

bool AdbManager::MakeRemoteDirectory(const std::string& targetDevice, const std::string& remotePath) const {
    std::string cmd = "mkdir -p \"" + remotePath + "\"";
    std::string out = ShellExec(targetDevice, cmd);
    return true;
}

} // namespace AdbTransfer
