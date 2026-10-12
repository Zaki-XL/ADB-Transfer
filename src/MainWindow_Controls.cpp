#include "MainWindow_Internal.h"

namespace AdbTransfer {

static LRESULT CALLBACK PresetButtonSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_RBUTTONUP) {
        MainWindow* pMain = reinterpret_cast<MainWindow*>(dwRefData);
        size_t presetIdx = static_cast<size_t>(uIdSubclass);
        if (pMain) {
            pMain->OnPresetRightClick(presetIdx);
            return 0;
        }
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, PresetButtonSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK GroupBoxSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_DROPFILES) {
        MainWindow* pMain = reinterpret_cast<MainWindow*>(dwRefData);
        if (pMain) {
            pMain->OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        }
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, GroupBoxSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK ListViewDropSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_DROPFILES) {
        MainWindow* pMain = reinterpret_cast<MainWindow*>(dwRefData);
        if (pMain) {
            pMain->OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        }
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, ListViewDropSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void MainWindow::InitControls() {
    // 1. ヘッダーエリア
    m_hHeaderGroup = CreateWindowExW(0, L"BUTTON", L"端末接続状態",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX | WS_CLIPSIBLINGS, 10, 5, 940, 55, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hHeaderGroup, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
    SetWindowSubclass(m_hHeaderGroup, GroupBoxSubclassProc, 1, (DWORD_PTR)this);

    m_hStatusLamp = CreateWindowExW(0, L"STATIC", L"●",
        WS_CHILD | WS_VISIBLE | SS_CENTER | WS_CLIPSIBLINGS, 25, 23, 20, 22, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hStatusLamp, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);

    m_hStatusText = CreateWindowExW(0, L"STATIC", L"接続を確認中...",
        WS_CHILD | WS_VISIBLE | SS_LEFT | WS_CLIPSIBLINGS, 50, 26, 600, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hStatusText, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    m_hBtnRegister = CreateWindowExW(0, L"BUTTON", L"端末を登録 (USB)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_CLIPSIBLINGS, 670, 22, 130, 26, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_REGISTER, m_hInstance, nullptr);
    SendMessageW(m_hBtnRegister, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    m_hBtnReconnect = CreateWindowExW(0, L"BUTTON", L"再接続",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_CLIPSIBLINGS, 810, 22, 90, 26, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_RECONNECT, m_hInstance, nullptr);
    SendMessageW(m_hBtnReconnect, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    // 言語切替セレクター
    m_hComboLang = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_CLIPSIBLINGS, 910, 22, 110, 200, m_hwnd, (HMENU)(INT_PTR)IDC_COMBO_LANG, m_hInstance, nullptr);
    SendMessageW(m_hComboLang, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    m_langCodes.clear();
    const auto& availLangs = I18nManager::GetInstance().RefreshAvailableLanguages();
    std::string curLang = I18nManager::GetInstance().GetCurrentLanguage();
    int selIdx = 0;
    for (size_t i = 0; i < availLangs.size(); ++i) {
        m_langCodes.push_back(availLangs[i].code);
        SendMessageW(m_hComboLang, CB_ADDSTRING, 0, (LPARAM)availLangs[i].name.c_str());
        if (availLangs[i].code == curLang) {
            selIdx = static_cast<int>(i);
        }
    }
    SendMessageW(m_hComboLang, CB_SETCURSEL, selIdx, 0);

    // 2. プリセットバー
    m_hPresetGroup = CreateWindowExW(0, L"BUTTON", L"転送先プリセット",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX | WS_CLIPSIBLINGS, 10, 65, 940, 55, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hPresetGroup, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
    SetWindowSubclass(m_hPresetGroup, GroupBoxSubclassProc, 2, (DWORD_PTR)this);

    m_hBtnAddPreset = CreateWindowExW(0, L"BUTTON", L"+ フォルダ登録",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE | WS_CLIPSIBLINGS, 670, 82, 250, 26, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_ADD_PRESET, m_hInstance, nullptr);
    SendMessageW(m_hBtnAddPreset, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    // 3. ストレージツリービュー（左ペイン）
    m_hTreeStorage = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | WS_CLIPSIBLINGS,
        10, 125, 360, 440, m_hwnd, (HMENU)(INT_PTR)IDC_TREE_STORAGE, m_hInstance, nullptr);
    SendMessageW(m_hTreeStorage, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
    SetWindowTheme(m_hTreeStorage, L"Explorer", nullptr);
    TreeView_SetExtendedStyle(m_hTreeStorage,
        TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL | TVS_EX_FADEINOUTEXPANDOS,
        TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL | TVS_EX_FADEINOUTEXPANDOS);

    // 4. ファイル転送エリア（右ペイン）
    m_hTransferGroup = CreateWindowExW(0, L"BUTTON", L"ファイル転送エリア",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX | WS_CLIPSIBLINGS, 390, 125, 560, 440, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hTransferGroup, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);
    DragAcceptFiles(m_hTransferGroup, TRUE);
    SetWindowSubclass(m_hTransferGroup, GroupBoxSubclassProc, 3, (DWORD_PTR)this);

    m_hTransferDestText = CreateWindowExW(0, L"STATIC", L"転送先: [未選択]",
        WS_CHILD | WS_VISIBLE | SS_LEFT | WS_CLIPSIBLINGS, 405, 150, 530, 25, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hTransferDestText, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hTransferList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | WS_CLIPSIBLINGS,
        405, 180, 530, 330, m_hwnd, (HMENU)(INT_PTR)IDC_TRANSFER_LIST, m_hInstance, nullptr);
    SendMessageW(m_hTransferList, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
    DragAcceptFiles(m_hTransferList, TRUE);
    SetWindowSubclass(m_hTransferList, ListViewDropSubclassProc, 4, (DWORD_PTR)this);
    SetWindowTheme(m_hTransferList, L"Explorer", nullptr);

    ListView_SetExtendedListViewStyle(m_hTransferList,
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

    LVCOLUMNW lvc{};
    lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.cx = 500;
    lvc.pszText = const_cast<wchar_t*>(L"対象ファイル/フォルダ");
    ListViewInsertColumnW(m_hTransferList, 0, &lvc);

    m_hTransferSummaryText = CreateWindowExW(0, L"STATIC", L"選択中: 0 ファイル (0 B)",
        WS_CHILD | WS_VISIBLE | SS_LEFT | WS_CLIPSIBLINGS, 405, 525, 200, 22, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hTransferSummaryText, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hBtnStartTransfer = CreateWindowExW(0, L"BUTTON", L"転送開始",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_CLIPSIBLINGS, 620, 520, 100, 30, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_START_TRANSFER, m_hInstance, nullptr);
    SendMessageW(m_hBtnStartTransfer, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hBtnRemoveChecked = CreateWindowExW(0, L"BUTTON", L"リストから削除",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_CLIPSIBLINGS, 730, 520, 115, 30, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_REMOVE_CHECKED, m_hInstance, nullptr);
    SendMessageW(m_hBtnRemoveChecked, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    m_hBtnClearTransfer = CreateWindowExW(0, L"BUTTON", L"リストクリア",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_CLIPSIBLINGS, 855, 520, 95, 30, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_CLEAR_TRANSFER, m_hInstance, nullptr);
    SendMessageW(m_hBtnClearTransfer, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    // 5. 下部進捗バー＆ステータス
    m_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH | WS_CLIPSIBLINGS, 10, 580, 940, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    m_hProgressText = CreateWindowExW(0, L"STATIC", L"準備完了",
        WS_CHILD | WS_VISIBLE | SS_LEFT | WS_CLIPSIBLINGS, 10, 605, 940, 22, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hProgressText, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    UpdateTransferButtonState();
    ApplyLanguage();
    UpdateUIConnectionState(false, false);
}

void MainWindow::ApplyLanguage() {
    std::wstring titleWithVer = tW("window_title") + L" v" + APP_VERSION;
    SetWindowTextW(m_hwnd, titleWithVer.c_str());
    SetWindowTextW(m_hHeaderGroup, tW("header.group_title").c_str());
    SetWindowTextW(m_hBtnRegister, tW("header.btn_register").c_str());
    SetWindowTextW(m_hBtnReconnect, tW("header.btn_reconnect").c_str());
    SetWindowTextW(m_hPresetGroup, tW("preset.group_title").c_str());

    size_t presetCount = m_config ? m_config->GetPresets().size() : 0;
    bool isTwoRows = (presetCount > 4);
    std::wstring addBtnText = tW("preset.btn_add") + (isTwoRows ? L"\r\n(MAX 8)" : L"");
    SetWindowTextW(m_hBtnAddPreset, addBtnText.c_str());

    SetWindowTextW(m_hTransferGroup, tW("transfer.group_title").c_str());

    if (m_selectedRemotePath.empty()) {
        SetWindowTextW(m_hTransferDestText, tW("transfer.dest_none").c_str());
    } else {
        std::wstring destMsg = tW("transfer.dest_prefix") + Utf8ToUtf16(m_selectedRemotePath);
        SetWindowTextW(m_hTransferDestText, destMsg.c_str());
    }

    LVCOLUMNW lvc{};
    lvc.mask = LVCF_TEXT;
    lvc.pszText = const_cast<wchar_t*>(tW("transfer.col_name").c_str());
    ListViewSetColumnW(m_hTransferList, 0, &lvc);

    SetWindowTextW(m_hBtnStartTransfer, tW("transfer.btn_start").c_str());
    SetWindowTextW(m_hBtnRemoveChecked, tW("transfer.btn_remove").c_str());
    SetWindowTextW(m_hBtnClearTransfer, tW("transfer.btn_clear").c_str());

    UpdateTransferSummary();

    if (!m_currentConnStatus.key.empty()) {
        SetWindowTextW(m_hStatusText, tW(m_currentConnStatus.key, m_currentConnStatus.params).c_str());
    } else {
        UpdateConnectedStatusText();
    }

    if (!m_currentProgressStatus.key.empty()) {
        SetWindowTextW(m_hProgressText, tW(m_currentProgressStatus.key, m_currentProgressStatus.params).c_str());
    } else {
        SetProgressStatus("progress.ready");
    }

    for (const auto& node : m_rootStorages) {
        if (!node.hItem) continue;
        std::wstring dispName;
        if (node.isInternal) {
            std::map<std::string, std::wstring> p = { {"free", Utf8ToUtf16(node.freeSize)} };
            dispName = tW("storage_tree.internal", p);
        } else {
            std::map<std::string, std::wstring> p = {
                {"uuid", Utf8ToUtf16(node.uuid)},
                {"free", Utf8ToUtf16(node.freeSize)}
            };
            dispName = tW("storage_tree.sdcard", p);
        }
        TVITEMW tvi{};
        tvi.mask = TVIF_TEXT | TVIF_HANDLE;
        tvi.hItem = node.hItem;
        tvi.pszText = const_cast<wchar_t*>(dispName.c_str());
        TreeViewSetItemW(m_hTreeStorage, &tvi);
    }
}

void MainWindow::OnLanguageChanged() {
    int curSel = static_cast<int>(SendMessageW(m_hComboLang, CB_GETCURSEL, 0, 0));
    if (curSel >= 0 && curSel < static_cast<int>(m_langCodes.size())) {
        std::string newLang = m_langCodes[curSel];
        if (newLang != I18nManager::GetInstance().GetCurrentLanguage()) {
            I18nManager::GetInstance().SetLanguage(newLang);
            m_config->SetLanguage(newLang);
            m_config->Save(L"");
            ApplyLanguage();
        }
    }
}

void MainWindow::UpdateTransferButtonState() {
    if (!m_hBtnStartTransfer) return;
    bool canTransfer = !m_isTransferring && !m_selectedRemotePath.empty();
    EnableWindow(m_hBtnStartTransfer, canTransfer ? TRUE : FALSE);
}

void MainWindow::UpdateUIConnectionState(bool isConnected, bool isScanning) {
    m_isConnectingOrScanning = isScanning;
    bool isReady = isConnected && !isScanning;

    if (m_hTreeStorage) EnableWindow(m_hTreeStorage, isReady ? TRUE : FALSE);
    if (m_hBtnAddPreset) EnableWindow(m_hBtnAddPreset, isReady ? TRUE : FALSE);
    for (HWND hBtn : m_hPresetButtons) EnableWindow(hBtn, isReady ? TRUE : FALSE);
    if (m_hTransferList) EnableWindow(m_hTransferList, isReady ? TRUE : FALSE);
    if (m_hBtnRemoveChecked) EnableWindow(m_hBtnRemoveChecked, (isReady && !m_isTransferring) ? TRUE : FALSE);
    if (m_hBtnClearTransfer) EnableWindow(m_hBtnClearTransfer, (isReady && !m_isTransferring) ? TRUE : FALSE);

    if (!isConnected) {
        if (m_hTreeStorage) TreeView_DeleteAllItems(m_hTreeStorage);
    }
    UpdateTransferButtonState();
    UpdateConnectionLamp(isConnected);
    if (m_hStatusLamp) InvalidateRect(m_hStatusLamp, nullptr, TRUE);
}

void MainWindow::RefreshPresetButtons() {
    for (HWND hBtn : m_hPresetButtons) {
        DestroyWindow(hBtn);
    }
    m_hPresetButtons.clear();

    const auto& presets = m_config->GetPresets();
    bool isReady = m_connState.isConnected && !m_isConnectingOrScanning;
    for (size_t i = 0; i < presets.size() && i < 8; ++i) {
        std::wstring btnText = I18nManager::Utf8ToWide(presets[i].name);

        HWND hBtn = CreateWindowExW(0, L"BUTTON", btnText.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_CLIPSIBLINGS, 0, 0, 100, 28, m_hwnd,
            (HMENU)(INT_PTR)(IDC_PRESET_BASE + i), m_hInstance, nullptr);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
        EnableWindow(hBtn, isReady ? TRUE : FALSE);
        SetWindowSubclass(hBtn, PresetButtonSubclassProc, (UINT_PTR)i, (DWORD_PTR)this);
        m_hPresetButtons.push_back(hBtn);
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    UpdateLayout(rc.right - rc.left, rc.bottom - rc.top);
}

void MainWindow::UpdateConnectionLamp(bool connected) {
    (void)connected;
    SetWindowTextW(m_hStatusLamp, L"●");
}

} // namespace AdbTransfer
