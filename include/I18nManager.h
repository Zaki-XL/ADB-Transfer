#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace AdbTransfer {

struct LanguageInfo {
    std::string code;       // 言語コード（例: "ja", "en", "zh"）
    std::wstring name;      // 表示名（例: L"日本語", L"English", L"简体中文"）
    std::wstring filePath;  // ファイルの絶対パス
};

class I18nManager {
public:
    static I18nManager& GetInstance();

    // 初期化および言語ディレクトリ設定（デフォルトは EXE と同階層の lang/）
    void Init(const std::wstring& customLangDir = L"");

    // lang/ ディレクトリを再スキャンして利用可能な言語一覧を更新
    const std::vector<LanguageInfo>& RefreshAvailableLanguages();

    // 言語を切り替え（成功した場合 true）
    bool SetLanguage(const std::string& langCode);

    // 現在選択されている言語コードを取得
    const std::string& GetCurrentLanguage() const { return m_currentLang; }

    // 翻訳文字列の取得（キーが存在しない場合は "[MISSING: key]" を返却）
    std::wstring tW(const std::string& key) const;
    std::string t(const std::string& key) const;

    // プレースホルダー置換付き翻訳
    std::wstring tW(const std::string& key, const std::map<std::string, std::wstring>& params) const;
    std::string t(const std::string& key, const std::map<std::string, std::string>& params) const;

    // 言語ディレクトリのパスを取得
    const std::wstring& GetLangDir() const { return m_langDir; }

    // UTF-8 <-> UTF-16 相互変換ユーティリティ
    static std::wstring Utf8ToWide(const std::string& str);
    static std::string WideToUtf8(const std::wstring& wstr);

private:
    I18nManager() = default;
    ~I18nManager() = default;
    I18nManager(const I18nManager&) = delete;
    I18nManager& operator=(const I18nManager&) = delete;

    bool LoadJsonFile(const std::wstring& path, std::map<std::string, std::string>& outMap, std::wstring& outLangName);

    std::wstring m_langDir;
    std::string m_currentLang = "ja";
    std::vector<LanguageInfo> m_availableLanguages;
    std::map<std::string, std::string> m_translations; // ドット区切りキー -> UTF-8文字列
};

// グローバルショートカット
inline std::wstring tW(const std::string& key) {
    return I18nManager::GetInstance().tW(key);
}

inline std::wstring tW(const std::string& key, const std::map<std::string, std::wstring>& params) {
    return I18nManager::GetInstance().tW(key, params);
}

inline std::string t(const std::string& key) {
    return I18nManager::GetInstance().t(key);
}

inline std::string t(const std::string& key, const std::map<std::string, std::string>& params) {
    return I18nManager::GetInstance().t(key, params);
}

} // namespace AdbTransfer
