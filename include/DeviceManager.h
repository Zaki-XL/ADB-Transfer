#pragma once

#include <string>
#include "AdbManager.h"
#include "ConfigManager.h"

namespace AdbTransfer {

struct ConnectionState {
    bool isConnected = false;
    bool isWifi = false;
    std::string deviceName;
    std::string targetSerial; // "192.168.1.50:5555" または USBシリアル
};

class DeviceManager {
public:
    DeviceManager() = default;

    // USB経由のワンクリック端末登録フロー
    bool RegisterDeviceUsb(const AdbManager& adb,
                           ConfigManager& config,
                           std::string& outMessage);

    // Wi-Fi自動接続
    bool AutoConnectWifi(const AdbManager& adb,
                         const ConfigManager& config,
                         std::string& outMessage);

    // 現在の接続状態を確認
    ConnectionState CheckStatus(const AdbManager& adb,
                                const ConfigManager& config);
};

} // namespace AdbTransfer
