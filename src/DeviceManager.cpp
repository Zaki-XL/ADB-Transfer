#include "DeviceManager.h"
#include <iostream>

namespace AdbTransfer {

bool DeviceManager::RegisterDeviceUsb(const AdbManager& adb,
                                      ConfigManager& config,
                                      std::string& outMessage) {
    auto devices = adb.GetDevices();
    std::string usbSerial;
    for (const auto& d : devices) {
        if (d.isUsb && d.state == "device") {
            usbSerial = d.serial;
            break;
        }
    }

    if (usbSerial.empty()) {
        outMessage = "USB接続された端末が見つかりません。USBデバッグを有効にして接続してください。";
        return false;
    }

    std::string model = adb.GetDeviceModel(usbSerial);
    if (model.empty()) model = "Android Device";

    std::string ip = adb.GetDeviceWifiIp(usbSerial);
    if (ip.empty()) {
        outMessage = "Wi-Fi IPアドレスの取得に失敗しました。端末がWi-Fiに接続されているか確認してください。";
        return false;
    }

    if (!adb.EnableTcpip(usbSerial, 5555)) {
        outMessage = "adb tcpip 5555 の有効化に失敗しました。";
        return false;
    }

    DeviceConfig dev;
    dev.name = model;
    dev.serial = usbSerial;
    dev.ip = ip;
    dev.port = 5555;
    config.SetDevice(dev);
    config.Save(L"");

    outMessage = "端末登録完了！ [" + model + "] (" + ip + ":5555)。USBケーブルを外してWi-Fiで使用可能です。";
    return true;
}

bool DeviceManager::AutoConnectWifi(const AdbManager& adb,
                                    const ConfigManager& config,
                                    std::string& outMessage) {
    const auto& dev = config.GetDevice();
    if (dev.ip.empty()) {
        outMessage = "未登録の端末です。「端末を登録 (USB)」を行ってください。";
        return false;
    }

    std::string connectMsg;
    bool ok = adb.ConnectWifi(dev.ip, dev.port, &connectMsg);
    if (ok) {
        outMessage = "Wi-Fi接続成功: " + dev.name + " (" + dev.ip + ":" + std::to_string(dev.port) + ")";
        return true;
    } else {
        outMessage = "Wi-Fi接続失敗 (" + dev.ip + "): " + connectMsg;
        return false;
    }
}

ConnectionState DeviceManager::CheckStatus(const AdbManager& adb,
                                           const ConfigManager& config) {
    ConnectionState state;
    const auto& dev = config.GetDevice();
    auto devices = adb.GetDevices();

    // 1. Wi-Fi端末を優先チェック
    std::string wifiTarget = dev.ip + ":" + std::to_string(dev.port);
    for (const auto& d : devices) {
        if (d.serial == wifiTarget && d.state == "device") {
            state.isConnected = true;
            state.isWifi = true;
            state.deviceName = dev.name.empty() ? "Wi-Fi端末" : dev.name;
            state.targetSerial = d.serial;
            return state;
        }
    }

    // 2. 任意のWi-Fiデバイスがあるか
    for (const auto& d : devices) {
        if (d.isWifi && d.state == "device") {
            state.isConnected = true;
            state.isWifi = true;
            state.deviceName = d.model.empty() ? d.serial : d.model;
            state.targetSerial = d.serial;
            return state;
        }
    }

    // 3. USBデバイスのチェック
    for (const auto& d : devices) {
        if (d.isUsb && d.state == "device") {
            state.isConnected = true;
            state.isWifi = false;
            state.deviceName = d.model.empty() ? dev.name : d.model;
            state.targetSerial = d.serial;
            return state;
        }
    }

    state.isConnected = false;
    return state;
}

} // namespace AdbTransfer
