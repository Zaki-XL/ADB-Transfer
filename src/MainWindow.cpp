#include "MainWindow_Internal.h"
#include <thread>

namespace AdbTransfer {

MainWindow::MainWindow() {
    m_adb = std::make_unique<AdbManager>();
    m_config = std::make_unique<ConfigManager>();
    m_device = std::make_unique<DeviceManager>();
    m_scanner = std::make_unique<StorageScanner>();
    m_engine = std::make_unique<TransferEngine>();
}

MainWindow::~MainWindow() {
    if (m_hFontRegular) DeleteObject(m_hFontRegular);
    if (m_hFontBold) DeleteObject(m_hFontBold);
    if (m_hFontTitle) DeleteObject(m_hFontTitle);
    if (m_hBgBrush) DeleteObject(m_hBgBrush);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        pThis = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    m_hInstance = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    m_hBgBrush = CreateSolidBrush(RGB(246, 248, 251));

    m_hFontRegular = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    if (!m_hFontRegular) {
        m_hFontRegular = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    m_hFontBold = CreateFontW(
        -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    if (!m_hFontBold) {
        m_hFontBold = CreateFontW(
            -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    m_hFontTitle = CreateFontW(
        -15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
    if (!m_hFontTitle) {
        m_hFontTitle = CreateFontW(
            -15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    const wchar_t CLASS_NAME[] = L"AdbTransfer_MainWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = 0;
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hbrBackground = m_hBgBrush;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    typedef BOOL(WINAPI *SetDpiFunc)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        auto pSetDpi = reinterpret_cast<SetDpiFunc>(reinterpret_cast<void*>(GetProcAddress(hUser, "SetProcessDpiAwarenessContext")));
        if (pSetDpi) {
            pSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    std::wstring winTitle = std::wstring(L"ADB Transfer v") + APP_VERSION;
    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        CLASS_NAME,
        winTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 700,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) return false;

    m_config->Load(L"");

    I18nManager::GetInstance().Init();
    std::string savedLang = m_config->GetLanguage();
    if (!savedLang.empty()) {
        I18nManager::GetInstance().SetLanguage(savedLang);
    }

    InitControls();
    RefreshPresetButtons();
    LoadTransferListFromConfig();

    std::string savedRemote = m_config->GetLastRemotePath();
    if (!savedRemote.empty()) {
        m_selectedRemotePath = savedRemote;
        std::wstring msg = tW("transfer.dest_prefix") + Utf8ToUtf16(m_selectedRemotePath);
        SetWindowTextW(m_hTransferDestText, msg.c_str());
        UpdateTransferButtonState();
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    UpdateLayout(rc.right - rc.left, rc.bottom - rc.top);

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    OnReconnect();

    return true;
}

int MainWindow::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void MainWindow::UpdateConnectedStatusText() {
    if (!m_connState.isConnected) {
        SetConnectionStatus("header.status_disconnected");
        return;
    }
    std::string ip = m_config ? m_config->GetDevice().ip : m_connState.targetSerial;
    if (ip.empty()) ip = m_connState.targetSerial;
    SetConnectionStatus("header.status_connected", {
        {"device", Utf8ToUtf16(m_connState.deviceName)},
        {"ip", Utf8ToUtf16(ip)}
    });
}

void MainWindow::SetConnectionStatus(const std::string& key, const std::map<std::string, std::wstring>& params) {
    m_currentConnStatus.key = key;
    m_currentConnStatus.params = params;
    if (m_hStatusText) {
        std::wstring text = tW(key, params);
        SetWindowTextW(m_hStatusText, text.c_str());
    }
}

void MainWindow::SetProgressStatus(const std::string& key, const std::map<std::string, std::wstring>& params) {
    m_currentProgressStatus.key = key;
    m_currentProgressStatus.params = params;
    if (m_hProgressText) {
        std::wstring text = tW(key, params);
        SetWindowTextW(m_hProgressText, text.c_str());
    }
}

void MainWindow::OnReconnect() {
    UpdateUIConnectionState(false, true);
    SetConnectionStatus("header.status_checking");
    std::thread([this]() {
        std::string msg;
        m_device->AutoConnectWifi(*m_adb, *m_config, msg);
        m_connState = m_device->CheckStatus(*m_adb, *m_config);
        PostMessageW(m_hwnd, WM_APP_CONNECT_COMPLETE, 0, 0);
    }).detach();
}

void MainWindow::OnRegisterDevice() {
    int res = MessageBoxW(m_hwnd, tW("messages.confirm_register_msg").c_str(), tW("messages.confirm_register_title").c_str(), MB_YESNO | MB_ICONQUESTION);
    if (res != IDYES) return;

    UpdateUIConnectionState(false, true);
    SetConnectionStatus("header.status_searching_usb");
    std::thread([this]() {
        std::string msg;
        bool ok = m_device->RegisterDeviceUsb(*m_adb, *m_config, msg);
        if (ok) {
            m_device->AutoConnectWifi(*m_adb, *m_config, msg);
        }
        m_connState = m_device->CheckStatus(*m_adb, *m_config);
        PostMessageW(m_hwnd, WM_APP_CONNECT_COMPLETE, 0, 0);

        if (ok) {
            std::map<std::string, std::wstring> params = { {"ip", Utf8ToUtf16(m_config->GetDevice().ip)} };
            MessageBoxW(m_hwnd, tW("messages.register_success_msg", params).c_str(), tW("messages.register_success_title").c_str(), MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(m_hwnd, tW("messages.register_fail_msg").c_str(), tW("messages.register_fail_title").c_str(), MB_OK | MB_ICONERROR);
        }
    }).detach();
}

void MainWindow::OnRefreshStorage() {
    if (!m_connState.isConnected) {
        UpdateUIConnectionState(false, false);
        return;
    }

    UpdateUIConnectionState(true, true);
    TreeView_DeleteAllItems(m_hTreeStorage);
    SetConnectionStatus("header.status_scanning_storage");

    std::thread([this]() {
        auto storages = m_scanner->ScanStorages(*m_adb, m_connState.targetSerial);
        auto pStorages = new std::vector<StorageInfo>(std::move(storages));
        PostMessageW(m_hwnd, WM_APP_SCAN_STORAGE_DONE, (WPARAM)pStorages, 0);
    }).detach();
}

void MainWindow::OnTreeItemExpanding(NMTREEVIEWW* nmtv) {
    if ((nmtv->action & TVE_EXPAND) == 0) return;

    HTREEITEM hItem = nmtv->itemNew.hItem;
    if (!hItem) return;

    TVITEMEXW item{};
    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    TreeViewGetItemW(m_hTreeStorage, &item);

    auto pData = reinterpret_cast<TreeNodeData*>(item.lParam);
    if (!pData || pData->isLoaded) return;

    pData->isLoaded = true;

    std::string path = pData->fullPath;
    std::thread([this, hItem, path]() {
        auto dirs = m_scanner->ScanDirectories(*m_adb, m_connState.targetSerial, path);
        struct DirResult {
            HTREEITEM parent;
            std::vector<RemoteDirectoryItem> list;
        };
        auto pRes = new DirResult{hItem, std::move(dirs)};
        PostMessageW(m_hwnd, WM_APP_SCAN_DIR_DONE, (WPARAM)pRes, 0);
    }).detach();
}

void MainWindow::OnTreeSelectionChanged(NMTREEVIEWW* nmtv) {
    HTREEITEM hItem = nmtv->itemNew.hItem;
    if (!hItem) return;

    TVITEMEXW item{};
    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    TreeViewGetItemW(m_hTreeStorage, &item);

    auto pData = reinterpret_cast<TreeNodeData*>(item.lParam);
    if (pData) {
        m_selectedRemotePath = pData->fullPath;
        if (m_config) {
            m_config->SetLastRemotePath(m_selectedRemotePath);
            m_config->Save(L"");
        }
        std::wstring msg = tW("transfer.dest_prefix") + Utf8ToUtf16(m_selectedRemotePath);
        SetWindowTextW(m_hTransferDestText, msg.c_str());
        UpdateTransferButtonState();
    }
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO* pMMI = reinterpret_cast<MINMAXINFO*>(lParam);
        pMMI->ptMinTrackSize.x = 750;
        pMMI->ptMinTrackSize.y = 480;
        return 0;
    }

    case WM_ENTERSIZEMOVE: {
        m_inSizeMove = true;
        return 0;
    }

    case WM_EXITSIZEMOVE: {
        m_inSizeMove = false;
        RECT rc;
        GetClientRect(hwnd, &rc);
        UpdateLayout(rc.right - rc.left, rc.bottom - rc.top);
        InvalidateRect(hwnd, nullptr, TRUE);
        UpdateWindow(hwnd);
        return 0;
    }

    case WM_SIZE: {
        if (m_inSizeMove) {
            return 0;
        }
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        UpdateLayout(w, h);
        InvalidateRect(hwnd, nullptr, TRUE);
        UpdateWindow(hwnd);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        FillRect(hdc, &ps.rcPaint, m_hBgBrush);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            RECT splitRc = GetSplitterRect();
            if (m_isDraggingSplitter || PtInRect(&splitRc, pt)) {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        RECT splitRc = GetSplitterRect();
        if (PtInRect(&splitRc, pt)) {
            m_isDraggingSplitter = true;
            m_dragSplitX = m_splitX;
            SetCapture(hwnd);
            DrawSplitterGuide(m_dragSplitX);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (m_isDraggingSplitter) {
            short curX = (short)LOWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int minX = 180;
            int maxX = (rc.right - rc.left) - 360;
            if (maxX < minX) maxX = minX;
            if (curX < minX) curX = minX;
            if (curX > maxX) curX = maxX;
            if (curX != m_dragSplitX) {
                DrawSplitterGuide(m_dragSplitX);
                m_dragSplitX = curX;
                DrawSplitterGuide(m_dragSplitX);
            }
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (m_isDraggingSplitter) {
            DrawSplitterGuide(m_dragSplitX);
            m_isDraggingSplitter = false;
            ReleaseCapture();
            if (m_splitX != m_dragSplitX) {
                m_splitX = m_dragSplitX;
                RECT rc;
                GetClientRect(hwnd, &rc);
                UpdateLayout(rc.right - rc.left, rc.bottom - rc.top);
            }
            return 0;
        }
        break;
    }

    case WM_CAPTURECHANGED: {
        if (m_isDraggingSplitter) {
            DrawSplitterGuide(m_dragSplitX);
            m_isDraggingSplitter = false;
            return 0;
        }
        break;
    }

    case WM_DROPFILES:
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_COMBO_LANG && HIWORD(wParam) == CBN_SELCHANGE) {
            OnLanguageChanged();
        } else if (id == IDC_BTN_REGISTER) {
            OnRegisterDevice();
        } else if (id == IDC_BTN_RECONNECT) {
            OnReconnect();
        } else if (id == IDC_BTN_ADD_PRESET) {
            OnAddPreset();
        } else if (id == IDC_BTN_START_TRANSFER) {
            OnStartTransfer();
        } else if (id == IDC_BTN_REMOVE_CHECKED) {
            OnRemoveChecked();
        } else if (id == IDC_BTN_CLEAR_TRANSFER) {
            OnClearTransferList();
        } else if (id >= IDC_PRESET_BASE && id < IDC_PRESET_BASE + 100) {
            size_t idx = id - IDC_PRESET_BASE;
            const auto& presets = m_config->GetPresets();
            if (idx < presets.size()) {
                m_selectedRemotePath = presets[idx].remotePath;
                if (m_config) {
                    m_config->SetLastRemotePath(m_selectedRemotePath);
                    m_config->Save(L"");
                }
                std::wstring msgW = tW("transfer.dest_prefix") + Utf8ToUtf16(m_selectedRemotePath);
                SetWindowTextW(m_hTransferDestText, msgW.c_str());
                UpdateTransferButtonState();
            }
        }
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, m_hBgBrush);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdc, TRANSPARENT);
        if (hCtrl == m_hStatusLamp) {
            if (m_isConnectingOrScanning) {
                SetTextColor(hdc, RGB(217, 119, 6));
            } else if (m_connState.isConnected) {
                SetTextColor(hdc, RGB(22, 163, 74));
            } else {
                SetTextColor(hdc, RGB(220, 38, 38));
            }
        } else if (hCtrl == m_hTransferSummaryText) {
            SetTextColor(hdc, RGB(37, 99, 235));
        } else {
            SetTextColor(hdc, RGB(30, 41, 59));
        }
        return reinterpret_cast<INT_PTR>(m_hBgBrush);
    }

    case WM_CTLCOLORBTN: {
        return reinterpret_cast<INT_PTR>(m_hBgBrush);
    }

    case WM_NOTIFY: {
        NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->idFrom == IDC_TREE_STORAGE) {
            if (nmhdr->code == TVN_ITEMEXPANDINGW || nmhdr->code == TVN_ITEMEXPANDINGA) {
                OnTreeItemExpanding(reinterpret_cast<NMTREEVIEWW*>(lParam));
            } else if (nmhdr->code == TVN_SELCHANGEDW || nmhdr->code == TVN_SELCHANGEDA) {
                OnTreeSelectionChanged(reinterpret_cast<NMTREEVIEWW*>(lParam));
            } else if (nmhdr->code == TVN_DELETEITEMW || nmhdr->code == TVN_DELETEITEMA) {
                NMTREEVIEWW* pnmtv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                if (pnmtv->itemOld.lParam) {
                    delete reinterpret_cast<TreeNodeData*>(pnmtv->itemOld.lParam);
                    pnmtv->itemOld.lParam = 0;
                }
            }
        } else if (nmhdr->idFrom == IDC_TRANSFER_LIST) {
            if (nmhdr->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* nmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
                OnTransferListItemChanged(nmlv);
            } else if (nmhdr->code == NM_CLICK || nmhdr->code == NM_DBLCLK) {
                LVHITTESTINFO hti{};
                GetCursorPos(&hti.pt);
                ScreenToClient(m_hTransferList, &hti.pt);
                ListView_HitTest(m_hTransferList, &hti);
                if (hti.iItem >= 0 && !(hti.flags & LVHT_ONITEMSTATEICON)) {
                    OnTransferListClick(hti.iItem);
                }
            } else if (nmhdr->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW lplvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                switch (lplvcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int itemIdx = static_cast<int>(lplvcd->nmcd.dwItemSpec);
                    if (itemIdx % 2 == 1) {
                        lplvcd->clrTextBk = RGB(248, 250, 252);
                    } else {
                        lplvcd->clrTextBk = RGB(255, 255, 255);
                    }
                    return CDRF_DODEFAULT;
                }
                default:
                    return CDRF_DODEFAULT;
                }
            }
        }
        return 0;
    }

    case WM_APP_CONNECT_COMPLETE: {
        UpdateConnectionLamp(m_connState.isConnected);
        UpdateConnectedStatusText();
        if (m_connState.isConnected) {
            OnRefreshStorage();
        } else {
            UpdateUIConnectionState(false, false);
        }
        return 0;
    }

    case WM_APP_SCAN_STORAGE_DONE: {
        auto pStorages = reinterpret_cast<std::vector<StorageInfo>*>(wParam);
        if (pStorages) {
            m_rootStorages.clear();
            for (const auto& s : *pStorages) {
                bool isInternal = (s.rootPath == "/sdcard");
                std::string uuid = s.id;
                std::wstring dispName;
                if (isInternal) {
                    std::map<std::string, std::wstring> params = { {"free", Utf8ToUtf16(s.freeSize)} };
                    dispName = tW("storage_tree.internal", params);
                } else {
                    std::map<std::string, std::wstring> params = {
                        {"uuid", Utf8ToUtf16(s.id)},
                        {"free", Utf8ToUtf16(s.freeSize)}
                    };
                    dispName = tW("storage_tree.sdcard", params);
                }

                TVINSERTSTRUCTW tis{};
                tis.hParent = TVI_ROOT;
                tis.hInsertAfter = TVI_LAST;
                tis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
                tis.item.pszText = const_cast<wchar_t*>(dispName.c_str());
                tis.item.cChildren = 1;
                tis.item.lParam = reinterpret_cast<LPARAM>(new TreeNodeData{s.rootPath, false});

                HTREEITEM hRoot = TreeViewInsertItemW(m_hTreeStorage, &tis);
                m_rootStorages.push_back({ hRoot, isInternal, uuid, s.freeSize });

                TVINSERTSTRUCTW dummyTis{};
                dummyTis.hParent = hRoot;
                dummyTis.hInsertAfter = TVI_LAST;
                dummyTis.item.mask = TVIF_TEXT;
                std::wstring loadingText = tW("storage_tree.loading");
                dummyTis.item.pszText = const_cast<wchar_t*>(loadingText.c_str());
                TreeViewInsertItemW(m_hTreeStorage, &dummyTis);
            }
            delete pStorages;
            UpdateConnectedStatusText();
            UpdateUIConnectionState(true, false);
        }
        return 0;
    }

    case WM_APP_SCAN_DIR_DONE: {
        struct DirResult {
            HTREEITEM parent;
            std::vector<RemoteDirectoryItem> list;
        };
        auto pRes = reinterpret_cast<DirResult*>(wParam);
        if (pRes) {
            HTREEITEM hChild = TreeView_GetChild(m_hTreeStorage, pRes->parent);
            while (hChild) {
                HTREEITEM hNext = TreeView_GetNextSibling(m_hTreeStorage, hChild);
                TreeView_DeleteItem(m_hTreeStorage, hChild);
                hChild = hNext;
            }

            if (pRes->list.empty()) {
                TVITEMW tvi{};
                tvi.mask = TVIF_CHILDREN | TVIF_HANDLE;
                tvi.hItem = pRes->parent;
                tvi.cChildren = 0;
                TreeViewSetItemW(m_hTreeStorage, &tvi);
            } else {
                for (const auto& d : pRes->list) {
                    std::wstring dirNameW = I18nManager::Utf8ToWide(d.name);

                    TVINSERTSTRUCTW tis{};
                    tis.hParent = pRes->parent;
                    tis.hInsertAfter = TVI_LAST;
                    tis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
                    tis.item.pszText = const_cast<wchar_t*>(dirNameW.c_str());
                    tis.item.cChildren = 1;
                    tis.item.lParam = reinterpret_cast<LPARAM>(new TreeNodeData{d.fullPath, false});

                    HTREEITEM hNewItem = TreeViewInsertItemW(m_hTreeStorage, &tis);

                    TVINSERTSTRUCTW dummyTis{};
                    dummyTis.hParent = hNewItem;
                    dummyTis.hInsertAfter = TVI_LAST;
                    dummyTis.item.mask = TVIF_TEXT;
                    std::wstring loadingText = tW("storage_tree.loading");
                    dummyTis.item.pszText = const_cast<wchar_t*>(loadingText.c_str());
                    TreeViewInsertItemW(m_hTreeStorage, &dummyTis);
                }
            }
            delete pRes;
        }
        return 0;
    }

    case WM_APP_TRANSFER_PROGRESS: {
        struct ProgData {
            int percent;
            std::string message;
        };
        auto pProg = reinterpret_cast<ProgData*>(wParam);
        if (pProg) {
            SendMessageW(m_hProgressBar, PBM_SETPOS, pProg->percent, 0);
            std::wstring msgW = I18nManager::Utf8ToWide(pProg->message);
            SetWindowTextW(m_hProgressText, msgW.c_str());
            delete pProg;
        }
        return 0;
    }

    case WM_APP_TRANSFER_COMPLETE: {
        auto pResults = reinterpret_cast<std::vector<TransferResult>*>(wParam);
        m_isTransferring = false;
        UpdateUIConnectionState(m_connState.isConnected, false);

        if (pResults) {
            int successCount = 0;
            int total = static_cast<int>(pResults->size());
            std::wstring failDetails;

            for (const auto& r : *pResults) {
                if (r.success) {
                    successCount++;
                } else {
                    failDetails += L"\n・" + r.localPath + L": " + Utf8ToUtf16(r.errorMessage);
                }
            }

            SendMessageW(m_hProgressBar, PBM_SETPOS, 100, 0);
            std::map<std::string, std::wstring> params = {
                {"success", std::to_wstring(successCount)},
                {"total", std::to_wstring(total)}
            };
            SetProgressStatus("progress.complete", params);

            if (successCount == total) {
                std::wstring msg = tW("messages.transfer_success_msg", params);
                MessageBoxW(m_hwnd, msg.c_str(), tW("messages.transfer_success_title").c_str(), MB_OK | MB_ICONINFORMATION);
            } else {
                std::wstring msg = tW("messages.transfer_fail_msg", params) + failDetails;
                MessageBoxW(m_hwnd, msg.c_str(), tW("messages.transfer_fail_title").c_str(), MB_OK | MB_ICONWARNING);
            }
            delete pResults;
        }
        return 0;
    }

    case WM_DESTROY:
        SaveTransferListToConfig();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace AdbTransfer
