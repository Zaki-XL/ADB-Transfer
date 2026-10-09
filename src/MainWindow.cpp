#include "MainWindow.h"
#include "I18nManager.h"
#include <shellapi.h>
#include <uxtheme.h>
#include <iostream>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace AdbTransfer {

// コントロールID
constexpr int IDC_BTN_REGISTER       = 1001;
constexpr int IDC_BTN_RECONNECT      = 1002;
constexpr int IDC_BTN_ADD_PRESET     = 1003;
constexpr int IDC_BTN_START_TRANSFER = 1004;
constexpr int IDC_BTN_CLEAR_TRANSFER = 1005;
constexpr int IDC_TREE_STORAGE       = 1006;
constexpr int IDC_BTN_REMOVE_CHECKED = 1007;
constexpr int IDC_TRANSFER_LIST      = 1008;
constexpr int IDC_COMBO_LANG         = 1009;
constexpr int IDC_PRESET_BASE        = 2000; // 2000 ~ 2099

// 構造体（ツリーノード用データ）
struct TreeNodeData {
    std::string fullPath;
    bool isLoaded = false;
};

// 明示的な Unicode TreeView API ヘルパー
static inline HTREEITEM TreeViewInsertItemW(HWND hwnd, const TVINSERTSTRUCTW* lpis) {
    return (HTREEITEM)SendMessageW(hwnd, TVM_INSERTITEMW, 0, (LPARAM)lpis);
}

static inline BOOL TreeViewGetItemW(HWND hwnd, TVITEMEXW* pitem) {
    return (BOOL)SendMessageW(hwnd, TVM_GETITEMW, 0, (LPARAM)pitem);
}

static inline BOOL TreeViewSetItemW(HWND hwnd, const TVITEMW* pitem) {
    return (BOOL)SendMessageW(hwnd, TVM_SETITEMW, 0, (LPARAM)pitem);
}

static inline std::wstring Utf8ToUtf16(const std::string& utf8Str) {
    if (utf8Str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::vector<wchar_t> wbuf(len);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, wbuf.data(), len);
    return std::wstring(wbuf.data());
}

static LRESULT CALLBACK PresetButtonSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_RBUTTONUP) {
        MainWindow* pMain = reinterpret_cast<MainWindow*>(dwRefData);
        size_t presetIdx = static_cast<size_t>(uIdSubclass);
        if (pMain) {
            pMain->OnPresetRightClick(presetIdx, hWnd);
            return 0;
        }
    } else if (uMsg == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, PresetButtonSubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

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

    // コモンコントロール初期化
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // モダンUI用フォント＆背景ブラシ生成 (Yu Gothic UI / Segoe UI, ClearType)
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
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = m_hBgBrush;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    // 高DPI対応（Windows 10/11）
    typedef BOOL(WINAPI *SetDpiFunc)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        auto pSetDpi = (SetDpiFunc)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (pSetDpi) {
            pSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    std::wstring winTitle = std::wstring(L"ADB Transfer v") + APP_VERSION;
    m_hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        CLASS_NAME,
        winTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 700,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) return false;

    // 起動時の初期化（設定読み込み）
    m_config->Load(L"");

    // 多言語 (i18n) の初期化
    I18nManager::GetInstance().Init();
    std::string savedLang = m_config->GetLanguage();
    if (!savedLang.empty()) {
        I18nManager::GetInstance().SetLanguage(savedLang);
    }

    InitControls();
    RefreshPresetButtons();

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    UpdateLayout(rc.right - rc.left, rc.bottom - rc.top);

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);

    // Wi-Fi自動接続
    OnReconnect();

    return true;
}

RECT MainWindow::GetSplitterRect() const {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    size_t presetCount = m_config ? m_config->GetPresets().size() : 0;
    int top = (presetCount > 4) ? 162 : 125;
    int bottom = rc.bottom - 70;
    if (bottom < top) bottom = top;

    int splitX = m_splitX;
    int minLeft = 180;
    int maxLeft = (rc.right - rc.left) - 360;
    if (maxLeft < minLeft) maxLeft = minLeft;
    if (splitX < minLeft) splitX = minLeft;
    if (splitX > maxLeft) splitX = maxLeft;

    return RECT{ splitX - 4, top, splitX + 4, bottom };
}

void MainWindow::InitControls() {
    // 1. ヘッダーエリア
    m_hHeaderGroup = CreateWindowExW(0, L"BUTTON", L"端末接続状態",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 5, 940, 55, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hHeaderGroup, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hStatusLamp = CreateWindowExW(0, L"STATIC", L"●",
        WS_CHILD | WS_VISIBLE | SS_CENTER, 25, 23, 20, 22, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hStatusLamp, WM_SETFONT, (WPARAM)m_hFontTitle, TRUE);

    m_hStatusText = CreateWindowExW(0, L"STATIC", L"接続を確認中...",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 50, 26, 600, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hStatusText, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    m_hBtnRegister = CreateWindowExW(0, L"BUTTON", L"端末を登録 (USB)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 670, 22, 130, 26, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_REGISTER, m_hInstance, nullptr);
    SendMessageW(m_hBtnRegister, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    m_hBtnReconnect = CreateWindowExW(0, L"BUTTON", L"再接続",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 810, 22, 90, 26, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_RECONNECT, m_hInstance, nullptr);
    SendMessageW(m_hBtnReconnect, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    // 言語切替セレクター (ドロップダウンコンボボックス)
    m_hComboLang = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 910, 22, 110, 200, m_hwnd, (HMENU)(INT_PTR)IDC_COMBO_LANG, m_hInstance, nullptr);
    SendMessageW(m_hComboLang, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    // 利用可能な言語を追加
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

    // 2. プリセットバー（0〜4個は1行、5〜8個は2行に動的伸縮）
    m_hPresetGroup = CreateWindowExW(0, L"BUTTON", L"転送先プリセット（右クリックで削除可能、最大8つ）",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 65, 940, 55, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hPresetGroup, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hBtnAddPreset = CreateWindowExW(0, L"BUTTON", L"+ フォルダ登録",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE, 670, 82, 250, 26, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_ADD_PRESET, m_hInstance, nullptr);
    SendMessageW(m_hBtnAddPreset, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    // 3. ストレージツリービュー（左ペイン）
    m_hTreeStorage = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        10, 125, 360, 440, m_hwnd, (HMENU)(INT_PTR)IDC_TREE_STORAGE, m_hInstance, nullptr);
    SendMessageW(m_hTreeStorage, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
    // Explorer モダンテーマの適用（角丸ハイライト、モダンな矢印展開アイコン）
    SetWindowTheme(m_hTreeStorage, L"Explorer", nullptr);
    TreeView_SetExtendedStyle(m_hTreeStorage,
        TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL | TVS_EX_FADEINOUTEXPANDOS,
        TVS_EX_DOUBLEBUFFER | TVS_EX_AUTOHSCROLL | TVS_EX_FADEINOUTEXPANDOS);

    // 4. ファイル転送エリア（右ペイン）
    m_hTransferGroup = CreateWindowExW(0, L"BUTTON", L"ファイル転送エリア (ドラッグ＆ドロップ)",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 390, 125, 560, 440, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hTransferGroup, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hTransferDestText = CreateWindowExW(0, L"STATIC", L"転送先: [未選択] 左のツリーまたはプリセットから選択してください",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 405, 150, 530, 25, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hTransferDestText, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    // SysListView32 レポート形式、チェックボックス付き
    m_hTransferList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
        405, 180, 530, 330, m_hwnd, (HMENU)(INT_PTR)IDC_TRANSFER_LIST, m_hInstance, nullptr);
    SendMessageW(m_hTransferList, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
    // Explorer モダンテーマの適用
    SetWindowTheme(m_hTransferList, L"Explorer", nullptr);

    // 拡張スタイル: チェックボックス、1行選択、ダブルバッファ、ラベルチップ
    ListView_SetExtendedListViewStyle(m_hTransferList,
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);

    // カラムを1列追加
    LVCOLUMNW lvc{};
    lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.cx = 500;
    lvc.pszText = const_cast<wchar_t*>(L"対象ファイル/フォルダ");
    ListView_InsertColumn(m_hTransferList, 0, &lvc);

    // チェック状態サマリー表示テキスト
    m_hTransferSummaryText = CreateWindowExW(0, L"STATIC", L"選択中: 0 ファイル (0 B)",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 405, 525, 200, 22, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hTransferSummaryText, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    // 操作ボタン3つ: 「転送開始」「リストから削除」「リストクリア」
    m_hBtnStartTransfer = CreateWindowExW(0, L"BUTTON", L"転送開始",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 620, 520, 100, 30, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_START_TRANSFER, m_hInstance, nullptr);
    SendMessageW(m_hBtnStartTransfer, WM_SETFONT, (WPARAM)m_hFontBold, TRUE);

    m_hBtnRemoveChecked = CreateWindowExW(0, L"BUTTON", L"リストから削除",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 730, 520, 115, 30, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_REMOVE_CHECKED, m_hInstance, nullptr);
    SendMessageW(m_hBtnRemoveChecked, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    m_hBtnClearTransfer = CreateWindowExW(0, L"BUTTON", L"リストクリア",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 855, 520, 95, 30, m_hwnd, (HMENU)(INT_PTR)IDC_BTN_CLEAR_TRANSFER, m_hInstance, nullptr);
    SendMessageW(m_hBtnClearTransfer, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    // 5. 下部進捗バー＆ステータス
    m_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 10, 580, 940, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    m_hProgressText = CreateWindowExW(0, L"STATIC", L"準備完了",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 605, 940, 22, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hProgressText, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);

    UpdateTransferButtonState();
    ApplyLanguage();
}

void MainWindow::UpdateLayout(int width, int height) {
    if (width <= 0 || height <= 0) return;

    size_t presetCount = m_config ? m_config->GetPresets().size() : 0;
    bool isTwoRows = (presetCount > 4);

    // スプリッターX座標の描画用クランプ（m_splitX 自体はウィンドウサイズ縮小時に不可逆上書きしない）
    int minLeft = 180;
    int maxLeft = width - 360;
    if (maxLeft < minLeft) maxLeft = minLeft;
    int effectiveSplitX = m_splitX;
    if (effectiveSplitX < minLeft) effectiveSplitX = minLeft;
    if (effectiveSplitX > maxLeft) effectiveSplitX = maxLeft;

    // 1. ヘッダーエリア
    SetWindowPos(m_hHeaderGroup, nullptr, 10, 5, width - 20, 55, SWP_NOZORDER);
    SetWindowPos(m_hStatusLamp, nullptr, 25, 25, 20, 20, SWP_NOZORDER);

    int btnReconnectW = 90;
    int btnReconnectX = width - 15 - btnReconnectW;
    int btnRegisterW = 150;
    int btnRegisterX = btnReconnectX - 8 - btnRegisterW;
    int statusTextW = btnRegisterX - 50 - 10;
    if (statusTextW < 100) statusTextW = 100;

    SetWindowPos(m_hStatusText, nullptr, 50, 27, statusTextW, 20, SWP_NOZORDER);
    SetWindowPos(m_hBtnRegister, nullptr, btnRegisterX, 22, btnRegisterW, 26, SWP_NOZORDER);
    SetWindowPos(m_hBtnReconnect, nullptr, btnReconnectX, 22, btnReconnectW, 26, SWP_NOZORDER);

    // 2. プリセットバー（0〜4個は1行: 高さ55、5〜8個は2行: 高さ90）
    int presetGroupH = isTwoRows ? 90 : 55;
    SetWindowPos(m_hPresetGroup, nullptr, 10, 65, width - 20, presetGroupH, SWP_NOZORDER);

    int addBtnWidth = isTwoRows ? 220 : 250;
    int addBtnHeight = isTwoRows ? 64 : 26;
    int addBtnX = width - addBtnWidth - 15;
    SetWindowPos(m_hBtnAddPreset, nullptr, addBtnX, 82, addBtnWidth, addBtnHeight, SWP_NOZORDER);
    std::wstring addBtnText = tW("preset.btn_add") + (isTwoRows ? L"\r\n(MAX 8)" : L"");
    SetWindowTextW(m_hBtnAddPreset, addBtnText.c_str());

    // プリセットボタンの配置
    int availW = addBtnX - 25 - 10;
    if (!isTwoRows) {
        // 0〜4個: 1行表示（ボタン高さ 26px）
        int colW = (presetCount > 0) ? (availW - static_cast<int>(presetCount - 1) * 8) / static_cast<int>(presetCount) : 140;
        if (colW > 150) colW = 150;
        if (colW < 90) colW = 90;
        for (size_t i = 0; i < m_hPresetButtons.size() && i < 4; ++i) {
            int bx = 25 + static_cast<int>(i) * (colW + 8);
            SetWindowPos(m_hPresetButtons[i], nullptr, bx, 82, colW, 26, SWP_NOZORDER);
        }
    } else {
        // 5〜8個: 2行表示（4列 × 2行、ボタン高さ 28px）
        int colW = (availW - 3 * 8) / 4;
        if (colW < 80) colW = 80;
        for (size_t i = 0; i < m_hPresetButtons.size() && i < 8; ++i) {
            int row = static_cast<int>(i / 4);
            int col = static_cast<int>(i % 4);
            int bx = 25 + col * (colW + 8);
            int by = 82 + row * 34;
            SetWindowPos(m_hPresetButtons[i], nullptr, bx, by, colW, 28, SWP_NOZORDER);
        }
    }

    // 3. メインエリア縦幅
    int mainTop = isTwoRows ? 162 : 125;
    int mainBottom = height - 60;
    int mainHeight = mainBottom - mainTop;
    if (mainHeight < 100) mainHeight = 100;

    // 左ペイン（ストレージツリー）
    int leftWidth = effectiveSplitX - 10 - 5;
    SetWindowPos(m_hTreeStorage, nullptr, 10, mainTop, leftWidth, mainHeight, SWP_NOZORDER);

    // 右ペイン（ファイル転送エリア）
    int rightX = effectiveSplitX + 5;
    int rightWidth = width - rightX - 10;
    SetWindowPos(m_hTransferGroup, nullptr, rightX, mainTop, rightWidth, mainHeight, SWP_NOZORDER);

    int innerX = rightX + 15;
    int innerWidth = rightWidth - 30;
    if (innerWidth < 100) innerWidth = 100;

    SetWindowPos(m_hTransferDestText, nullptr, innerX, mainTop + 25, innerWidth, 25, SWP_NOZORDER);

    int listHeight = mainHeight - 25 - 25 - 45;
    if (listHeight < 50) listHeight = 50;
    SetWindowPos(m_hTransferList, nullptr, innerX, mainTop + 55, innerWidth, listHeight, SWP_NOZORDER);

    // リストビューのカラム幅を調整（横スクロールバーが出ないようにフィット）
    ListView_SetColumnWidth(m_hTransferList, 0, innerWidth - 25);

    // ボタン配置
    int btnY = mainTop + mainHeight - 38;
    int btnStartX = rightX + rightWidth - 345;
    if (btnStartX < innerX + 160) btnStartX = innerX + 160;

    // サマリーテキストをボタン群の左側に配置
    int summaryWidth = btnStartX - innerX - 10;
    if (summaryWidth < 80) summaryWidth = 80;
    SetWindowPos(m_hTransferSummaryText, nullptr, innerX, btnY + 6, summaryWidth, 22, SWP_NOZORDER | SWP_SHOWWINDOW);

    SetWindowPos(m_hBtnStartTransfer, nullptr, btnStartX, btnY, 100, 30, SWP_NOZORDER);
    SetWindowPos(m_hBtnRemoveChecked, nullptr, btnStartX + 108, btnY, 120, 30, SWP_NOZORDER);
    SetWindowPos(m_hBtnClearTransfer, nullptr, btnStartX + 236, btnY, 100, 30, SWP_NOZORDER);

    // 4. 下部進捗バー＆ステータス・言語セレクター
    SetWindowPos(m_hProgressBar, nullptr, 10, height - 54, width - 20, 18, SWP_NOZORDER);

    int comboLangW = 110;
    int comboLangX = width - 10 - comboLangW;
    int comboLangY = height - 32;
    int progressTextW = comboLangX - 10 - 10;
    if (progressTextW < 100) progressTextW = 100;

    SetWindowPos(m_hProgressText, nullptr, 10, height - 30, progressTextW, 22, SWP_NOZORDER);
    SetWindowPos(m_hComboLang, nullptr, comboLangX, comboLangY, comboLangW, 200, SWP_NOZORDER);

    // スプリッター周辺のみを更新してフリッカーを防止（子コントロールは SetWindowPos により自律描画）
    RECT splitArea = { effectiveSplitX - 10, mainTop, effectiveSplitX + 10, mainBottom };
    InvalidateRect(m_hwnd, &splitArea, FALSE);
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
    ListView_SetColumn(m_hTransferList, 0, &lvc);

    SetWindowTextW(m_hBtnStartTransfer, tW("transfer.btn_start").c_str());
    SetWindowTextW(m_hBtnRemoveChecked, tW("transfer.btn_remove").c_str());
    SetWindowTextW(m_hBtnClearTransfer, tW("transfer.btn_clear").c_str());

    UpdateTransferSummary();

    // 接続状態テキストの反映 (共通キー・パラメータから現在の言語で再描画)
    if (!m_currentConnStatus.key.empty()) {
        SetWindowTextW(m_hStatusText, tW(m_currentConnStatus.key, m_currentConnStatus.params).c_str());
    } else if (m_connState.isConnected) {
        std::string ip = m_config ? m_config->GetDevice().ip : m_connState.targetSerial;
        if (ip.empty()) ip = m_connState.targetSerial;
        SetConnectionStatus("header.status_connected", {
            {"device", Utf8ToUtf16(m_connState.deviceName)},
            {"ip", Utf8ToUtf16(ip)}
        });
    } else {
        SetConnectionStatus("header.status_disconnected");
    }

    // 進捗ステータステキストの反映 (共通キー・パラメータから現在の言語で再描画)
    if (!m_currentProgressStatus.key.empty()) {
        SetWindowTextW(m_hProgressText, tW(m_currentProgressStatus.key, m_currentProgressStatus.params).c_str());
    } else {
        SetProgressStatus("progress.ready");
    }

    // ストレージツリーのルート項目表示を動的更新
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

void MainWindow::RefreshPresetButtons() {
    for (HWND hBtn : m_hPresetButtons) {
        DestroyWindow(hBtn);
    }
    m_hPresetButtons.clear();

    const auto& presets = m_config->GetPresets();
    for (size_t i = 0; i < presets.size() && i < 8; ++i) {
        int len = MultiByteToWideChar(CP_UTF8, 0, presets[i].name.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> w(len);
        MultiByteToWideChar(CP_UTF8, 0, presets[i].name.c_str(), -1, w.data(), len);

        HWND hBtn = CreateWindowExW(0, L"BUTTON", w.data(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 100, 28, m_hwnd,
            (HMENU)(INT_PTR)(IDC_PRESET_BASE + i), m_hInstance, nullptr);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)m_hFontRegular, TRUE);
        SetWindowSubclass(hBtn, PresetButtonSubclassProc, (UINT_PTR)i, (DWORD_PTR)this);
        m_hPresetButtons.push_back(hBtn);
    }

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    UpdateLayout(rc.right - rc.left, rc.bottom - rc.top);
}

void MainWindow::OnPresetRightClick(size_t index, HWND hBtn) {
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

void MainWindow::UpdateConnectionLamp(bool connected) {
    // 接続状態ランプ（緑または赤）
    // シンプルにUnicode文字（●）の表示で対応
    SetWindowTextW(m_hStatusLamp, connected ? L"●" : L"○");
}

void MainWindow::OnReconnect() {
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
    if (!m_connState.isConnected) return;

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
        std::wstring msg = tW("transfer.dest_prefix") + Utf8ToUtf16(m_selectedRemotePath);
        SetWindowTextW(m_hTransferDestText, msg.c_str());
        UpdateTransferButtonState();
    }
}

void MainWindow::OnAddPreset() {
    if (m_selectedRemotePath.empty()) {
        MessageBoxW(m_hwnd, tW("preset.warn_select_folder").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (m_config->GetPresets().size() >= 8) {
        MessageBoxW(m_hwnd, tW("preset.warn_max_presets").c_str(), tW("messages.warn_no_dest_title").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    // 表示名を決定
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

void MainWindow::OnDropFiles(HDROP hDrop) {
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

            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(m_hTransferList);
            lvi.pszText = path;
            int itemIdx = ListView_InsertItem(m_hTransferList, &lvi);
            // デフォルトでチェックボックスを有りに設定
            ListView_SetCheckState(m_hTransferList, itemIdx, TRUE);
        }
    }
    DragFinish(hDrop);

    UpdateTransferSummary();
    SetProgressStatus("progress.items_added", {{"count", std::to_wstring(ListView_GetItemCount(m_hTransferList))}});
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

    // チェックされたアイテムのみを収集
    std::vector<std::wstring> filesToTransfer;
    int count = ListView_GetItemCount(m_hTransferList);
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(m_hTransferList, i)) {
            if (i < static_cast<int>(m_transferItems.size())) {
                filesToTransfer.push_back(m_transferItems[i].localPath);
            }
        }
    }

    if (filesToTransfer.empty()) {
        MessageBoxW(m_hwnd, tW("messages.warn_no_files_msg").c_str(), tW("messages.warn_no_files_title").c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    m_isTransferring = true;
    EnableWindow(m_hBtnStartTransfer, FALSE);
    EnableWindow(m_hBtnRemoveChecked, FALSE);
    EnableWindow(m_hBtnClearTransfer, FALSE);

    std::string remoteDir = m_selectedRemotePath;
    std::string targetDevice = m_connState.targetSerial;

    std::thread([this, filesToTransfer, remoteDir, targetDevice]() {
        auto results = m_engine->TransferBatch(
            *m_adb, targetDevice, filesToTransfer, remoteDir,
            [this](int cur, int total, const std::string& msg, int pct) {
                struct ProgData {
                    int percent;
                    std::string message;
                };
                auto pProg = new ProgData{pct, "[" + std::to_string(cur) + "/" + std::to_string(total) + "] " + msg};
                PostMessageW(m_hwnd, WM_APP_TRANSFER_PROGRESS, (WPARAM)pProg, 0);
            });

        auto pRes = new std::vector<TransferResult>(std::move(results));
        PostMessageW(m_hwnd, WM_APP_TRANSFER_COMPLETE, (WPARAM)pRes, 0);
    }).detach();
}

LRESULT MainWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO* pMMI = reinterpret_cast<MINMAXINFO*>(lParam);
        pMMI->ptMinTrackSize.x = 750;
        pMMI->ptMinTrackSize.y = 480;
        return 0;
    }

    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        UpdateLayout(w, h);
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
            SetCapture(hwnd);
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
            m_splitX = curX;
            UpdateLayout(rc.right - rc.left, rc.bottom - rc.top);
            return 0;
        }
        break;
    }

    case WM_LBUTTONUP: {
        if (m_isDraggingSplitter) {
            m_isDraggingSplitter = false;
            ReleaseCapture();
            return 0;
        }
        break;
    }

    case WM_CAPTURECHANGED: {
        m_isDraggingSplitter = false;
        return 0;
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
                std::wstring msgW = tW("transfer.dest_prefix") + Utf8ToUtf16(m_selectedRemotePath);
                SetWindowTextW(m_hTransferDestText, msgW.c_str());
                UpdateTransferButtonState();
            }
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // 子コントロールのチラつき（フリッカー）を完全抑止

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hCtrl = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdc, TRANSPARENT);
        if (hCtrl == m_hStatusLamp) {
            // 接続状態に応じて鮮やかな緑/赤
            SetTextColor(hdc, m_connState.isConnected ? RGB(22, 163, 74) : RGB(220, 38, 38));
        } else if (hCtrl == m_hTransferSummaryText) {
            SetTextColor(hdc, RGB(37, 99, 235)); // アクセントブルー (#2563EB)
        } else {
            SetTextColor(hdc, RGB(30, 41, 59)); // スレートダーク (#1E293B)
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
                if ((nmlv->uChanged & LVIF_STATE) &&
                    ((nmlv->uNewState & LVIS_STATEIMAGEMASK) != (nmlv->uOldState & LVIS_STATEIMAGEMASK))) {
                    UpdateTransferSummary();
                }
            } else if (nmhdr->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW lplvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);
                switch (lplvcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int itemIdx = static_cast<int>(lplvcd->nmcd.dwItemSpec);
                    // 偶数行・奇数行で背景色を交互に変える（ゼブラストライプ）
                    if (itemIdx % 2 == 1) {
                        lplvcd->clrTextBk = RGB(248, 250, 252); // 淡いスレートグレー
                    } else {
                        lplvcd->clrTextBk = RGB(255, 255, 255); // 白
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
        if (m_connState.isConnected) {
            std::string ip = m_config ? m_config->GetDevice().ip : m_connState.targetSerial;
            if (ip.empty()) ip = m_connState.targetSerial;
            SetConnectionStatus("header.status_connected", {
                {"device", Utf8ToUtf16(m_connState.deviceName)},
                {"ip", Utf8ToUtf16(ip)}
            });
            OnRefreshStorage();
        } else {
            SetConnectionStatus("header.status_disconnected");
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
                tis.item.cChildren = 1; // サブディレクトリあり（展開可能にする）
                tis.item.lParam = reinterpret_cast<LPARAM>(new TreeNodeData{s.rootPath, false});

                HTREEITEM hRoot = TreeViewInsertItemW(m_hTreeStorage, &tis);
                m_rootStorages.push_back({ hRoot, isInternal, uuid, s.freeSize });

                // 開閉を可能にするためダミー子要素を追加
                TVINSERTSTRUCTW dummyTis{};
                dummyTis.hParent = hRoot;
                dummyTis.hInsertAfter = TVI_LAST;
                dummyTis.item.mask = TVIF_TEXT;
                std::wstring loadingText = tW("storage_tree.loading");
                dummyTis.item.pszText = const_cast<wchar_t*>(loadingText.c_str());
                TreeViewInsertItemW(m_hTreeStorage, &dummyTis);
            }
            delete pStorages;
            if (m_connState.isConnected) {
                std::string ip = m_config ? m_config->GetDevice().ip : m_connState.targetSerial;
                if (ip.empty()) ip = m_connState.targetSerial;
                SetConnectionStatus("header.status_connected", {
                    {"device", Utf8ToUtf16(m_connState.deviceName)},
                    {"ip", Utf8ToUtf16(ip)}
                });
            }
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
            // 親アイテムに付いている既存の子アイテム（ダミー等）を全削除
            HTREEITEM hChild = TreeView_GetChild(m_hTreeStorage, pRes->parent);
            while (hChild) {
                HTREEITEM hNext = TreeView_GetNextSibling(m_hTreeStorage, hChild);
                TreeView_DeleteItem(m_hTreeStorage, hChild);
                hChild = hNext;
            }

            if (pRes->list.empty()) {
                // サブディレクトリが存在しない場合、+ボタンを非表示化
                TVITEMW tvi{};
                tvi.mask = TVIF_CHILDREN | TVIF_HANDLE;
                tvi.hItem = pRes->parent;
                tvi.cChildren = 0;
                TreeViewSetItemW(m_hTreeStorage, &tvi);
            } else {
                for (const auto& d : pRes->list) {
                    int len = MultiByteToWideChar(CP_UTF8, 0, d.name.c_str(), -1, nullptr, 0);
                    std::vector<wchar_t> w(len);
                    MultiByteToWideChar(CP_UTF8, 0, d.name.c_str(), -1, w.data(), len);

                    TVINSERTSTRUCTW tis{};
                    tis.hParent = pRes->parent;
                    tis.hInsertAfter = TVI_LAST;
                    tis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
                    tis.item.pszText = w.data();
                    tis.item.cChildren = 1; // さらにサブディレクトリ展開可能
                    tis.item.lParam = reinterpret_cast<LPARAM>(new TreeNodeData{d.fullPath, false});

                    HTREEITEM hSub = TreeViewInsertItemW(m_hTreeStorage, &tis);

                    // 各サブディレクトリにも開閉用のダミー子要素を追加
                    TVINSERTSTRUCTW dummyTis{};
                    dummyTis.hParent = hSub;
                    dummyTis.hInsertAfter = TVI_LAST;
                    dummyTis.item.mask = TVIF_TEXT;
                    dummyTis.item.pszText = const_cast<wchar_t*>(L"読み込み中...");
                    TreeViewInsertItemW(m_hTreeStorage, &dummyTis);
                }
                // 親アイテムを展開
                TreeView_Expand(m_hTreeStorage, pRes->parent, TVE_EXPAND);
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
            int len = MultiByteToWideChar(CP_UTF8, 0, pProg->message.c_str(), -1, nullptr, 0);
            std::vector<wchar_t> w(len);
            MultiByteToWideChar(CP_UTF8, 0, pProg->message.c_str(), -1, w.data(), len);
            SetWindowTextW(m_hProgressText, w.data());
            delete pProg;
        }
        return 0;
    }

    case WM_APP_TRANSFER_COMPLETE: {
        auto pResults = reinterpret_cast<std::vector<TransferResult>*>(wParam);
        m_isTransferring = false;
        UpdateTransferButtonState();
        EnableWindow(m_hBtnRemoveChecked, TRUE);
        EnableWindow(m_hBtnClearTransfer, TRUE);

        if (pResults) {
            int successCount = 0;
            int total = static_cast<int>(pResults->size());
            for (const auto& r : *pResults) {
                if (r.success) successCount++;
            }

            std::map<std::string, std::wstring> params = {
                {"count", std::to_wstring(successCount)},
                {"total", std::to_wstring(total)}
            };
            SetProgressStatus("progress.complete", params);
            SendMessageW(m_hProgressBar, PBM_SETPOS, 100, 0);

            std::wstring summary = tW("progress.complete", params);
            MessageBoxW(m_hwnd, summary.c_str(), tW("progress.ready").c_str(), MB_OK | (successCount == total ? MB_ICONINFORMATION : MB_ICONWARNING));
            delete pResults;
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int MainWindow::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace AdbTransfer
