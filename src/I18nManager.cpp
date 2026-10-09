#include "I18nManager.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace AdbTransfer {

// UTF-8 <-> Wide 変換
std::wstring I18nManager::Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::vector<wchar_t> wbuf(len);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wbuf.data(), len);
    return std::wstring(wbuf.data());
}

std::string I18nManager::WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::vector<char> buf(len);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, buf.data(), len, nullptr, nullptr);
    return std::string(buf.data());
}

I18nManager& I18nManager::GetInstance() {
    static I18nManager instance;
    return instance;
}

void I18nManager::Init(const std::wstring& customLangDir) {
    if (!customLangDir.empty()) {
        m_langDir = customLangDir;
    } else {
        // 実行ファイルと同じディレクトリの lang/ をデフォルトとする
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring path = exePath;
        size_t pos = path.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            m_langDir = path.substr(0, pos + 1) + L"lang";
        } else {
            m_langDir = L"lang";
        }
    }

    RefreshAvailableLanguages();
    SetLanguage(m_currentLang);
}

// 簡易・堅牢な再帰下降 JSON パーサー (文字列値とネストされたオブジェクトを展開)
namespace {

class SimpleJsonParser {
public:
    explicit SimpleJsonParser(const std::string& json) : m_src(json), m_pos(0) {}

    bool Parse(std::map<std::string, std::string>& outMap, std::string& outLangName) {
        SkipWhitespace();
        if (m_pos >= m_src.size() || m_src[m_pos] != '{') return false;
        m_pos++; // skip '{'

        bool ok = ParseObject("", outMap);
        if (outMap.find("language_name") != outMap.end()) {
            outLangName = outMap["language_name"];
        }
        return ok;
    }

private:
    void SkipWhitespace() {
        while (m_pos < m_src.size()) {
            char c = m_src[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                m_pos++;
            } else if (c == '/' && m_pos + 1 < m_src.size() && m_src[m_pos + 1] == '/') {
                // 行コメント対応
                m_pos += 2;
                while (m_pos < m_src.size() && m_src[m_pos] != '\n') {
                    m_pos++;
                }
            } else {
                break;
            }
        }
    }

    bool ParseString(std::string& outStr) {
        SkipWhitespace();
        if (m_pos >= m_src.size() || m_src[m_pos] != '"') return false;
        m_pos++; // skip opening '"'

        outStr.clear();
        while (m_pos < m_src.size()) {
            char c = m_src[m_pos++];
            if (c == '"') {
                return true;
            } else if (c == '\\') {
                if (m_pos >= m_src.size()) return false;
                char esc = m_src[m_pos++];
                switch (esc) {
                    case '"':  outStr.push_back('"'); break;
                    case '\\': outStr.push_back('\\'); break;
                    case '/':  outStr.push_back('/'); break;
                    case 'b':  outStr.push_back('\b'); break;
                    case 'f':  outStr.push_back('\f'); break;
                    case 'n':  outStr.push_back('\n'); break;
                    case 'r':  outStr.push_back('\r'); break;
                    case 't':  outStr.push_back('\t'); break;
                    case 'u': {
                        // 4桁16進Unicodeエスケープ
                        if (m_pos + 4 <= m_src.size()) {
                            std::string hexStr = m_src.substr(m_pos, 4);
                            m_pos += 4;
                            unsigned int codepoint = 0;
                            std::stringstream ss;
                            ss << std::hex << hexStr;
                            ss >> codepoint;
                            // UTF-8 エンコード
                            if (codepoint <= 0x7F) {
                                outStr.push_back((char)codepoint);
                            } else if (codepoint <= 0x7FF) {
                                outStr.push_back((char)(0xC0 | ((codepoint >> 6) & 0x1F)));
                                outStr.push_back((char)(0x80 | (codepoint & 0x3F)));
                            } else {
                                outStr.push_back((char)(0xE0 | ((codepoint >> 12) & 0x0F)));
                                outStr.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
                                outStr.push_back((char)(0x80 | (codepoint & 0x3F)));
                            }
                        }
                        break;
                    }
                    default:   outStr.push_back(esc); break;
                }
            } else {
                outStr.push_back(c);
            }
        }
        return false;
    }

    bool ParseObject(const std::string& prefix, std::map<std::string, std::string>& outMap) {
        while (m_pos < m_src.size()) {
            SkipWhitespace();
            if (m_pos >= m_src.size()) return false;
            if (m_src[m_pos] == '}') {
                m_pos++;
                return true;
            }

            // キー
            std::string key;
            if (!ParseString(key)) return false;

            SkipWhitespace();
            if (m_pos >= m_src.size() || m_src[m_pos] != ':') return false;
            m_pos++; // skip ':'

            SkipWhitespace();
            if (m_pos >= m_src.size()) return false;

            std::string fullKey = prefix.empty() ? key : (prefix + "." + key);

            if (m_src[m_pos] == '{') {
                m_pos++; // skip '{'
                if (!ParseObject(fullKey, outMap)) return false;
            } else if (m_src[m_pos] == '"') {
                std::string val;
                if (!ParseString(val)) return false;
                outMap[fullKey] = val;
            } else {
                // その他のプリミティブ値 (数字、真偽値等)
                size_t start = m_pos;
                while (m_pos < m_src.size() && m_src[m_pos] != ',' && m_src[m_pos] != '}' && m_src[m_pos] != ']') {
                    m_pos++;
                }
                std::string rawVal = m_src.substr(start, m_pos - start);
                // トリム
                while (!rawVal.empty() && isspace((unsigned char)rawVal.front())) rawVal.erase(rawVal.begin());
                while (!rawVal.empty() && isspace((unsigned char)rawVal.back())) rawVal.pop_back();
                outMap[fullKey] = rawVal;
            }

            SkipWhitespace();
            if (m_pos < m_src.size() && m_src[m_pos] == ',') {
                m_pos++;
            }
        }
        return false;
    }

    const std::string& m_src;
    size_t m_pos;
};

} // namespace

bool I18nManager::LoadJsonFile(const std::wstring& path, std::map<std::string, std::string>& outMap, std::wstring& outLangName) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<char> buffer(size);
    DWORD bytesRead = 0;
    BOOL res = ReadFile(hFile, buffer.data(), size, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (!res || bytesRead == 0) return false;

    // BOM (0xEF, 0xBB, 0xBF) スキップ
    const char* start = buffer.data();
    size_t len = bytesRead;
    if (len >= 3 && (unsigned char)start[0] == 0xEF && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF) {
        start += 3;
        len -= 3;
    }

    std::string jsonStr(start, len);
    std::string langNameUtf8;
    SimpleJsonParser parser(jsonStr);
    if (!parser.Parse(outMap, langNameUtf8)) {
        return false;
    }

    outLangName = Utf8ToWide(langNameUtf8);
    return true;
}

const std::vector<LanguageInfo>& I18nManager::RefreshAvailableLanguages() {
    m_availableLanguages.clear();

    std::wstring searchPattern = m_langDir + L"\\*.json";
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring fileName = ffd.cFileName;
                size_t dotPos = fileName.find_last_of(L'.');
                if (dotPos != std::wstring::npos) {
                    std::wstring baseCodeW = fileName.substr(0, dotPos);
                    std::string code = WideToUtf8(baseCodeW);
                    std::wstring fullPath = m_langDir + L"\\" + fileName;

                    std::map<std::string, std::string> tempMap;
                    std::wstring langName;
                    if (LoadJsonFile(fullPath, tempMap, langName)) {
                        if (langName.empty()) {
                            langName = baseCodeW;
                        }
                        m_availableLanguages.push_back({ code, langName, fullPath });
                    }
                }
            }
        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    // 日本語 (ja) を先頭にし、その他を言語コード順でソート
    std::sort(m_availableLanguages.begin(), m_availableLanguages.end(), [](const LanguageInfo& a, const LanguageInfo& b) {
        if (a.code == "ja") return true;
        if (b.code == "ja") return false;
        return a.code < b.code;
    });

    return m_availableLanguages;
}

bool I18nManager::SetLanguage(const std::string& langCode) {
    std::wstring targetPath = m_langDir + L"\\" + Utf8ToWide(langCode) + L".json";

    std::map<std::string, std::string> newTranslations;
    std::wstring langName;

    if (!LoadJsonFile(targetPath, newTranslations, langName)) {
        // フォールバック: ja.json
        std::wstring jaPath = m_langDir + L"\\ja.json";
        if (!LoadJsonFile(jaPath, newTranslations, langName)) {
            return false;
        }
    }

    m_translations = std::move(newTranslations);
    m_currentLang = langCode;
    return true;
}

std::wstring I18nManager::tW(const std::string& key) const {
    auto it = m_translations.find(key);
    if (it != m_translations.end()) {
        return Utf8ToWide(it->second);
    }
    return Utf8ToWide("[MISSING: " + key + "]");
}

std::string I18nManager::t(const std::string& key) const {
    auto it = m_translations.find(key);
    if (it != m_translations.end()) {
        return it->second;
    }
    return "[MISSING: " + key + "]";
}

std::wstring I18nManager::tW(const std::string& key, const std::map<std::string, std::wstring>& params) const {
    std::wstring text = tW(key);
    for (const auto& kv : params) {
        std::wstring placeholder = L"{" + Utf8ToWide(kv.first) + L"}";
        size_t pos = 0;
        while ((pos = text.find(placeholder, pos)) != std::wstring::npos) {
            text.replace(pos, placeholder.length(), kv.second);
            pos += kv.second.length();
        }
    }
    return text;
}

std::string I18nManager::t(const std::string& key, const std::map<std::string, std::string>& params) const {
    std::string text = t(key);
    for (const auto& kv : params) {
        std::string placeholder = "{" + kv.first + "}";
        size_t pos = 0;
        while ((pos = text.find(placeholder, pos)) != std::string::npos) {
            text.replace(pos, placeholder.length(), kv.second);
            pos += kv.second.length();
        }
    }
    return text;
}

} // namespace AdbTransfer
