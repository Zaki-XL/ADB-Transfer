#include "TransferEngine.h"
#include "I18nManager.h"
#include <windows.h>
#include <iostream>
#include <regex>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <unordered_set>
#include <memory>

namespace AdbTransfer {

std::string TransferEngine::FormatSpeed(double bytesPerSec) {
    if (bytesPerSec <= 0.0) return "0.0 KB/s";
    const char* units[] = { "B/s", "KB/s", "MB/s", "GB/s" };
    int idx = 0;
    double val = bytesPerSec;
    while (val >= 1024.0 && idx < 3) {
        val /= 1024.0;
        idx++;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f %s", val, units[idx]);
    return std::string(buf);
}

std::string TransferEngine::FormatEta(int etaSeconds) {
    if (etaSeconds < 0) return "--:--";
    if (etaSeconds > 86400) return "> 24h";
    int h = etaSeconds / 3600;
    int m = (etaSeconds % 3600) / 60;
    int s = etaSeconds % 60;
    char buf[64];
    if (h > 0) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }
    return std::string(buf);
}

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
                                        ProgressCallback progressCb,
                                        std::function<bool()> isCanceled) {
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
        progressCb(1, 1, line, pct, 0.0, -1);
    };

    if (progressCb) {
        progressCb(1, 1, "転送中: " + baseNameUtf8, 10, 0.0, -1);
    }

    // フォルダの場合はターゲットディレクトリを事前に作成
    if (isDir) {
        adb.MakeRemoteDirectory(targetDevice, targetRemote);
    }

    std::string pushDest = isDir ? targetRemote + "/" : targetRemote;
    int pushExit = adb.Push(targetDevice, localPath, pushDest, onAdbLine, isCanceled);
    if (isCanceled && isCanceled()) {
        res.success = false;
        res.errorMessage = "Canceled";
        return res;
    }
    if (pushExit != 0) {
        res.success = false;
        res.errorMessage = lastAdbError.empty() ? ("adb push 終了コード: " + std::to_string(pushExit)) : lastAdbError;
        return res;
    }

    if (progressCb) {
        progressCb(1, 1, "ベリファイ中 (整合性照合)...", 95, 0.0, -1);
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
        progressCb(1, 1, res.success ? "完了・ベリファイ成功" : "失敗: " + res.errorMessage, 100, 0.0, -1);
    }

    return res;
}

std::vector<TransferResult> TransferEngine::TransferTasks(const AdbManager& adb,
                                                          const std::string& targetDevice,
                                                          const std::vector<TransferTask>& tasks,
                                                          ProgressCallback progressCb,
                                                          std::function<bool()> isCanceled,
                                                          FileCompleteCallback onFileComplete) {
    std::unordered_set<std::string> createdDirs;
    std::vector<TransferResult> results;
    int total = static_cast<int>(tasks.size());

    uint64_t totalAllBytes = 0;
    for (const auto& t : tasks) {
        totalAllBytes += t.fileSize;
    }

    uint64_t completedBytes = 0;
    double currentSpeed = 0.0;

    struct PendingVerify {
        TransferTask task;
        int taskIndex;
        std::future<TransferResult> fut;
    };
    std::unique_ptr<PendingVerify> pendingVerify;

    auto finishPendingVerify = [&](bool waitIfRunning) {
        if (!pendingVerify) return;
        if (!waitIfRunning && pendingVerify->fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            return;
        }
        TransferResult vRes = pendingVerify->fut.get();
        if (vRes.success) {
            completedBytes += pendingVerify->task.fileSize;
        }

        int finishEta = -1;
        if (currentSpeed > 1024.0 && totalAllBytes > completedBytes) {
            finishEta = static_cast<int>((totalAllBytes - completedBytes) / currentSpeed);
        }

        if (progressCb) {
            progressCb(pendingVerify->taskIndex + 1, total,
                       vRes.success ? ("完了: " + pendingVerify->task.relativePathForDisplay)
                                    : ("検証失敗: " + pendingVerify->task.relativePathForDisplay),
                       100, currentSpeed, finishEta);
        }

        if (onFileComplete) {
            onFileComplete(pendingVerify->task, vRes);
        }

        results.push_back(vRes);
        pendingVerify.reset();
    };

    for (int i = 0; i < total; ++i) {
        if (isCanceled && isCanceled()) {
            break;
        }

        const auto& t = tasks[i];
        TransferResult res;
        res.localPath = t.localFilePath;

        // リモート側ターゲットディレクトリをキャッシュ付きで作成
        if (createdDirs.find(t.remoteTargetDir) == createdDirs.end()) {
            adb.MakeRemoteDirectory(targetDevice, t.remoteTargetDir);
            createdDirs.insert(t.remoteTargetDir);
        }

        std::string pushDest = t.remoteTargetDir;
        if (!pushDest.empty() && pushDest.back() != '/') {
            pushDest += "/";
        }

        size_t lastSlash = t.localFilePath.find_last_of(L"\\/");
        std::wstring fileName = (lastSlash != std::wstring::npos) ? t.localFilePath.substr(lastSlash + 1) : t.localFilePath;
        std::string fileNameUtf8 = I18nManager::WideToUtf8(fileName);
        std::string remoteTargetFile = pushDest + fileNameUtf8;
        res.remotePath = remoteTargetFile;

        // 初期ETA計算（前ファイルまでの平均速度がある場合）
        int initialEta = -1;
        if (currentSpeed > 1024.0 && totalAllBytes > completedBytes) {
            initialEta = static_cast<int>((totalAllBytes - completedBytes) / currentSpeed);
        }

        if (progressCb) {
            progressCb(i + 1, total, "転送中: " + t.relativePathForDisplay, 0, currentSpeed, initialEta);
        }

        std::atomic<bool> isPushing{true};
        auto fileStartTime = std::chrono::steady_clock::now();

        // 転送中ローカル推定タイマースレッド (端末への追加通信完全ゼロ: 0回)
        std::thread timerThread([&]() {
            while (isPushing.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (!isPushing.load()) break;

                // 前ファイルのベリファイが完了していれば非同期回収
                finishPendingVerify(false);

                auto now = std::chrono::steady_clock::now();
                std::chrono::duration<double> fileSec = now - fileStartTime;

                // 速度未定の初期状態（1ファイル目）は標準的なWireless ADB速度(約18MB/s)を仮定
                double activeSpeed = (currentSpeed > 1024.0) ? currentSpeed : (18.0 * 1024.0 * 1024.0);

                // 現在ファイルの推定転送済みバイト数
                uint64_t estFileBytes = static_cast<uint64_t>(fileSec.count() * activeSpeed);
                if (estFileBytes > static_cast<uint64_t>(t.fileSize * 0.95)) {
                    estFileBytes = static_cast<uint64_t>(t.fileSize * 0.95);
                }

                uint64_t estTotalTransferred = completedBytes + estFileBytes;
                int pct = (t.fileSize > 0) ? static_cast<int>((estFileBytes * 100LL) / t.fileSize) : 50;
                if (pct > 95) pct = 95;

                // 残り時間 (ETA)
                int etaSec = -1;
                if (activeSpeed > 1024.0 && totalAllBytes > estTotalTransferred) {
                    uint64_t remainingBytes = totalAllBytes - estTotalTransferred;
                    etaSec = static_cast<int>(remainingBytes / activeSpeed);
                }

                if (progressCb && isPushing.load()) {
                    progressCb(i + 1, total, t.relativePathForDisplay, pct, (currentSpeed > 0 ? currentSpeed : activeSpeed), etaSec);
                }
            }
        });

        std::string lastAdbError = "";
        auto onAdbLine = [&](const std::string& line) {
            if (line.find("error:") != std::string::npos || line.find("failed") != std::string::npos || line.find("cannot") != std::string::npos) {
                lastAdbError = line;
            }

            // adb push 完了メッセージから確定速度・所要時間をパース
            std::regex speedRegex(R"(([0-9]+(?:\.[0-9]+)?)\s*([KMG]?B/s))");
            std::smatch sm;
            if (std::regex_search(line, sm, speedRegex) && sm.size() > 2) {
                try {
                    double val = std::stod(sm[1].str());
                    std::string unit = sm[2].str();
                    double multiplier = 1024.0;
                    if (unit.find("M") != std::string::npos) multiplier = 1024.0 * 1024.0;
                    else if (unit.find("G") != std::string::npos) multiplier = 1024.0 * 1024.0 * 1024.0;
                    else if (unit == "B/s") multiplier = 1.0;
                    double reportedSpeed = val * multiplier;
                    if (reportedSpeed > 0.0) {
                        currentSpeed = (currentSpeed <= 0.0) ? reportedSpeed : (reportedSpeed * 0.6 + currentSpeed * 0.4);
                    }
                } catch (...) {}
            }
        };

        // リモート側はディレクトリ末尾スラッシュではなく、完全なファイルパスを指定
        int pushExit = adb.Push(targetDevice, t.localFilePath, remoteTargetFile, onAdbLine, isCanceled);

        // 推定タイマースレッド停止
        isPushing.store(false);
        if (timerThread.joinable()) {
            timerThread.join();
        }

        // 前ファイルのベリファイがまだ未回収ならここで確実に待機・回収
        finishPendingVerify(true);

        // 実測転送時間から確定速度をバックアップ計算
        auto fileEndTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> filePushSec = fileEndTime - fileStartTime;
        if (filePushSec.count() > 0.01 && t.fileSize > 0) {
            double actualSpeed = static_cast<double>(t.fileSize) / filePushSec.count();
            if (actualSpeed > 1024.0) {
                currentSpeed = (currentSpeed <= 0.0) ? actualSpeed : (actualSpeed * 0.5 + currentSpeed * 0.5);
            }
        }

        if (isCanceled && isCanceled()) {
            res.success = false;
            res.errorMessage = "Canceled";
            results.push_back(res);
            break;
        }

        if (pushExit != 0) {
            res.success = false;
            res.errorMessage = lastAdbError.empty() ? ("adb push 終了コード: " + std::to_string(pushExit)) : lastAdbError;
            results.push_back(res);
            continue;
        }

        // ベリファイ中のETA計算
        int verifyEta = -1;
        uint64_t afterPushTransferred = completedBytes + t.fileSize;
        if (currentSpeed > 1024.0 && totalAllBytes > afterPushTransferred) {
            verifyEta = static_cast<int>((totalAllBytes - afterPushTransferred) / currentSpeed);
        }

        if (progressCb) {
            progressCb(i + 1, total, "ベリファイ中: " + t.relativePathForDisplay, 95, currentSpeed, verifyEta);
        }

        // ベリファイを非同期に起動（次ファイルのPushと並行処理）
        pendingVerify = std::make_unique<PendingVerify>();
        pendingVerify->task = t;
        pendingVerify->taskIndex = i;
        pendingVerify->fut = std::async(std::launch::async, [&adb, targetDevice, t, remoteTargetFile, res]() mutable {
            res.sizeMatched = VerifyFile(adb, targetDevice, t.localFilePath, remoteTargetFile, res.localBytes, res.remoteBytes);
            res.success = res.sizeMatched;
            if (!res.sizeMatched) {
                res.errorMessage = "サイズ不一致: Local=" + std::to_string(res.localBytes) +
                                   " Remote=" + std::to_string(res.remoteBytes);
            }
            return res;
        });

        if (isCanceled && isCanceled()) {
            break;
        }
    }

    // 全ループ終了後、最後の保留中ベリファイを待機・回収
    finishPendingVerify(true);

    return results;

    return results;
}

std::vector<TransferResult> TransferEngine::TransferBatch(const AdbManager& adb,
                                                          const std::string& targetDevice,
                                                          const std::vector<std::wstring>& localPaths,
                                                          const std::string& remoteDir,
                                                          ProgressCallback progressCb,
                                                          std::function<bool()> isCanceled) {
    std::vector<TransferResult> results;
    int total = static_cast<int>(localPaths.size());

    for (int i = 0; i < total; ++i) {
        if (isCanceled && isCanceled()) break;

        auto cb = [&](int, int, const std::string& msg, int pct, double spd, int eta) {
            if (progressCb) {
                progressCb(i + 1, total, msg, pct, spd, eta);
            }
        };

        TransferResult r = Transfer(adb, targetDevice, localPaths[i], remoteDir, cb, isCanceled);
        results.push_back(r);
    }

    return results;
}

} // namespace AdbTransfer
