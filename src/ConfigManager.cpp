#include "ConfigManager.h"
#include <windows.h>
#include <sstream>
#include <vector>

namespace AdbTransfer {

ConfigManager::ConfigManager(const std::wstring& iniPath) {
    Load(iniPath);
}

std::wstring ConfigManager::GetDefaultIniPath() const {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path = exePath;
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return path.substr(0, pos + 1) + L"config.ini";
    }
    return L"config.ini";
}

bool ConfigManager::Load(const std::wstring& iniPath) {
    m_filePath = iniPath.empty() ? GetDefaultIniPath() : iniPath;

    // ファイルが存在しない場合はデフォルト値を維持
    DWORD attr = GetFileAttributesW(m_filePath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    wchar_t buf[512];

    // [General]
    GetPrivateProfileStringW(L"General", L"Language", L"ja", buf, 512, m_filePath.c_str());
    char mbBuf[512];
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, mbBuf, 512, nullptr, nullptr);
    m_language = mbBuf;
    if (m_language.empty()) m_language = "ja";

    GetPrivateProfileStringW(L"General", L"LastRemotePath", L"", buf, 512, m_filePath.c_str());
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, mbBuf, 512, nullptr, nullptr);
    m_lastRemotePath = mbBuf;

    // [Device]
    GetPrivateProfileStringW(L"Device", L"Name", L"", buf, 512, m_filePath.c_str());
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, mbBuf, 512, nullptr, nullptr);
    m_device.name = mbBuf;

    GetPrivateProfileStringW(L"Device", L"Serial", L"", buf, 512, m_filePath.c_str());
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, mbBuf, 512, nullptr, nullptr);
    m_device.serial = mbBuf;

    GetPrivateProfileStringW(L"Device", L"Ip", L"", buf, 512, m_filePath.c_str());
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, mbBuf, 512, nullptr, nullptr);
    m_device.ip = mbBuf;

    m_device.port = GetPrivateProfileIntW(L"Device", L"Port", 5555, m_filePath.c_str());

    // [Presets]
    m_presets.clear();
    int count = GetPrivateProfileIntW(L"Presets", L"Count", 0, m_filePath.c_str());
    for (int i = 0; i < count; ++i) {
        std::wstring keyName = L"Preset_" + std::to_wstring(i) + L"_Name";
        std::wstring keyPath = L"Preset_" + std::to_wstring(i) + L"_Path";

        GetPrivateProfileStringW(L"Presets", keyName.c_str(), L"", buf, 512, m_filePath.c_str());
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, mbBuf, 512, nullptr, nullptr);
        std::string name = mbBuf;

        GetPrivateProfileStringW(L"Presets", keyPath.c_str(), L"", buf, 512, m_filePath.c_str());
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, mbBuf, 512, nullptr, nullptr);
        std::string path = mbBuf;

        if (!name.empty() && !path.empty()) {
            m_presets.push_back({name, path});
        }
    }

    // [TransferList]
    m_transferPaths.clear();
    int transferCount = GetPrivateProfileIntW(L"TransferList", L"Count", 0, m_filePath.c_str());
    for (int i = 0; i < transferCount; ++i) {
        std::wstring keyPath = L"Item_" + std::to_wstring(i);
        GetPrivateProfileStringW(L"TransferList", keyPath.c_str(), L"", buf, 512, m_filePath.c_str());
        if (wcslen(buf) > 0) {
            m_transferPaths.push_back(buf);
        }
    }

    return true;
}

bool ConfigManager::Save(const std::wstring& iniPath) const {
    std::wstring targetPath = iniPath.empty() ? m_filePath : iniPath;
    if (targetPath.empty()) {
        targetPath = GetDefaultIniPath();
    }

    auto toW = [](const std::string& str) -> std::wstring {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> wbuf(len);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wbuf.data(), len);
        return std::wstring(wbuf.data());
    };

    // [General]
    WritePrivateProfileStringW(L"General", L"Language", toW(m_language).c_str(), targetPath.c_str());
    WritePrivateProfileStringW(L"General", L"LastRemotePath", toW(m_lastRemotePath).c_str(), targetPath.c_str());

    // [Device]
    WritePrivateProfileStringW(L"Device", L"Name", toW(m_device.name).c_str(), targetPath.c_str());
    WritePrivateProfileStringW(L"Device", L"Serial", toW(m_device.serial).c_str(), targetPath.c_str());
    WritePrivateProfileStringW(L"Device", L"Ip", toW(m_device.ip).c_str(), targetPath.c_str());
    WritePrivateProfileStringW(L"Device", L"Port", std::to_wstring(m_device.port).c_str(), targetPath.c_str());

    // [Presets]
    WritePrivateProfileStringW(L"Presets", L"Count", std::to_wstring(m_presets.size()).c_str(), targetPath.c_str());
    for (size_t i = 0; i < m_presets.size(); ++i) {
        std::wstring keyName = L"Preset_" + std::to_wstring(i) + L"_Name";
        std::wstring keyPath = L"Preset_" + std::to_wstring(i) + L"_Path";
        WritePrivateProfileStringW(L"Presets", keyName.c_str(), toW(m_presets[i].name).c_str(), targetPath.c_str());
        WritePrivateProfileStringW(L"Presets", keyPath.c_str(), toW(m_presets[i].remotePath).c_str(), targetPath.c_str());
    }

    // [TransferList]
    WritePrivateProfileStringW(L"TransferList", L"Count", std::to_wstring(m_transferPaths.size()).c_str(), targetPath.c_str());
    for (size_t i = 0; i < m_transferPaths.size(); ++i) {
        std::wstring keyPath = L"Item_" + std::to_wstring(i);
        WritePrivateProfileStringW(L"TransferList", keyPath.c_str(), m_transferPaths[i].c_str(), targetPath.c_str());
    }

    return true;
}

void ConfigManager::AddPreset(const std::string& name, const std::string& path) {
    for (const auto& item : m_presets) {
        if (item.remotePath == path) {
            return; // 重複登録防止
        }
    }
    m_presets.push_back({name, path});
}

bool ConfigManager::RemovePreset(size_t index) {
    if (index < m_presets.size()) {
        m_presets.erase(m_presets.begin() + index);
        return true;
    }
    return false;
}

void ConfigManager::ClearPresets() {
    m_presets.clear();
}

} // namespace AdbTransfer
