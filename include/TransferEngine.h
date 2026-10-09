#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "AdbManager.h"

namespace AdbTransfer {

struct TransferItem {
    std::wstring localPath;
    std::string remoteTargetPath;
    bool isDirectory = false;
    int64_t localSize = 0;
};

struct TransferResult {
    bool success = false;
    std::wstring localPath;
    std::string remotePath;
    int64_t localBytes = 0;
    int64_t remoteBytes = 0;
    bool sizeMatched = false;
    std::string errorMessage;
};

using ProgressCallback = std::function<void(int currentItem, int totalItems, const std::string& statusMsg, int percent)>;

class TransferEngine {
public:
    TransferEngine() = default;

    // ファイルまたはフォルダの転送実行とベリファイ
    TransferResult Transfer(const AdbManager& adb,
                            const std::string& targetDevice,
                            const std::wstring& localPath,
                            const std::string& remoteDir,
                            ProgressCallback progressCb = nullptr);

    // 複数アイテムの一括転送
    std::vector<TransferResult> TransferBatch(const AdbManager& adb,
                                              const std::string& targetDevice,
                                              const std::vector<std::wstring>& localPaths,
                                              const std::string& remoteDir,
                                              ProgressCallback progressCb = nullptr);

    // ローカルファイルサイズ取得 (Windows API)
    static int64_t GetLocalFileSize(const std::wstring& filePath);
    static bool IsDirectory(const std::wstring& path);

    // リモートファイルサイズとローカルファイルサイズのベリファイ照合
    static bool VerifyFile(const AdbManager& adb,
                           const std::string& targetDevice,
                           const std::wstring& localFilePath,
                           const std::string& remoteFilePath,
                           int64_t& outLocalSize,
                           int64_t& outRemoteSize);

    // フォルダ内のローカルファイル一覧を再帰取得
    static std::vector<std::wstring> CollectLocalFiles(const std::wstring& folderPath);
};

} // namespace AdbTransfer
