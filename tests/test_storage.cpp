#include "StorageScanner.h"
#include <cassert>
#include <iostream>

void TestStorageScanner() {
    std::cout << "[TEST] Running TestStorageScanner..." << std::endl;

    // 1. df 出力と storage リストのパーステスト
    std::string mockStorageList =
        "emulated\n"
        "self\n"
        "9C33-6BBD\n";

    std::string mockDf =
        "Filesystem     Size  Used  Avail Use% Mounted on\n"
        "/dev/fuse      115G   31G    84G  27% /storage/emulated\n"
        "/dev/block/vold/public:179,1 238G  56G   182G  24% /storage/9C33-6BBD\n";

    auto storages = AdbTransfer::StorageScanner::ParseMountsAndDf(mockStorageList, mockDf);

    assert(storages.size() == 2);

    // 内蔵ストレージ
    assert(storages[0].id == "internal");
    assert(storages[0].rootPath == "/sdcard");
    assert(!storages[0].isRemovable);
    assert(storages[0].freeSize == "84G");
    assert(storages[0].totalSize == "115G");

    // MicroSD
    assert(storages[1].id == "9C33-6BBD");
    assert(storages[1].rootPath == "/storage/9C33-6BBD");
    assert(storages[1].isRemovable);
    assert(storages[1].freeSize == "182G");
    assert(storages[1].totalSize == "238G");

    // 2. ディレクトリ一覧のパーステスト
    std::string mockLs =
        "/storage/9C33-6BBD/Books/\n"
        "/storage/9C33-6BBD/Movies/\n"
        "/storage/9C33-6BBD/Music/\n";

    auto dirs = AdbTransfer::StorageScanner::ParseDirectoryList(mockLs);
    assert(dirs.size() == 3);
    assert(dirs[0].name == "Books");
    assert(dirs[0].fullPath == "/storage/9C33-6BBD/Books");
    assert(dirs[1].name == "Movies");
    assert(dirs[2].name == "Music");

    std::cout << "[PASS] TestStorageScanner passed!" << std::endl;
}
