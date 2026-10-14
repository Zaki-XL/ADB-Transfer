#include "TransferEngine.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <windows.h>

void TestTransferEngine() {
    std::cout << "[TEST] Running TestTransferEngine..." << std::endl;

    std::wstring testFile = L"scratch\\test_file.bin";
    std::ofstream ofs(testFile.c_str(), std::ios::binary);
    std::string dummyData = "Hello, ADB Transfer! 1234567890\n";
    ofs.write(dummyData.c_str(), dummyData.length());
    ofs.close();

    // 1. ファイルサイズ取得テスト
    int64_t size = AdbTransfer::TransferEngine::GetLocalFileSize(testFile);
    assert(size == static_cast<int64_t>(dummyData.length()));

    // 2. ディレクトリ判定テスト
    assert(!AdbTransfer::TransferEngine::IsDirectory(testFile));
    assert(AdbTransfer::TransferEngine::IsDirectory(L"scratch"));

    // 3. ローカルファイル一覧再帰取得テスト
    auto files = AdbTransfer::TransferEngine::CollectLocalFiles(L"scratch");
    assert(!files.empty());

    // 4. FormatSpeed & FormatEta テスト
    assert(AdbTransfer::TransferEngine::FormatSpeed(0.0) == "0.0 KB/s");
    assert(AdbTransfer::TransferEngine::FormatSpeed(1024.0) == "1.0 KB/s");
    assert(AdbTransfer::TransferEngine::FormatSpeed(15.5 * 1024.0 * 1024.0) == "15.5 MB/s");
    assert(AdbTransfer::TransferEngine::FormatEta(-1) == "--:--");
    assert(AdbTransfer::TransferEngine::FormatEta(65) == "01:05");
    assert(AdbTransfer::TransferEngine::FormatEta(3665) == "01:01:05");

    DeleteFileW(testFile.c_str());
    std::cout << "[PASS] TestTransferEngine passed!" << std::endl;
}
