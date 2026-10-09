#include "StorageScanner.h"
#include <sstream>
#include <regex>
#include <map>

namespace AdbTransfer {

std::vector<StorageInfo> StorageScanner::ParseMountsAndDf(const std::string& storageListOutput,
                                                          const std::string& dfOutput) {
    std::vector<StorageInfo> results;

    // 1. df 出力からマウントポイントごとのサイズ情報を抽出
    // 例:
    // Filesystem     Size  Used  Avail Use% Mounted on
    // /dev/fuse      115G   31G    84G  27% /storage/emulated
    // /dev/block/vold/public:179,1 238G  56G   182G  24% /mnt/media_rw/9C33-6BBD
    struct DfEntry {
        std::string total;
        std::string free;
    };
    std::map<std::string, DfEntry> dfMap;

    std::istringstream dfStream(dfOutput);
    std::string line;
    while (std::getline(dfStream, line)) {
        std::istringstream ls(line);
        std::string fs, size, used, avail, usePct, mount;
        if (ls >> fs >> size >> used >> avail >> usePct >> mount) {
            dfMap[mount] = {size, avail};
        }
    }

    // 2. 内蔵ストレージのエントリ作成
    StorageInfo internalStorage;
    internalStorage.id = "internal";
    internalStorage.rootPath = "/sdcard";
    internalStorage.isRemovable = false;
    
    // /storage/emulated または /data または /sdcard の空き容量を探す
    if (dfMap.count("/storage/emulated")) {
        internalStorage.totalSize = dfMap["/storage/emulated"].total;
        internalStorage.freeSize = dfMap["/storage/emulated"].free;
    } else if (dfMap.count("/data")) {
        internalStorage.totalSize = dfMap["/data"].total;
        internalStorage.freeSize = dfMap["/data"].free;
    } else {
        internalStorage.totalSize = "Unknown";
        internalStorage.freeSize = "Unknown";
    }
    internalStorage.displayName = "[内蔵ストレージ] (空き " + internalStorage.freeSize + ")";
    results.push_back(internalStorage);

    // 3. /storage 配下のUUID（MicroSD）を走査
    // storageListOutput: "emulated\nself\n9C33-6BBD\n"
    std::istringstream storageStream(storageListOutput);
    std::regex uuidRegex(R"([0-9A-Fa-f]{4}-[0-9A-Fa-f]{4})");

    while (std::getline(storageStream, line)) {
        // 末尾改行除去
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        if (std::regex_match(line, uuidRegex)) {
            StorageInfo sd;
            sd.id = line;
            sd.rootPath = "/storage/" + line;
            sd.isRemovable = true;

            std::string sdTotal = "Unknown", sdFree = "Unknown";
            // /storage/UUID または /mnt/media_rw/UUID を検索
            if (dfMap.count(sd.rootPath)) {
                sdTotal = dfMap[sd.rootPath].total;
                sdFree = dfMap[sd.rootPath].free;
            } else {
                for (const auto& [mount, entry] : dfMap) {
                    if (mount.find(line) != std::string::npos) {
                        sdTotal = entry.total;
                        sdFree = entry.free;
                        break;
                    }
                }
            }

            sd.totalSize = sdTotal;
            sd.freeSize = sdFree;
            sd.displayName = "[MicroSDカード (" + line + ")] (空き " + sdFree + ")";
            results.push_back(sd);
        }
    }

    return results;
}

std::vector<RemoteDirectoryItem> StorageScanner::ParseDirectoryList(const std::string& lsOutput,
                                                                   const std::string& parentPath) {
    std::vector<RemoteDirectoryItem> items;
    std::istringstream stream(lsOutput);
    std::string line;

    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == '/')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        // フルパスからフォルダ名を抽出
        size_t lastSlash = line.find_last_of('/');
        std::string name = (lastSlash != std::string::npos) ? line.substr(lastSlash + 1) : line;

        // ドットから始まる隠しフォルダ（.android_secureなど）は必要に応じて除外するか保持
        if (name == "." || name == "..") continue;

        RemoteDirectoryItem item;
        item.name = name;
        item.fullPath = line;
        item.isDirectory = true;
        items.push_back(item);
    }
    return items;
}

std::vector<StorageInfo> StorageScanner::ScanStorages(const AdbManager& adb,
                                                      const std::string& targetDevice) const {
    // 1. /storage 配下のディレクトリ一覧取得
    std::string storageList = adb.ShellExec(targetDevice, "ls -1 /storage 2>/dev/null");
    // 2. df -h 取得
    std::string dfOut = adb.ShellExec(targetDevice, "df -h 2>/dev/null");

    return ParseMountsAndDf(storageList, dfOut);
}

std::vector<RemoteDirectoryItem> StorageScanner::ScanDirectories(const AdbManager& adb,
                                                                 const std::string& targetDevice,
                                                                 const std::string& remotePath) const {
    // Android (toybox/toolbox) で確実にサブディレクトリのみを取得
    // 1. find コマンド
    std::string cmd = "find \"" + remotePath + "\" -maxdepth 1 -mindepth 1 -type d 2>/dev/null";
    std::string out = adb.ShellExec(targetDevice, cmd);

    // 2. 空の場合は for ループでフォールバック
    if (out.empty()) {
        cmd = "for d in \"" + remotePath + "\"/*; do [ -d \"$d\" ] && echo \"$d\"; done";
        out = adb.ShellExec(targetDevice, cmd);
    }

    return ParseDirectoryList(out, remotePath);
}

bool StorageScanner::CreateDirectory(const AdbManager& adb,
                                     const std::string& targetDevice,
                                     const std::string& remotePath) const {
    return adb.MakeRemoteDirectory(targetDevice, remotePath);
}

} // namespace AdbTransfer
