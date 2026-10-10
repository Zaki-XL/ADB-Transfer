#pragma once

#include "MainWindow.h"
#include "I18nManager.h"
#include <shellapi.h>
#include <uxtheme.h>
#include <iostream>
#include <sstream>

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
inline HTREEITEM TreeViewInsertItemW(HWND hwnd, const TVINSERTSTRUCTW* lpis) {
    return (HTREEITEM)SendMessageW(hwnd, TVM_INSERTITEMW, 0, (LPARAM)lpis);
}

inline BOOL TreeViewGetItemW(HWND hwnd, TVITEMEXW* pitem) {
    return (BOOL)SendMessageW(hwnd, TVM_GETITEMW, 0, (LPARAM)pitem);
}

inline BOOL TreeViewSetItemW(HWND hwnd, const TVITEMW* pitem) {
    return (BOOL)SendMessageW(hwnd, TVM_SETITEMW, 0, (LPARAM)pitem);
}

// 明示的な Unicode ListView API ヘルパー
inline int ListViewInsertItemW(HWND hwnd, const LVITEMW* pitem) {
    return (int)SendMessageW(hwnd, LVM_INSERTITEMW, 0, (LPARAM)pitem);
}

inline BOOL ListViewSetColumnW(HWND hwnd, int iCol, const LVCOLUMNW* pcol) {
    return (BOOL)SendMessageW(hwnd, LVM_SETCOLUMNW, (WPARAM)iCol, (LPARAM)pcol);
}

inline int ListViewInsertColumnW(HWND hwnd, int iCol, const LVCOLUMNW* pcol) {
    return (int)SendMessageW(hwnd, LVM_INSERTCOLUMNW, (WPARAM)iCol, (LPARAM)pcol);
}

// 文字列変換（I18nManager への一元化）
inline std::wstring Utf8ToUtf16(const std::string& utf8Str) {
    return I18nManager::Utf8ToWide(utf8Str);
}

} // namespace AdbTransfer
