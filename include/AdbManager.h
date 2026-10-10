#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <windows.h>

namespace AdbTransfer {

struct AdbDevice {
    std::string serial;
    std::string state;
    std::string model;
    std::string product;
    bool isUsb = false;
    bool isWifi = false;
};

class AdbManager {
public:
    AdbManager();
    explicit AdbManager(const std::wstring& adbExePath);
    ~AdbManager() = default;

    void SetAdbPath(const std::wstring& path) { m_adbPath = path; }
    const std::wstring& GetAdbPath() const { return m_adbPath; }

    // コマンド同期実行（標準出力・標準エラー出力を取得）
    int Execute(const std::vector<std::string>& args,
                std::string& outStd,
                std::string& outErr,
                int timeoutMs = 30000) const;

    // 非同期プロセス起動（行ごとのコールバック）
    int ExecuteWithProgress(const std::vector<std::string>& args,
                            std::function<void(const std::string& line)> onOutput,
                            int timeoutMs = 600000) const;

    // 主要ADB操作
    std::vector<AdbDevice> GetDevices() const;
    std::string GetDeviceModel(const std::string& serial) const;
    std::string GetDeviceWifiIp(const std::string& serial) const;
    bool EnableTcpip(const std::string& serial, int port = 5555) const;
    bool ConnectWifi(const std::string& ip, int port = 5555, std::string* outMsg = nullptr) const;
    bool DisconnectWifi(const std::string& ip, int port = 5555) const;

    // ファイル転送・リモート操作
    int Push(const std::string& targetDevice,
             const std::wstring& localPath,
             const std::string& remotePath,
             std::function<void(const std::string&)> progressCb = nullptr) const;

    std::string ShellExec(const std::string& targetDevice, const std::string& shellCmd) const;
    int64_t GetRemoteFileSize(const std::string& targetDevice, const std::string& remotePath) const;
    int64_t GetRemoteAvailableSpace(const std::string& targetDevice, const std::string& remotePath) const;
    bool MakeRemoteDirectory(const std::string& targetDevice, const std::string& remotePath) const;

    std::wstring DetectDefaultAdbPath() const;

private:
    std::wstring m_adbPath;
};

} // namespace AdbTransfer
