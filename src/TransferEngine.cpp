#include "TransferEngine.h"
#include <windows.h>
#include <iostream>
#include <regex>

namespace AdbTransfer {

int64_t TransferEngine::GetLocalFileSize(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return -1;

    LARGE_INTEGER size;
    BOOL ok = GetFileSizeEx(hFile, &size);
    CloseHandle(hFile);

    return ok ? size.QuadPart : -1;
}

bool TransferEngine::IsDirectory(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::vector<std::wstring> TransferEngine::CollectLocalFiles(const std::wstring& folderPath) {
    std::vector<std::wstring> files;
    std::wstring searchPattern = folderPath + L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return files;

    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;

        std::wstring fullPath = folderPath + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            auto subFiles = CollectLocalFiles(fullPath);
            files.insert(files.end(), subFiles.begin(), subFiles.end());
        } else {
            files.push_back(fullPath);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return files;
}

bool TransferEngine::VerifyFile(const AdbManager& adb,
                                const std::string& targetDevice,
                                const std::wstring& localFilePath,
                                const std::string& remoteFilePath,
                                int64_t& outLocalSize,
                                int64_t& outRemoteSize) {
    outLocalSize = GetLocalFileSize(localFilePath);
    outRemoteSize = adb.GetRemoteFileSize(targetDevice, remoteFilePath);

    if (outLocalSize >= 0 && outRemoteSize >= 0 && outLocalSize == outRemoteSize) {
        return true;
    }
    return false;
}

TransferResult TransferEngine::Transfer(const AdbManager& adb,
                                        const std::string& targetDevice,
                                        const std::wstring& localPath,
                                        const std::string& remoteDir,
                                        ProgressCallback progressCb) {
    TransferResult res;
    res.localPath = localPath;

    // ファイル名（またはフォルダ名）の抽出
    size_t lastBackslash = localPath.find_last_of(L"\\/");
    std::wstring baseName = (lastBackslash != std::wstring::npos) ? localPath.substr(lastBackslash + 1) : localPath;

    int len = WideCharToMultiByte(CP_UTF8, 0, baseName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> mbBase(len);
    WideCharToMultiByte(CP_UTF8, 0, baseName.c_str(), -1, mbBase.data(), len, nullptr, nullptr);
    std::string baseNameUtf8 = mbBase.data();

    std::string normalizedRemoteDir = remoteDir;
    if (!normalizedRemoteDir.empty() && normalizedRemoteDir.back() != '/') {
        normalizedRemoteDir += "/";
    }

    std::string targetRemote = normalizedRemoteDir + baseNameUtf8;
    res.remotePath = targetRemote;

    bool isDir = IsDirectory(localPath);

    // ADB Push 実行（コールバックで進捗を拾う）
    auto onAdbLine = [&](const std::string& line) {
        if (!progressCb) return;

        // 進捗パーセントを抽出: "[ 45%] /remote/path"
        std::regex pctRegex(R"(\[\s*([0-9]+)%\])");
        std::smatch match;
        int pct = 50;
        if (std::regex_search(line, match, pctRegex) && match.size() > 1) {
            pct = std::stoi(match[1].str());
        }
        progressCb(1, 1, line, pct);
    };

    if (progressCb) {
        progressCb(1, 1, "転送中: " + baseNameUtf8, 10);
    }

    int pushExit = adb.Push(targetDevice, localPath, normalizedRemoteDir, onAdbLine);
    if (pushExit != 0) {
        res.success = false;
        res.errorMessage = "adb push 終了コード: " + std::to_string(pushExit);
        return res;
    }

    if (progressCb) {
        progressCb(1, 1, "ベリファイ中 (整合性照合)...", 95);
    }

    // ベリファイ
    if (!isDir) {
        // 単一ファイルの場合
        res.sizeMatched = VerifyFile(adb, targetDevice, localPath, targetRemote, res.localBytes, res.remoteBytes);
        res.success = res.sizeMatched;
        if (!res.sizeMatched) {
            res.errorMessage = "サイズ不一致: Local=" + std::to_string(res.localBytes) +
                               " Remote=" + std::to_string(res.remoteBytes);
        }
    } else {
        // フォルダの場合、全ファイルを照合
        auto allLocalFiles = CollectLocalFiles(localPath);
        int64_t totalLocal = 0;
        int64_t totalRemote = 0;
        bool allMatched = true;

        for (const auto& lf : allLocalFiles) {
            std::wstring relPathNorm = lf.substr(localPath.length());
            for (auto& wc : relPathNorm) {
                if (wc == L'\\') wc = L'/';
            }
            int rlen = WideCharToMultiByte(CP_UTF8, 0, relPathNorm.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::vector<char> rmb(rlen);
            WideCharToMultiByte(CP_UTF8, 0, relPathNorm.c_str(), -1, rmb.data(), rlen, nullptr, nullptr);
            std::string relPathUtf8 = rmb.data();

            std::string remoteFilePath = targetRemote + relPathUtf8;
            int64_t lSize = 0, rSize = 0;
            if (VerifyFile(adb, targetDevice, lf, remoteFilePath, lSize, rSize)) {
                totalLocal += lSize;
                totalRemote += rSize;
            } else {
                allMatched = false;
                res.errorMessage = "フォルダ内ファイル検証失敗: " + remoteFilePath;
                break;
            }
        }

        res.localBytes = totalLocal;
        res.remoteBytes = totalRemote;
        res.sizeMatched = allMatched;
        res.success = allMatched;
    }

    if (progressCb) {
        progressCb(1, 1, res.success ? "完了・ベリファイ成功" : "失敗: " + res.errorMessage, 100);
    }

    return res;
}

std::vector<TransferResult> TransferEngine::TransferBatch(const AdbManager& adb,
                                                          const std::string& targetDevice,
                                                          const std::vector<std::wstring>& localPaths,
                                                          const std::string& remoteDir,
                                                          ProgressCallback progressCb) {
    std::vector<TransferResult> results;
    int total = static_cast<int>(localPaths.size());

    for (int i = 0; i < total; ++i) {
        auto cb = [&](int, int, const std::string& msg, int pct) {
            if (progressCb) {
                progressCb(i + 1, total, msg, pct);
            }
        };

        TransferResult r = Transfer(adb, targetDevice, localPaths[i], remoteDir, cb);
        results.push_back(r);
    }

    return results;
}

} // namespace AdbTransfer
