#include "MainWindow_Internal.h"

namespace AdbTransfer {

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

void MainWindow::DrawSplitterGuide(int x) {
    if (!m_hwnd) return;
    HDC hdc = GetDCEx(m_hwnd, NULL, DCX_CACHE | DCX_LOCKWINDOWUPDATE);
    if (!hdc) {
        hdc = GetDC(m_hwnd);
    }
    if (!hdc) return;

    RECT splitRc = GetSplitterRect();
    int top = splitRc.top;
    int bottom = splitRc.bottom;
    int h = bottom - top;
    if (h > 0) {
        // 反転描画 (PATINVERT) により、子コントロール再描画ゼロ・超軽量でガイド線を表示
        PatBlt(hdc, x - 2, top, 4, h, PATINVERT);
    }
    ReleaseDC(m_hwnd, hdc);
}

void MainWindow::UpdateLayout(int width, int height) {
    if (width <= 0 || height <= 0) return;

    size_t presetCount = m_config ? m_config->GetPresets().size() : 0;
    bool isTwoRows = (presetCount > 4);

    // スプリッターX座標の描画用クランプ
    int minLeft = 180;
    int maxLeft = width - 360;
    if (maxLeft < minLeft) maxLeft = minLeft;
    int effectiveSplitX = m_splitX;
    if (effectiveSplitX < minLeft) effectiveSplitX = minLeft;
    if (effectiveSplitX > maxLeft) effectiveSplitX = maxLeft;

    HDWP hdwp = BeginDeferWindowPos(static_cast<int>(20 + m_hPresetButtons.size()));
    auto safeDefer = [&](HWND hCtrl, HWND hAfter, int x, int y, int cx, int cy, UINT flags) {
        if (!hCtrl) return;
        if (hdwp) {
            hdwp = DeferWindowPos(hdwp, hCtrl, hAfter, x, y, cx, cy, flags);
        }
        if (!hdwp) {
            SetWindowPos(hCtrl, hAfter, x, y, cx, cy, flags);
        }
    };
    const UINT baseFlags = SWP_NOACTIVATE | SWP_NOCOPYBITS;

    // 1. ヘッダーエリア
    safeDefer(m_hHeaderGroup, HWND_BOTTOM, 10, 5, width - 20, 55, baseFlags);
    safeDefer(m_hStatusLamp, HWND_TOP, 25, 25, 20, 20, baseFlags);

    int btnReconnectW = 90;
    int btnReconnectX = width - 15 - btnReconnectW;
    int btnRegisterW = 150;
    int btnRegisterX = btnReconnectX - 8 - btnRegisterW;
    int statusTextW = btnRegisterX - 50 - 10;
    if (statusTextW < 100) statusTextW = 100;

    safeDefer(m_hStatusText, HWND_TOP, 50, 27, statusTextW, 20, baseFlags);
    safeDefer(m_hBtnRegister, HWND_TOP, btnRegisterX, 22, btnRegisterW, 26, baseFlags);
    safeDefer(m_hBtnReconnect, HWND_TOP, btnReconnectX, 22, btnReconnectW, 26, baseFlags);

    // 2. プリセットバー
    int presetGroupH = isTwoRows ? 90 : 55;
    safeDefer(m_hPresetGroup, HWND_BOTTOM, 10, 65, width - 20, presetGroupH, baseFlags);

    int addBtnWidth = isTwoRows ? 220 : 250;
    int addBtnHeight = isTwoRows ? 64 : 26;
    int addBtnX = width - addBtnWidth - 15;
    safeDefer(m_hBtnAddPreset, HWND_TOP, addBtnX, 82, addBtnWidth, addBtnHeight, baseFlags);
    std::wstring addBtnText = tW("preset.btn_add") + (isTwoRows ? L"\r\n(MAX 8)" : L"");
    SetWindowTextW(m_hBtnAddPreset, addBtnText.c_str());

    // プリセットボタンの配置
    int availW = addBtnX - 25 - 10;
    if (!isTwoRows) {
        int colW = (presetCount > 0) ? (availW - static_cast<int>(presetCount - 1) * 8) / static_cast<int>(presetCount) : 140;
        if (colW > 150) colW = 150;
        if (colW < 90) colW = 90;
        for (size_t i = 0; i < m_hPresetButtons.size() && i < 4; ++i) {
            int bx = 25 + static_cast<int>(i) * (colW + 8);
            safeDefer(m_hPresetButtons[i], HWND_TOP, bx, 82, colW, 26, baseFlags);
        }
    } else {
        int colW = (availW - 3 * 8) / 4;
        if (colW < 80) colW = 80;
        for (size_t i = 0; i < m_hPresetButtons.size() && i < 8; ++i) {
            int row = static_cast<int>(i / 4);
            int col = static_cast<int>(i % 4);
            int bx = 25 + col * (colW + 8);
            int by = 82 + row * 34;
            safeDefer(m_hPresetButtons[i], HWND_TOP, bx, by, colW, 28, baseFlags);
        }
    }

    // 3. メインエリア縦幅
    int mainTop = isTwoRows ? 162 : 125;
    int mainBottom = height - 60;
    int mainHeight = mainBottom - mainTop;
    if (mainHeight < 100) mainHeight = 100;

    // 左ペイン（ストレージツリー）
    int leftWidth = effectiveSplitX - 10 - 5;
    safeDefer(m_hTreeStorage, HWND_TOP, 10, mainTop, leftWidth, mainHeight, baseFlags);

    // 右ペイン（ファイル転送エリア）
    int rightX = effectiveSplitX + 5;
    int rightWidth = width - rightX - 10;
    safeDefer(m_hTransferGroup, HWND_BOTTOM, rightX, mainTop, rightWidth, mainHeight, baseFlags);

    int innerX = rightX + 15;
    int innerWidth = rightWidth - 30;
    if (innerWidth < 100) innerWidth = 100;

    safeDefer(m_hTransferDestText, HWND_TOP, innerX, mainTop + 25, innerWidth, 25, baseFlags);

    int listHeight = mainHeight - 25 - 25 - 45;
    if (listHeight < 50) listHeight = 50;
    safeDefer(m_hTransferList, HWND_TOP, innerX, mainTop + 55, innerWidth, listHeight, baseFlags);

    ListView_SetColumnWidth(m_hTransferList, 0, innerWidth - 25);

    // ボタン配置
    int btnY = mainTop + mainHeight - 38;
    int btnStartX = rightX + rightWidth - 345;
    if (btnStartX < innerX + 160) btnStartX = innerX + 160;

    int summaryWidth = btnStartX - innerX - 10;
    if (summaryWidth < 80) summaryWidth = 80;
    safeDefer(m_hTransferSummaryText, HWND_TOP, innerX, btnY + 6, summaryWidth, 22, baseFlags | SWP_SHOWWINDOW);

    safeDefer(m_hBtnStartTransfer, HWND_TOP, btnStartX, btnY, 100, 30, baseFlags);
    safeDefer(m_hBtnRemoveChecked, HWND_TOP, btnStartX + 108, btnY, 120, 30, baseFlags);
    safeDefer(m_hBtnClearTransfer, HWND_TOP, btnStartX + 236, btnY, 100, 30, baseFlags);

    // 4. 下部進捗バー＆ステータス・言語セレクター
    safeDefer(m_hProgressBar, HWND_TOP, 10, height - 54, width - 20, 18, baseFlags);

    int comboLangW = 110;
    int comboLangX = width - 10 - comboLangW;
    int comboLangY = height - 32;
    int progressTextW = comboLangX - 10 - 10;
    if (progressTextW < 100) progressTextW = 100;

    safeDefer(m_hProgressText, HWND_TOP, 10, height - 30, progressTextW, 22, baseFlags);
    safeDefer(m_hComboLang, HWND_TOP, comboLangX, comboLangY, comboLangW, 200, baseFlags);

    if (hdwp) {
        EndDeferWindowPos(hdwp);
    }

    // 再描画
    InvalidateRect(m_hwnd, nullptr, TRUE);
    UpdateWindow(m_hwnd);
}

} // namespace AdbTransfer
