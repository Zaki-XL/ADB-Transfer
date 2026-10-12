#include "ConfigManager.h"
#include <cassert>
#include <iostream>
#include <windows.h>

void TestConfigManager() {
    std::cout << "[TEST] Running TestConfigManager..." << std::endl;

    std::wstring testIni = L"scratch\\test_config.ini";
    DeleteFileW(testIni.c_str());

    AdbTransfer::ConfigManager config;
    AdbTransfer::DeviceConfig dev;
    dev.name = "iPlay 60 mini Turbo";
    dev.serial = "ABC1234567";
    dev.ip = "192.168.1.100";
    dev.port = 5555;

    config.SetDevice(dev);
    config.SetLanguage("en");
    config.AddPreset("[SD] Books", "/storage/9C33-6BBD/Books");
    config.AddPreset("[内蔵] Comic", "/sdcard/Comic");

    // 重複登録の防止確認
    config.AddPreset("[SD] Books (Dup)", "/storage/9C33-6BBD/Books");
    assert(config.GetPresets().size() == 2);

    // 保存テスト
    bool saved = config.Save(testIni);
    assert(saved);

    // 読み込みテスト
    AdbTransfer::ConfigManager loadedConfig;
    bool loaded = loadedConfig.Load(testIni);
    assert(loaded);

    assert(loadedConfig.GetLanguage() == "en");
    assert(loadedConfig.GetDevice().name == "iPlay 60 mini Turbo");
    assert(loadedConfig.GetDevice().serial == "ABC1234567");
    assert(loadedConfig.GetDevice().ip == "192.168.1.100");
    assert(loadedConfig.GetDevice().port == 5555);

    assert(loadedConfig.GetPresets().size() == 2);
    assert(loadedConfig.GetPresets()[0].name == "[SD] Books");
    assert(loadedConfig.GetPresets()[0].remotePath == "/storage/9C33-6BBD/Books");
    assert(loadedConfig.GetPresets()[1].name == "[内蔵] Comic");
    assert(loadedConfig.GetPresets()[1].remotePath == "/sdcard/Comic");

    // プリセット削除テスト
    bool removed = loadedConfig.RemovePreset(0);
    assert(removed);
    assert(loadedConfig.GetPresets().size() == 1);
    assert(loadedConfig.GetPresets()[0].name == "[内蔵] Comic");

    // 転送リスト・転送先保持テスト
    config.SetTransferPaths({L"E:\\Test\\Folder1", L"E:\\Test\\file2.zip"});
    config.SetLastRemotePath("/storage/8144-1008/PV_Data");
    config.Save(testIni);
    AdbTransfer::ConfigManager loadedConfig2;
    loadedConfig2.Load(testIni);
    assert(loadedConfig2.GetTransferPaths().size() == 2);
    assert(loadedConfig2.GetTransferPaths()[0] == L"E:\\Test\\Folder1");
    assert(loadedConfig2.GetTransferPaths()[1] == L"E:\\Test\\file2.zip");
    assert(loadedConfig2.GetLastRemotePath() == "/storage/8144-1008/PV_Data");

    DeleteFileW(testIni.c_str());
    std::cout << "[PASS] TestConfigManager passed!" << std::endl;
}
