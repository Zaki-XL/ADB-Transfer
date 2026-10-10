#include "MainWindow.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    AdbTransfer::MainWindow app;
    if (!app.Create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"ADB Transfer の初期化に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
        return 1;
    }

    return app.Run();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)lpCmdLine;
    return wWinMain(hInstance, hPrevInstance, nullptr, nCmdShow);
}
