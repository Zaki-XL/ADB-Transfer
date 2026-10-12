#pragma once

#include <string>
#include <vector>

namespace AdbTransfer {

struct PresetItem {
    std::string name;       // 表示名（例: "[SD] Books"）
    std::string remotePath; // リモートパス（例: "/storage/9C33-6BBD/Books"）
};

struct DeviceConfig {
    std::string name;   // 端末名（例: "iPlay 60 mini Turbo"）
    std::string serial; // USBシリアル番号
    std::string ip;     // Wi-Fi IPアドレス
    int port = 5555;    // ポート番号
};

class ConfigManager {
public:
    ConfigManager() = default;
    explicit ConfigManager(const std::wstring& iniPath);

    bool Load(const std::wstring& iniPath);
    bool Save(const std::wstring& iniPath) const;

    const std::string& GetLanguage() const { return m_language; }
    void SetLanguage(const std::string& lang) { m_language = lang; }

    const DeviceConfig& GetDevice() const { return m_device; }
    void SetDevice(const DeviceConfig& dev) { m_device = dev; }

    const std::vector<PresetItem>& GetPresets() const { return m_presets; }
    void AddPreset(const std::string& name, const std::string& path);
    bool RemovePreset(size_t index);
    void ClearPresets();

    const std::vector<std::wstring>& GetTransferPaths() const { return m_transferPaths; }
    void SetTransferPaths(const std::vector<std::wstring>& paths) { m_transferPaths = paths; }

    const std::string& GetLastRemotePath() const { return m_lastRemotePath; }
    void SetLastRemotePath(const std::string& path) { m_lastRemotePath = path; }

    std::wstring GetDefaultIniPath() const;

private:
    std::string m_language = "ja";
    DeviceConfig m_device;
    std::vector<PresetItem> m_presets;
    std::vector<std::wstring> m_transferPaths;
    std::string m_lastRemotePath;
    std::wstring m_filePath;
};

} // namespace AdbTransfer
