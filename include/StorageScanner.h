#pragma once

#include <string>
#include <vector>
#include "AdbManager.h"

namespace AdbTransfer {

struct StorageInfo {
    std::string id;          // "internal" or UUID (例: "9C33-6BBD")
    std::string displayName; // 表示名（例: "[内蔵ストレージ] (空き 84.2 GB)"）
    std::string rootPath;    // リモートパス（例: "/sdcard" または "/storage/9C33-6BBD"）
    std::string totalSize;   // 総容量（例: "128G"）
    std::string freeSize;    // 空き容量（例: "84.2G"）
    bool isRemovable = false;// 外付けSDかどうか
};

struct RemoteDirectoryItem {
    std::string name;       // フォルダ名
    std::string fullPath;   // フルリモートパス
    bool isDirectory = true;
};

class StorageScanner {
public:
    StorageScanner() = default;

    // ストレージ一覧の取得（内蔵 ＋ MicroSD）
    std::vector<StorageInfo> ScanStorages(const AdbManager& adb, const std::string& targetDevice) const;

    // 特定ディレクトリ直下のサブディレクトリ一覧取得
    std::vector<RemoteDirectoryItem> ScanDirectories(const AdbManager& adb,
                                                     const std::string& targetDevice,
                                                     const std::string& remotePath) const;

    // ディレクトリ作成
    bool CreateDirectory(const AdbManager& adb,
                         const std::string& targetDevice,
                         const std::string& remotePath) const;

    // ユニットテスト用パース補助関数
    static std::vector<StorageInfo> ParseMountsAndDf(const std::string& storageListOutput,
                                                    const std::string& dfOutput);
    static std::vector<RemoteDirectoryItem> ParseDirectoryList(const std::string& lsOutput,
                                                               const std::string& parentPath);
};

} // namespace AdbTransfer
