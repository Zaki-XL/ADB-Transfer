#include "TransferEngine.h"
#include "I18nManager.h"
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
    std::string baseNameUtf8 = I18nManager::WideToUtf8(baseName);

    std::string normalizedRemoteDir = remoteDir;
    if (!normalizedRemoteDir.empty() && normalizedRemoteDir.back() != '/') {
        normalizedRemoteDir += "/";
    }

    std::string targetRemote = normalizedRemoteDir + baseNameUtf8;
    res.remotePath = targetRemote;

    bool isDir = IsDirectory(localPath);

    // ADB Push 実行（コールバックで進捗を拾う）
    std::string lastAdbError = "";
    auto onAdbLine = [&](const std::string& line) {
        if (line.find("error:") != std::string::npos || line.find("failed") != std::string::npos || line.find("cannot") != std::string::npos) {
            lastAdbError = line;
        }
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

    // フォルダの場合はターゲットディレクトリを事前に作成
    if (isDir) {
        adb.MakeRemoteDirectory(targetDevice, targetRemote);
    }

    std::string pushDest = isDir ? targetRemote + "/" : targetRemote;
    int pushExit = adb.Push(targetDevice, localPath, pushDest, onAdbLine);
    if (pushExit != 0) {
        res.success = false;
        res.errorMessage = lastAdbError.empty() ? ("adb push 終了コード: " + std::to_string(pushExit)) : lastAdbError;
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
            std::string relPathUtf8 = I18nManager::WideToUtf8(relPathNorm);

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

std::vector<TransferResult> TransferEngine::TransferTasks(const AdbManager& adb,
                                                          const std::string& targetDevice,
                                                          const std::vector<TransferTask>& tasks,
                                                          ProgressCallback progressCb) {
    std::vector<TransferResult> results;
    int total = static_cast<int>(tasks.size());

    for (int i = 0; i < total; ++i) {
        const auto& t = tasks[i];
        TransferResult res;
        res.localPath = t.localFilePath;

        // リモート側ターゲットディレクトリを事前に作成
        adb.MakeRemoteDirectory(targetDevice, t.remoteTargetDir);

        std::string pushDest = t.remoteTargetDir;
        if (!pushDest.empty() && pushDest.back() != '/') {
            pushDest += "/";
        }

        size_t lastSlash = t.localFilePath.find_last_of(L"\\/");
        std::wstring fileName = (lastSlash != std::wstring::npos) ? t.localFilePath.substr(lastSlash + 1) : t.localFilePath;
        std::string fileNameUtf8 = I18nManager::WideToUtf8(fileName);
        std::string remoteTargetFile = pushDest + fileNameUtf8;
        res.remotePath = remoteTargetFile;

        if (progressCb) {
            progressCb(i + 1, total, "転送中: " + t.relativePathForDisplay, 10);
        }

        std::string lastAdbError = "";
        auto onAdbLine = [&](const std::string& line) {
            if (line.find("error:") != std::string::npos || line.find("failed") != std::string::npos || line.find("cannot") != std::string::npos) {
                lastAdbError = line;
            }
            if (!progressCb) return;
            std::regex pctRegex(R"(\[\s*([0-9]+)%\])");
            std::smatch match;
            int pct = 50;
            if (std::regex_search(line, match, pctRegex) && match.size() > 1) {
                pct = std::stoi(match[1].str());
            }
            progressCb(i + 1, total, t.relativePathForDisplay + ": " + line, pct);
        };

        // リモート側はディレクトリ末尾スラッシュではなく、完全なファイルパスを指定
        // (ADBデーモンが親ディレクトリを自動再帰作成して確実に配置するため)
        int pushExit = adb.Push(targetDevice, t.localFilePath, remoteTargetFile, onAdbLine);
        if (pushExit != 0) {
            res.success = false;
            res.errorMessage = lastAdbError.empty() ? ("adb push 終了コード: " + std::to_string(pushExit)) : lastAdbError;
            results.push_back(res);
            continue;
        }

        if (progressCb) {
            progressCb(i + 1, total, "ベリファイ中: " + t.relativePathForDisplay, 95);
        }

        res.sizeMatched = VerifyFile(adb, targetDevice, t.localFilePath, remoteTargetFile, res.localBytes, res.remoteBytes);
        res.success = res.sizeMatched;
        if (!res.sizeMatched) {
            res.errorMessage = "サイズ不一致: Local=" + std::to_string(res.localBytes) +
                               " Remote=" + std::to_string(res.remoteBytes);
        }

        if (progressCb) {
            progressCb(i + 1, total, res.success ? "完了: " + t.relativePathForDisplay : "検証失敗: " + t.relativePathForDisplay, 100);
        }

        results.push_back(res);
    }

    return results;
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
