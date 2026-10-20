================================================================================
ADB Transfer v1.2.2 - Android 端末ファイル転送ツール
================================================================================

【概要】
Windows PCから Android 端末への Wireless ADB 経由による
高速・安全なファイル転送ツールです。

【同梱ファイル】
- AdbTransfer.exe        : アプリケーション本体
- adb.exe                : Android Debug Bridge 実行バイナリ (AOSP由来)
- AdbWinApi.dll          : ADB通信用DLL
- AdbWinUsbApi.dll       : ADB USB通信用DLL
- lang/                  : 多言語リソースフォルダ (ja.json, en.json, zh.json)
- LICENSE-Apache-2.0.txt : ADB用 Apache License 2.0 ライセンス全文
- NOTICE-adb.txt         : ADB著作権告知ファイル
- README.txt             : 本説明ファイル

【使用方法】
1. 初回設定 (USB接続):
   - 端末側で「開発者向けオプション」の「USBデバッグ」をONにします。
   - 端末とPCをUSBケーブルで接続します。
   - AdbTransfer.exe を起動し、「端末を登録 (USB)」ボタンをクリックします。
   - 登録が完了すると、自動的にWi-FiワイヤレスADBが有効化されます。
   - USBケーブルを取り外してWi-Fi環境で利用可能です。

2. 通常使用 (Wi-Fi接続):
   - 端末とPCが同一Wi-Fiネットワークに接続された状態で AdbTransfer.exe を起動します。
   - 自動的にWi-Fi経由で接続されます。

3. ファイル転送:
   - 左側のストレージツリーから転送先フォルダ（内蔵またはMicroSDカード）を選択します。
   - 右側のエリアにファイルやフォルダをドラッグ＆ドロップします。
   - 「転送開始」をクリックすると、高速転送およびファイルサイズ整合性ベリファイが実行されます。

【ライセンス情報 (Licenses)】
- 本配布物に含まれる adb.exe、AdbWinApi.dll、AdbWinUsbApi.dll は、
  Android Open Source Project (AOSP) の成果物であり、
  Apache License, Version 2.0 に基づいて再配布されています。
  ライセンスの詳細は同封の「LICENSE-Apache-2.0.txt」および「NOTICE-adb.txt」をご確認ください。

================================================================================
