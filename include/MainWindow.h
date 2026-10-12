#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include "AdbManager.h"
#include "ConfigManager.h"
#include "DeviceManager.h"
#include "StorageScanner.h"
#include "TransferEngine.h"

namespace AdbTransfer {

// アプリケーションバージョン (Semantic Versioning: MAJOR.MINOR.PATCH)
// 機能追加時は MINOR (第2), 機能修正時は PATCH (第3) をカウントアップ
constexpr const wchar_t* APP_VERSION = L"1.1.0";

// カスタムメッセージ
constexpr UINT WM_APP_CONNECT_COMPLETE    = WM_APP + 1;
constexpr UINT WM_APP_SCAN_STORAGE_DONE   = WM_APP + 2;
constexpr UINT WM_APP_SCAN_DIR_DONE       = WM_APP + 3;
constexpr UINT WM_APP_TRANSFER_PROGRESS   = WM_APP + 4;
constexpr UINT WM_APP_TRANSFER_COMPLETE   = WM_APP + 5;

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    int Run();
    void OnPresetRightClick(size_t index);
    void OnDropFiles(HDROP hDrop);
    HBRUSH GetBgBrush() const { return m_hBgBrush; }

    // 展開式ファイル転送リスト用構造体
    struct SubFileInfo {
        std::wstring localPath;
        std::wstring relativePath;
        uint64_t fileSize = 0;
        bool isChecked = true;
    };

    struct TransferItemInfo {
        std::wstring localPath;
        std::wstring folderName;
        bool isDirectory = false;
        uint64_t totalBytes = 0;
        int fileCount = 0;
        bool isExpanded = false; // 展開中フラグ (true: "v ", false: "> ")
        bool isChecked = true;
        std::vector<SubFileInfo> subFiles;
    };

    struct TransferListRow {
        size_t rootIndex = 0;
        int subFileIndex = -1; // -1: ルートアイテム, >= 0: サブファイル
    };

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void InitControls();
    void UpdateLayout(int width, int height);
    void RefreshPresetButtons();
    void SetConnectionStatus(const std::string& key, const std::map<std::string, std::wstring>& params = {});
    void SetProgressStatus(const std::string& key, const std::map<std::string, std::wstring>& params = {});
    void UpdateConnectionLamp(bool connected);
    void UpdateConnectedStatusText();

    // アクション
    void OnRegisterDevice();
    void OnReconnect();
    void OnRefreshStorage();
    void OnTreeItemExpanding(NMTREEVIEWW* nmtv);
    void OnTreeSelectionChanged(NMTREEVIEWW* nmtv);
    void OnAddPreset();
    void OnStartTransfer();
    void OnRemoveChecked();
    void OnClearTransferList();
    void UpdateTransferSummary();
    void UpdateTransferButtonState();
    void UpdateUIConnectionState(bool isConnected, bool isScanning = false);
    void ApplyLanguage();
    void OnLanguageChanged();
    RECT GetSplitterRect() const;

    // 展開式ファイル転送リスト用メソッド
    void AddTransferItem(const std::wstring& path);
    void RefreshTransferListView();
    void SaveTransferListToConfig();
    void LoadTransferListFromConfig();
    void OnTransferListClick(int itemIndex);
    void OnTransferListItemChanged(NMLISTVIEW* nmlv);

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;

    // コントロール
    HWND m_hHeaderGroup = nullptr;
    HWND m_hStatusLamp = nullptr;
    HWND m_hStatusText = nullptr;
    HWND m_hBtnRegister = nullptr;
    HWND m_hBtnReconnect = nullptr;
    HWND m_hComboLang = nullptr;
    std::vector<std::string> m_langCodes;

    HWND m_hPresetGroup = nullptr;
    std::vector<HWND> m_hPresetButtons;
    HWND m_hBtnAddPreset = nullptr;

    HWND m_hTreeStorage = nullptr;
    struct RootStorageNode {
        HTREEITEM hItem = nullptr;
        bool isInternal = true;
        std::string uuid;
        std::string freeSize;
    };
    std::vector<RootStorageNode> m_rootStorages;

    HWND m_hTransferGroup = nullptr;
    HWND m_hTransferDestText = nullptr;
    HWND m_hTransferList = nullptr; // SysListView32 (チェックボックス付き)
    HWND m_hTransferSummaryText = nullptr; // チェックされたファイル数・合計サイズ
    HWND m_hBtnStartTransfer = nullptr;
    HWND m_hBtnRemoveChecked = nullptr;
    HWND m_hBtnClearTransfer = nullptr;

    HWND m_hProgressBar = nullptr;
    HWND m_hProgressText = nullptr;

    // スプリッター状態
    int m_splitX = 380;
    bool m_isDraggingSplitter = false;
    int m_dragSplitX = 380;
    void DrawSplitterGuide(int x);
    bool m_inSizeMove = false; // ウィンドウリサイズドラッグ中フラグ

    // コアマネージャー
    std::unique_ptr<AdbManager> m_adb;
    std::unique_ptr<ConfigManager> m_config;
    std::unique_ptr<DeviceManager> m_device;
    std::unique_ptr<StorageScanner> m_scanner;
    std::unique_ptr<TransferEngine> m_engine;

    // モダンUIリソース
    HFONT m_hFontRegular = nullptr;
    HFONT m_hFontBold = nullptr;
    HFONT m_hFontTitle = nullptr;
    HBRUSH m_hBgBrush = nullptr;

    // 内部状態
    ConnectionState m_connState;
    std::string m_selectedRemotePath; // 選択中の転送先フォルダ
    std::vector<TransferItemInfo> m_transferItems;
    std::vector<TransferListRow> m_listRows;
    bool m_isUpdatingListChecks = false;
    bool m_isTransferring = false;
    bool m_isConnectingOrScanning = false;

    struct UiStatusState {
        std::string key;
        std::map<std::string, std::wstring> params;
    };
    UiStatusState m_currentConnStatus;
    UiStatusState m_currentProgressStatus;
};

} // namespace AdbTransfer
