#include "MainWindow_Internal.h"
#include <thread>

namespace AdbTransfer {

static void CalculateItemSize(const std::wstring& path, uint64_t& outBytes, int& outFileCount) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        std::wstring searchPath = path + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                    continue;
                }
                std::wstring fullSubPath = path + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    CalculateItemSize(fullSubPath, outBytes, outFileCount);
                } else {
                    LARGE_INTEGER size;
                    size.LowPart = fd.nFileSizeLow;
                    size.HighPart = fd.nFileSizeHigh;
                    outBytes += size.QuadPart;
                    outFileCount++;
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    } else {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            LARGE_INTEGER size;
            size.LowPart = fad.nFileSizeLow;
            size.HighPart = fad.nFileSizeHigh;
            outBytes += size.QuadPart;
            outFileCount++;
        }
    }
}

static std::wstring FormatFileSize(uint64_t bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double size = static_cast<double>(bytes);
    int unitIdx = 0;
    while (size >= 1024.0 && unitIdx < 4) {
        size /= 1024.0;
        unitIdx++;
    }
    wchar_t buf[64];
    if (unitIdx == 0) {
        swprintf_s(buf, L"%llu B", bytes);
    } else {
        swprintf_s(buf, L"%.2f %s", size, units[unitIdx]);
    }
    return buf;
}

void MainWindow::UpdateTransferSummary() {
    int totalCheckedFiles = 0;
    uint64_t totalCheckedBytes = 0;

    int count = ListView_GetItemCount(m_hTransferList);
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(m_hTransferList, i)) {
            if (i < static_cast<int>(m_transferItems.size())) {
                totalCheckedFiles += m_transferItems[i].fileCount;
                totalCheckedBytes += m_transferItems[i].totalBytes;
            }
        }
    }

    std::map<std::string, std::wstring> params = {
        {"count", std::to_wstring(totalCheckedFiles)},
        {"size", FormatFileSize(totalCheckedBytes)}
    };
    std::wstring summary = tW("transfer.summary_format", params);
    SetWindowTextW(m_hTransferSummaryText, summary.c_str());
}

void MainWindow::OnAddPreset() {
    if (!m_connState.isConnected || m_isConnectingOrScanning) {
        return;
    }
    if (m_selectedRemotePath.empty()) {
        MessageBoxW(m_hwnd, tW("preset.warn_select_folder").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (m_config->GetPresets().size() >= 8) {
        MessageBoxW(m_hwnd, tW("preset.warn_max_presets").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    std::string name = m_selectedRemotePath;
    size_t lastSlash = name.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash + 1 < name.length()) {
        name = name.substr(lastSlash + 1);
    }
    if (m_selectedRemotePath.find("/storage/") != std::string::npos) {
        name = t("preset.prefix_sd") + name;
    } else {
        name = t("preset.prefix_internal") + name;
    }

    size_t prevCount = m_config->GetPresets().size();
    m_config->AddPreset(name, m_selectedRemotePath);
    if (m_config->GetPresets().size() == prevCount) {
        MessageBoxW(m_hwnd, tW("preset.warn_already_exists").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    m_config->Save(L"");
    RefreshPresetButtons();
    MessageBoxW(m_hwnd, tW("preset.success_added").c_str(), tW("progress.ready").c_str(), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::OnPresetRightClick(size_t index) {
    if (!m_connState.isConnected || m_isConnectingOrScanning) return;

    const auto& presets = m_config->GetPresets();
    if (index >= presets.size()) return;

    HMENU hMenu = CreatePopupMenu();
    std::wstring menuText = tW("preset.delete_title") + L" (" + Utf8ToUtf16(presets[index].name) + L")";
    AppendMenuW(hMenu, MF_STRING, 1, menuText.c_str());

    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (cmd == 1) {
        std::map<std::string, std::wstring> params = { {"name", Utf8ToUtf16(presets[index].name)} };
        std::wstring confirmMsg = tW("preset.delete_confirm", params);
        if (MessageBoxW(m_hwnd, confirmMsg.c_str(), tW("preset.delete_title").c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
            m_config->RemovePreset(index);
            m_config->Save(L"");
            RefreshPresetButtons();
        }
    }
}

void MainWindow::OnDropFiles(HDROP hDrop) {
    if (!m_connState.isConnected || m_isConnectingOrScanning) {
        DragFinish(hDrop);
        return;
    }
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH * 2] = {0};
        if (DragQueryFileW(hDrop, i, path, ARRAYSIZE(path)) > 0) {
            TransferItemInfo item;
            item.localPath = path;
            DWORD attr = GetFileAttributesW(path);
            item.isDirectory = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
            CalculateItemSize(path, item.totalBytes, item.fileCount);
            m_transferItems.push_back(item);

            std::wstring fullPath = path;
            size_t lastSlash = fullPath.find_last_of(L"\\/");
            std::wstring fileName = (lastSlash != std::wstring::npos) ? fullPath.substr(lastSlash + 1) : fullPath;
            std::wstring parentDir = (lastSlash != std::wstring::npos) ? fullPath.substr(0, lastSlash) : L"";
            std::wstring displayText = fileName;
            if (item.isDirectory) {
                displayText += L" [フォルダ]";
            }
            if (!parentDir.empty()) {
                displayText += L" (" + parentDir + L")";
            }

            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(m_hTransferList);
            lvi.pszText = const_cast<wchar_t*>(displayText.c_str());
            int itemIdx = ListViewInsertItemW(m_hTransferList, &lvi);
            ListView_SetCheckState(m_hTransferList, itemIdx, TRUE);
        }
    }
    DragFinish(hDrop);

    UpdateTransferSummary();
    SetProgressStatus("progress.items_added", {{"count", std::to_wstring(ListView_GetItemCount(m_hTransferList))}});

    InvalidateRect(m_hTransferList, nullptr, TRUE);
    UpdateWindow(m_hTransferList);
}

void MainWindow::OnRemoveChecked() {
    int count = ListView_GetItemCount(m_hTransferList);
    int removedCount = 0;
    for (int i = count - 1; i >= 0; --i) {
        if (ListView_GetCheckState(m_hTransferList, i)) {
            ListView_DeleteItem(m_hTransferList, i);
            if (i < static_cast<int>(m_transferItems.size())) {
                m_transferItems.erase(m_transferItems.begin() + i);
            }
            removedCount++;
        }
    }

    UpdateTransferSummary();
    SetProgressStatus("progress.items_removed", {{"removed", std::to_wstring(removedCount)}, {"remain", std::to_wstring(m_transferItems.size())}});
}

void MainWindow::OnClearTransferList() {
    m_transferItems.clear();
    ListView_DeleteAllItems(m_hTransferList);
    UpdateTransferSummary();
    SendMessageW(m_hProgressBar, PBM_SETPOS, 0, 0);
    SetProgressStatus("progress.list_cleared");
}

void MainWindow::OnStartTransfer() {
    if (m_isTransferring) {
        MessageBoxW(m_hwnd, tW("messages.warn_no_files_msg").c_str(), tW("messages.warn_no_files_title").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }
    if (!m_connState.isConnected) {
        MessageBoxW(m_hwnd, tW("header.status_disconnected").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    if (m_selectedRemotePath.empty()) {
        MessageBoxW(m_hwnd, tW("messages.warn_no_dest_msg").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    std::vector<std::wstring> filesToTransfer;
    uint64_t totalRequiredBytes = 0;
    int count = ListView_GetItemCount(m_hTransferList);
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(m_hTransferList, i)) {
            if (i < static_cast<int>(m_transferItems.size())) {
                filesToTransfer.push_back(m_transferItems[i].localPath);
                totalRequiredBytes += m_transferItems[i].totalBytes;
            }
        }
    }

    if (filesToTransfer.empty()) {
        MessageBoxW(m_hwnd, tW("messages.warn_no_files_msg").c_str(), tW("messages.warn_no_files_title").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    std::string remoteDir = m_selectedRemotePath;
    std::string targetDevice = m_connState.targetSerial;

    int64_t availableBytes = m_adb->GetRemoteAvailableSpace(targetDevice, remoteDir);
    if (availableBytes >= 0 && totalRequiredBytes > static_cast<uint64_t>(availableBytes)) {
        std::map<std::string, std::wstring> params = {
            {"required", FormatFileSize(totalRequiredBytes)},
            {"available", FormatFileSize(static_cast<uint64_t>(availableBytes))}
        };
        std::wstring msg = tW("messages.warn_low_space_msg", params);
        MessageBoxW(m_hwnd, msg.c_str(), tW("messages.warn_low_space_title").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    m_isTransferring = true;
    EnableWindow(m_hBtnStartTransfer, FALSE);
    EnableWindow(m_hBtnRemoveChecked, FALSE);
    EnableWindow(m_hBtnClearTransfer, FALSE);

    std::thread([this, filesToTransfer, remoteDir, targetDevice]() {
        auto results = m_engine->TransferBatch(
            *m_adb, targetDevice, filesToTransfer, remoteDir,
            [this](int cur, int total, const std::string& msg, int pct) {
                struct ProgData {
                    int percent;
                    std::string message;
                };
                int overallPct = (total > 0) ? static_cast<int>(((cur - 1) * 100LL + pct) / total) : pct;
                if (overallPct > 100) overallPct = 100;
                if (overallPct < 0) overallPct = 0;
                auto pProg = new ProgData{overallPct, "[" + std::to_string(cur) + "/" + std::to_string(total) + "] " + msg};
                PostMessageW(m_hwnd, WM_APP_TRANSFER_PROGRESS, (WPARAM)pProg, 0);
            });

        auto pRes = new std::vector<TransferResult>(std::move(results));
        PostMessageW(m_hwnd, WM_APP_TRANSFER_COMPLETE, (WPARAM)pRes, 0);
    }).detach();
}

} // namespace AdbTransfer
