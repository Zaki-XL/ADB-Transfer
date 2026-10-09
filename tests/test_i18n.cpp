#include "I18nManager.h"
#include <cassert>
#include <iostream>
#include <windows.h>

void TestI18nManager() {
    std::cout << "[TEST] Running TestI18nManager..." << std::endl;

    auto& i18n = AdbTransfer::I18nManager::GetInstance();
    i18n.Init(L"lang");

    // 1. 利用可能な言語一覧の検証
    const auto& langs = i18n.RefreshAvailableLanguages();
    assert(!langs.empty());
    std::cout << "  - Found " << langs.size() << " languages." << std::endl;

    bool foundJa = false;
    bool foundEn = false;
    bool foundZh = false;
    for (const auto& lang : langs) {
        if (lang.code == "ja") {
            foundJa = true;
            assert(lang.name == L"日本語");
        } else if (lang.code == "en") {
            foundEn = true;
            assert(lang.name == L"English");
        } else if (lang.code == "zh") {
            foundZh = true;
            assert(lang.name == L"简体中文");
        }
    }
    assert(foundJa && foundEn && foundZh);
    assert(langs[0].code == "ja"); // ja が先頭にソートされていること

    // 2. 言語切り替えと翻訳取得の検証
    // 2.1 日本語
    bool setJa = i18n.SetLanguage("ja");
    assert(setJa);
    assert(i18n.GetCurrentLanguage() == "ja");
    assert(i18n.t("header.btn_register") == "端末を登録 (USB)");
    assert(i18n.tW("header.btn_register") == L"端末を登録 (USB)");
    assert(i18n.t("transfer.btn_start") == "転送開始");

    // 2.2 英語
    bool setEn = i18n.SetLanguage("en");
    assert(setEn);
    assert(i18n.GetCurrentLanguage() == "en");
    assert(i18n.t("header.btn_register") == "Register Device (USB)");
    assert(i18n.tW("header.btn_register") == L"Register Device (USB)");
    assert(i18n.t("transfer.btn_start") == "Start Transfer");

    // 2.3 簡体字中国語
    bool setZh = i18n.SetLanguage("zh");
    assert(setZh);
    assert(i18n.GetCurrentLanguage() == "zh");
    assert(i18n.t("header.btn_register") == "注册设备 (USB)");
    assert(i18n.tW("header.btn_register") == L"注册设备 (USB)");
    assert(i18n.t("transfer.btn_start") == "开始传输");

    // 3. パラメータ置換の検証
    i18n.SetLanguage("ja");
    std::map<std::string, std::wstring> paramsW = {
        {"device", L"Android Device"},
        {"ip", L"192.168.1.50"}
    };
    std::wstring formattedW = i18n.tW("header.status_connected", paramsW);
    assert(formattedW == L"接続中: Android Device (192.168.1.50)");

    // ストレージツリーのフォーマット検証 (日・英・中)
    std::map<std::string, std::wstring> storageParams = {
        {"uuid", L"9C33-6BBD"},
        {"free", L"84G"}
    };
    assert(i18n.tW("storage_tree.internal", storageParams) == L"内蔵ストレージ (空き: 84G)");
    assert(i18n.tW("storage_tree.sdcard", storageParams) == L"MicroSDカード [9C33-6BBD] (空き: 84G)");

    i18n.SetLanguage("en");
    assert(i18n.tW("storage_tree.internal", storageParams) == L"Internal Storage (Free: 84G)");
    assert(i18n.tW("storage_tree.sdcard", storageParams) == L"MicroSD Card [9C33-6BBD] (Free: 84G)");

    i18n.SetLanguage("zh");
    assert(i18n.tW("storage_tree.internal", storageParams) == L"内部存储 (可用: 84G)");
    assert(i18n.tW("storage_tree.sdcard", storageParams) == L"MicroSD 卡 [9C33-6BBD] (可用: 84G)");

    // 4. 欠損キーのフォールバック動作
    std::string missingStr = i18n.t("non.existent.dummy_key");
    assert(missingStr == "[MISSING: non.existent.dummy_key]");
    std::wstring missingWStr = i18n.tW("non.existent.dummy_key");
    assert(missingWStr == L"[MISSING: non.existent.dummy_key]");

    // 5. UTF-8 <-> Wide 変換ユーティリティの検証
    std::string origUtf8 = "テスト文字列 123 !? 日本語 English";
    std::wstring convertedWide = AdbTransfer::I18nManager::Utf8ToWide(origUtf8);
    std::string backUtf8 = AdbTransfer::I18nManager::WideToUtf8(convertedWide);
    assert(origUtf8 == backUtf8);

    std::cout << "[PASS] TestI18nManager passed!" << std::endl;
}
