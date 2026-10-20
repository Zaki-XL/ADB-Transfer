#include "MainWindow_Internal.h"
#include <thread>
#include <algorithm>

namespace AdbTransfer {

static void ScanDirectoryRecursive(const std::wstring& rootDir,
                                    const std::wstring& curDir,
                                    std::vector<MainWindow::SubFileInfo>& outFiles,
                                    uint64_t& outBytes) {
    std::wstring searchPattern = curDir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring fullPath = curDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanDirectoryRecursive(rootDir, fullPath, outFiles, outBytes);
        } else {
            LARGE_INTEGER sz;
            sz.LowPart = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            outBytes += sz.QuadPart;

            MainWindow::SubFileInfo sub;
            sub.localPath = fullPath;
            sub.relativePath = fullPath.substr(rootDir.length() + 1);
            sub.fileSize = sz.QuadPart;
            sub.isChecked = true;
            outFiles.push_back(sub);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

void MainWindow::AddTransferItem(const std::wstring& path) {
    // 重複チェック
    for (const auto& existing : m_transferItems) {
        if (_wcsicmp(existing.localPath.c_str(), path.c_str()) == 0) {
            return;
        }
    }

    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;

    TransferItemInfo item;
    item.localPath = path;
    item.isDirectory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    size_t lastSlash = path.find_last_of(L"\\/");
    item.folderName = (lastSlash != std::wstring::npos) ? path.substr(lastSlash + 1) : path;
    item.isExpanded = false;
    item.isChecked = true;

    if (item.isDirectory) {
        uint64_t totalBytes = 0;
        ScanDirectoryRecursive(path, path, item.subFiles, totalBytes);
        item.fileCount = static_cast<int>(item.subFiles.size());
        item.totalBytes = totalBytes;
    } else {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            LARGE_INTEGER sz;
            sz.LowPart = fad.nFileSizeLow;
            sz.HighPart = fad.nFileSizeHigh;
            item.totalBytes = sz.QuadPart;
        }
        item.fileCount = 1;

        SubFileInfo sub;
        sub.localPath = path;
        sub.relativePath = item.folderName;
        sub.fileSize = item.totalBytes;
        sub.isChecked = true;
        item.subFiles.push_back(sub);
    }

    m_transferItems.push_back(item);
}

void MainWindow::RefreshTransferListView() {
    m_isUpdatingListChecks = true;

    // 現在のスクロール位置（一番上の表示行インデックス）を保持
    int topIndex = ListView_GetTopIndex(m_hTransferList);
    // 現在フォーカスされている行の rootIndex / subFileIndex を特定
    int focusedRow = ListView_GetNextItem(m_hTransferList, -1, LVNI_FOCUSED);
    size_t focusedRoot = (size_t)-1;
    int focusedSub = -1;
    if (focusedRow >= 0 && focusedRow < static_cast<int>(m_listRows.size())) {
        focusedRoot = m_listRows[focusedRow].rootIndex;
        focusedSub = m_listRows[focusedRow].subFileIndex;
    }

    // 再描画のチラつき防止
    SendMessageW(m_hTransferList, WM_SETREDRAW, FALSE, 0);

    ListView_DeleteAllItems(m_hTransferList);
    m_listRows.clear();

    for (size_t i = 0; i < m_transferItems.size(); ++i) {
        auto& item = m_transferItems[i];
        if (item.isDirectory) {
            // 親フォルダ行: 未展開なら "> ", 展開中なら "v "
            std::wstring prefix = item.isExpanded ? L"v " : L"> ";
            std::wstring displayText = prefix + item.folderName + L" (" +
                std::to_wstring(item.fileCount) + L"項目, " + FormatFileSize(item.totalBytes) + L")";

            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(m_hTransferList);
            lvi.pszText = const_cast<wchar_t*>(displayText.c_str());
            int rowIdx = ListViewInsertItemW(m_hTransferList, &lvi);

            m_listRows.push_back({i, -1});
            ListView_SetCheckState(m_hTransferList, rowIdx, item.isChecked);

            if (item.isExpanded) {
                // サブファイルをインデント表示
                for (size_t j = 0; j < item.subFiles.size(); ++j) {
                    const auto& sf = item.subFiles[j];
                    std::wstring subText = L"      " + sf.relativePath + L" (" + FormatFileSize(sf.fileSize) + L")";

                    LVITEMW subLvi{};
                    subLvi.mask = LVIF_TEXT;
                    subLvi.iItem = ListView_GetItemCount(m_hTransferList);
                    subLvi.pszText = const_cast<wchar_t*>(subText.c_str());
                    int subRowIdx = ListViewInsertItemW(m_hTransferList, &subLvi);

                    m_listRows.push_back({i, static_cast<int>(j)});
                    ListView_SetCheckState(m_hTransferList, subRowIdx, sf.isChecked);
                }
            }
        } else {
            // 単一ファイル行
            std::wstring displayText = item.folderName + L" (" + FormatFileSize(item.totalBytes) + L")";

            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(m_hTransferList);
            lvi.pszText = const_cast<wchar_t*>(displayText.c_str());
            int rowIdx = ListViewInsertItemW(m_hTransferList, &lvi);

            m_listRows.push_back({i, -1});
            ListView_SetCheckState(m_hTransferList, rowIdx, item.isChecked);
        }
    }

    // フォーカスと選択状態の復元
    int newFocusRow = -1;
    if (focusedRoot != (size_t)-1) {
        for (size_t r = 0; r < m_listRows.size(); ++r) {
            if (m_listRows[r].rootIndex == focusedRoot &&
                (m_listRows[r].subFileIndex == focusedSub || (focusedSub != -1 && m_listRows[r].subFileIndex == -1))) {
                newFocusRow = static_cast<int>(r);
                break;
            }
        }
    }

    if (newFocusRow >= 0) {
        ListView_SetItemState(m_hTransferList, newFocusRow, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
    }

    // スクロール位置の復元
    if (topIndex > 0) {
        // 現在の最上部アイテムとの差分ピクセルだけスクロール
        RECT rcTopItem{};
        if (ListView_GetItemRect(m_hTransferList, 0, &rcTopItem, LVIR_BOUNDS)) {
            int itemHeight = rcTopItem.bottom - rcTopItem.top;
            if (itemHeight > 0) {
                int totalRows = static_cast<int>(m_listRows.size());
                int targetTop = (topIndex < totalRows) ? topIndex : (totalRows - 1);
                if (targetTop > 0) {
                    ListView_Scroll(m_hTransferList, 0, targetTop * itemHeight);
                }
            }
        }
    }

    SendMessageW(m_hTransferList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_hTransferList, nullptr, TRUE);

    m_isUpdatingListChecks = false;
    UpdateTransferSummary();
}

void MainWindow::OnTransferListClick(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= static_cast<int>(m_listRows.size())) return;

    const auto& row = m_listRows[itemIndex];
    if (row.subFileIndex == -1 && row.rootIndex < m_transferItems.size()) {
        auto& item = m_transferItems[row.rootIndex];
        if (item.isDirectory) {
            // フォルダの展開 / 折りたたみをトグル
            item.isExpanded = !item.isExpanded;
            RefreshTransferListView();

            // 開閉したフォルダ項目を可視領域内に確実に表示
            for (size_t r = 0; r < m_listRows.size(); ++r) {
                if (m_listRows[r].rootIndex == row.rootIndex && m_listRows[r].subFileIndex == -1) {
                    ListView_EnsureVisible(m_hTransferList, static_cast<int>(r), FALSE);
                    ListView_SetItemState(m_hTransferList, static_cast<int>(r), LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
                    break;
                }
            }
        }
    }
}

void MainWindow::OnTransferListItemChanged(NMLISTVIEW* nmlv) {
    if (m_isUpdatingListChecks) return;
    if (!(nmlv->uChanged & LVIF_STATE)) return;

    int oldState = (nmlv->uOldState >> 12) & 15;
    int newState = (nmlv->uNewState >> 12) & 15;
    if (oldState == newState || newState == 0) return;

    bool isChecked = (newState == 2);
    int rowIdx = nmlv->iItem;
    if (rowIdx < 0 || rowIdx >= static_cast<int>(m_listRows.size())) return;

    auto row = m_listRows[rowIdx];
    if (row.rootIndex >= m_transferItems.size()) return;
    auto& rootItem = m_transferItems[row.rootIndex];

    if (row.subFileIndex == -1) {
        // 親フォルダ/ルートファイルのチェック変更
        rootItem.isChecked = isChecked;
        for (auto& sf : rootItem.subFiles) {
            sf.isChecked = isChecked;
        }

        // 展開中の子行のチェックボックス表示を連動
        m_isUpdatingListChecks = true;
        for (size_t r = 0; r < m_listRows.size(); ++r) {
            if (m_listRows[r].rootIndex == row.rootIndex && m_listRows[r].subFileIndex >= 0) {
                ListView_SetCheckState(m_hTransferList, r, isChecked);
            }
        }
        m_isUpdatingListChecks = false;
    } else {
        // 個別子ファイルのチェック変更
        if (row.subFileIndex < static_cast<int>(rootItem.subFiles.size())) {
            rootItem.subFiles[row.subFileIndex].isChecked = isChecked;
        }

        bool anyChecked = false;
        for (const auto& sf : rootItem.subFiles) {
            if (sf.isChecked) anyChecked = true;
        }
        rootItem.isChecked = anyChecked;

        // 親行のチェックボックス表示を連動
        m_isUpdatingListChecks = true;
        for (size_t r = 0; r < m_listRows.size(); ++r) {
            if (m_listRows[r].rootIndex == row.rootIndex && m_listRows[r].subFileIndex == -1) {
                ListView_SetCheckState(m_hTransferList, r, rootItem.isChecked);
                break;
            }
        }
        m_isUpdatingListChecks = false;
    }

    UpdateTransferSummary();
}

void MainWindow::UpdateTransferSummary() {
    int totalCheckedFiles = 0;
    uint64_t totalCheckedBytes = 0;

    for (const auto& item : m_transferItems) {
        for (const auto& sf : item.subFiles) {
            if (sf.isChecked) {
                totalCheckedFiles++;
                totalCheckedBytes += sf.fileSize;
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

void MainWindow::SaveTransferListToConfig() {
    if (!m_config) return;
    std::vector<std::wstring> paths;
    for (const auto& item : m_transferItems) {
        paths.push_back(item.localPath);
    }
    m_config->SetTransferPaths(paths);
    m_config->Save(L"");
}

void MainWindow::LoadTransferListFromConfig() {
    if (!m_config) return;
    const auto& paths = m_config->GetTransferPaths();
    for (const auto& p : paths) {
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            AddTransferItem(p);
        }
    }
    RefreshTransferListView();
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

    for (const auto& p : m_config->GetPresets()) {
        if (p.remotePath == m_selectedRemotePath) {
            MessageBoxW(m_hwnd, tW("preset.warn_already_exists").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    std::string prefix = "";
    if (m_selectedRemotePath.find("/sdcard") == 0) {
        prefix = I18nManager::WideToUtf8(tW("preset.prefix_internal"));
    } else if (m_selectedRemotePath.find("/storage/") == 0) {
        prefix = I18nManager::WideToUtf8(tW("preset.prefix_sd"));
    }

    m_config->AddPreset(prefix + name, m_selectedRemotePath);
    m_config->Save(L"");
    RefreshPresetButtons();
    MessageBoxW(m_hwnd, tW("preset.success_added").c_str(), tW("messages.register_success_title").c_str(), MB_OK | MB_ICONINFORMATION);
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
        if (ShowCenteredMessageBox(confirmMsg, tW("preset.delete_title"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
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
            AddTransferItem(path);
        }
    }
    DragFinish(hDrop);

    RefreshTransferListView();
    SaveTransferListToConfig();

    SetProgressStatus("progress.items_added", {{"count", std::to_wstring(m_transferItems.size())}});
    InvalidateRect(m_hTransferList, nullptr, TRUE);
    UpdateWindow(m_hTransferList);
}

void MainWindow::OnRemoveChecked() {
    int removedCount = 0;
    for (int i = static_cast<int>(m_transferItems.size()) - 1; i >= 0; --i) {
        auto& item = m_transferItems[i];
        if (item.isChecked) {
            m_transferItems.erase(m_transferItems.begin() + i);
            removedCount++;
        } else {
            // 子ファイルの一部がチェックされている場合
            for (int j = static_cast<int>(item.subFiles.size()) - 1; j >= 0; --j) {
                if (item.subFiles[j].isChecked) {
                    item.subFiles.erase(item.subFiles.begin() + j);
                    removedCount++;
                }
            }
            if (item.subFiles.empty()) {
                m_transferItems.erase(m_transferItems.begin() + i);
            } else {
                item.fileCount = static_cast<int>(item.subFiles.size());
                uint64_t newTotal = 0;
                for (const auto& sf : item.subFiles) newTotal += sf.fileSize;
                item.totalBytes = newTotal;
            }
        }
    }

    RefreshTransferListView();
    SaveTransferListToConfig();
    SetProgressStatus("progress.items_removed", {{"removed", std::to_wstring(removedCount)}, {"remain", std::to_wstring(m_transferItems.size())}});
}

void MainWindow::OnClearTransferList() {
    m_transferItems.clear();
    m_listRows.clear();
    ListView_DeleteAllItems(m_hTransferList);
    SaveTransferListToConfig();
    UpdateTransferSummary();
    SendMessageW(m_hProgressBar, PBM_SETPOS, 0, 0);
    SetProgressStatus("progress.list_cleared");
}

void MainWindow::RemoveCompletedFile(const std::wstring& localPath) {
    for (int i = static_cast<int>(m_transferItems.size()) - 1; i >= 0; --i) {
        auto& item = m_transferItems[i];
        if (!item.isDirectory) {
            if (_wcsicmp(item.localPath.c_str(), localPath.c_str()) == 0) {
                m_transferItems.erase(m_transferItems.begin() + i);
                break;
            }
        } else {
            bool found = false;
            for (int j = static_cast<int>(item.subFiles.size()) - 1; j >= 0; --j) {
                if (_wcsicmp(item.subFiles[j].localPath.c_str(), localPath.c_str()) == 0) {
                    item.subFiles.erase(item.subFiles.begin() + j);
                    found = true;
                    break;
                }
            }
            if (found) {
                if (item.subFiles.empty()) {
                    m_transferItems.erase(m_transferItems.begin() + i);
                } else {
                    item.fileCount = static_cast<int>(item.subFiles.size());
                    uint64_t newTotal = 0;
                    for (const auto& sf : item.subFiles) newTotal += sf.fileSize;
                    item.totalBytes = newTotal;
                }
                break;
            }
        }
    }

    RefreshTransferListView();
    SaveTransferListToConfig();
    UpdateTransferSummary();
}

void MainWindow::OnStartTransfer() {
    if (m_isTransferring) {
        // 転送中なら中断処理
        if (!m_cancelRequested.load()) {
            m_cancelRequested = true;
            EnableWindow(m_hBtnStartTransfer, FALSE); // 連続クリック抑止
            SetProgressStatus("progress.stopping");
        }
        return;
    }
    if (!m_connState.isConnected) {
        ShowCenteredMessageBox(tW("header.status_disconnected"), tW("messages.warn_no_dest_title"), MB_OK | MB_ICONERROR);
        return;
    }
    if (m_selectedRemotePath.empty()) {
        ShowCenteredMessageBox(tW("messages.warn_no_dest_msg"), tW("messages.warn_no_dest_title"), MB_OK | MB_ICONWARNING);
        return;
    }

    std::string remoteDir = m_selectedRemotePath;
    if (!remoteDir.empty() && remoteDir.back() == '/') {
        remoteDir.pop_back();
    }
    std::string targetDevice = m_connState.targetSerial;

    // チェックされているファイルから転送タスクを生成
    std::vector<TransferTask> tasks;
    uint64_t totalRequiredBytes = 0;

    for (const auto& item : m_transferItems) {
        if (item.isDirectory) {
            for (const auto& sf : item.subFiles) {
                if (sf.isChecked) {
                    TransferTask t;
                    t.localFilePath = sf.localPath;
                    t.fileSize = sf.fileSize;

                    // サブディレクトリ階層の計算
                    size_t lastSlash = sf.relativePath.find_last_of(L"\\/");
                    std::string subDirUtf8 = "";
                    if (lastSlash != std::wstring::npos) {
                        std::wstring subDirW = sf.relativePath.substr(0, lastSlash);
                        for (auto& ch : subDirW) {
                            if (ch == L'\\') ch = L'/';
                        }
                        subDirUtf8 = "/" + I18nManager::WideToUtf8(subDirW);
                    }

                    // フォルダ構成を完全再現: remoteDir / フォルダ名 / [サブディレクトリ]
                    t.remoteTargetDir = remoteDir + "/" + I18nManager::WideToUtf8(item.folderName) + subDirUtf8;
                    t.relativePathForDisplay = I18nManager::WideToUtf8(item.folderName + L"/" + sf.relativePath);
                    tasks.push_back(t);
                    totalRequiredBytes += sf.fileSize;
                }
            }
        } else {
            if (item.isChecked && !item.subFiles.empty()) {
                TransferTask t;
                t.localFilePath = item.localPath;
                t.fileSize = item.totalBytes;
                t.remoteTargetDir = remoteDir;
                t.relativePathForDisplay = I18nManager::WideToUtf8(item.folderName);
                tasks.push_back(t);
                totalRequiredBytes += item.totalBytes;
            }
        }
    }

    if (tasks.empty()) {
        ShowCenteredMessageBox(tW("messages.warn_no_files_msg"), tW("messages.warn_no_files_title"), MB_OK | MB_ICONWARNING);
        return;
    }

    int64_t availableBytes = m_adb->GetRemoteAvailableSpace(targetDevice, remoteDir);
    if (availableBytes >= 0 && totalRequiredBytes > static_cast<uint64_t>(availableBytes)) {
        std::map<std::string, std::wstring> params = {
            {"required", FormatFileSize(totalRequiredBytes)},
            {"available", FormatFileSize(static_cast<uint64_t>(availableBytes))}
        };
        std::wstring msg = tW("messages.warn_low_space_msg", params);
        ShowCenteredMessageBox(msg, tW("messages.warn_low_space_title"), MB_OK | MB_ICONWARNING);
        return;
    }

    m_isTransferring = true;
    m_cancelRequested = false;

    // ボタンテキストを「停止」に変更し、クリック可能のままにする
    SetWindowTextW(m_hBtnStartTransfer, tW("transfer.btn_stop").c_str());
    EnableWindow(m_hBtnStartTransfer, TRUE);
    EnableWindow(m_hBtnRemoveChecked, FALSE);
    EnableWindow(m_hBtnClearTransfer, FALSE);

    std::thread([this, tasks, targetDevice]() {
        auto results = m_engine->TransferTasks(
            *m_adb, targetDevice, tasks,
            [this](int cur, int total, const std::string& msg, int pct, double spd, int /*eta*/) {
                struct ProgData {
                    int percent;
                    std::string message;
                };
                int overallPct = (total > 0) ? static_cast<int>(((cur - 1) * 100LL + pct) / total) : pct;
                if (overallPct > 100) overallPct = 100;
                if (overallPct < 0) overallPct = 0;

                std::string speedStr = TransferEngine::FormatSpeed(spd);
                std::string extraStatus = "";
                if (pct >= 95 && pct < 100) {
                    extraStatus = " [" + t("progress.waiting_flush") + "]";
                }

                std::string dispMsg = "[" + std::to_string(cur) + "/" + std::to_string(total) + "] " +
                                      msg + extraStatus + " (" + speedStr + " - 全体 " + std::to_string(overallPct) + "%)";

                auto pProg = new ProgData{overallPct, dispMsg};
                PostMessageW(m_hwnd, WM_APP_TRANSFER_PROGRESS, (WPARAM)pProg, 0);
            },
            [this]() {
                return m_cancelRequested.load();
            },
            [this](const TransferTask& task, const TransferResult& res) {
                if (res.success) {
                    auto pPath = new std::wstring(task.localFilePath);
                    PostMessageW(m_hwnd, WM_APP_TRANSFER_FILE_DONE, (WPARAM)pPath, 0);
                }
            });

        auto pRes = new std::vector<TransferResult>(std::move(results));
        PostMessageW(m_hwnd, WM_APP_TRANSFER_COMPLETE, (WPARAM)pRes, 0);
    }).detach();
}

} // namespace AdbTransfer
