# ADB Transfer

[日本語](README.md) | [English](README_EN.md) | **简体中文**

专为从 Windows PC 通过 Wireless ADB 快速、可靠地将书籍、漫画、视频、PDF 等大容量文件传输至 Android 设备而设计的原生 Win32 GUI 桌面工具。

---

## 1. 概述与核心特性

从根本上解决传统 MTP 模式下经常出现的“创建了空文件夹但内容为0字节”、“莫名静默断开”等痛点问题，通过 Wi-Fi 局域网实现流畅高效的文件传输。

- **USB 首次一键注册**: 将设备通过 USB 连接电脑后只需点击一次，自动完成 Wi-Fi IP 获取并开启 `tcpip 5555` 端口。之后即可完全无线使用。
- **内部存储与外置 MicroSD 卡自动识别**: 不仅支持 `/sdcard`，还能自动检测外置 MicroSD 卡（UUID）并提供树状目录层级浏览。
- **拖拽添加与目录智能合并**: 直接将文件或文件夹拖入传输区域即可高速传输。自动合并至目标目录，完整保护已有文件。
- **完全文件大小精确校验**: 每次 `adb push` 完成后，以 1 字节为单位严格比对 Windows 本地与 Android 远端（`stat -c %s`）文件大小。
- **多语言国际化支持 (i18n: 日本語 / English / 简体中文)**:
  - 启动时自动扫描 `lang/` 目录中的语言定义文件。
  - 通过界面右下角的下拉菜单随时一键切换语言（无需重启应用）。
  - **添加新语言步骤**:
    1. 复制 `lang/en.json`（或 `lang/ja.json`），将文件名重命名为目标语言代码（例如韩语为 `ko.json`，法语为 `fr.json`，西班牙语为 `es.json`）。
    2. 将 JSON 中的 `"language_name"` 修改为您希望在下拉菜单中显示的语言名称（如 `"한국어"`），并翻译各项文本。
    3. 启动应用后，新语言将自动显示在语言选择器中（无需修改任何代码或重新编译！）。  
    *(※ 若存在未翻译的词条，应用不会崩溃，会自动安全回退显示为 `[MISSING: 键名]`)*
- **单文件绿色便携分发**: 无需安装任何额外运行库。解压 GitHub Releases 的 ZIP 即可直接运行。

---

## 版本控制规则 (Versioning Policy)
本项目遵循语义化版本控制规范（Semantic Versioning `MAJOR.MINOR.PATCH`）：
- **标题显示**: `ADB Transfer v1.0.0`
- **第2位数值 (MINOR / 功能增加)**: 新增功能、新增 UI 组件、新增存储格式支持等
- **第3位数值 (PATCH / 功能修复)**: 缺陷修复、消除闪烁/界面渲染优化、文本修正等
- **第1位数值 (MAJOR / 主版本更新)**: 架构破坏性变更、核心通信协议重构等

```mermaid
flowchart LR
    subgraph PC [Windows 11 / 10]
        UI[ADB Transfer<br>AdbTransfer.exe]
        ADB[内置 adb.exe<br>AdbWinApi.dll]
        UI -->|异步管道控制| ADB
    end
    subgraph LAN [Wi-Fi 局域网]
        ADB -. "TCP 5555<br>高速流式传输" .-> DEV
    end
    subgraph DEV [Android 设备]
        ADBD[adbd 守护进程]
        INT[(内部存储<br>/sdcard/)]
        EXT[(MicroSD卡<br>/storage/UUID/)]
        ADBD --> INT
        ADBD --> EXT
    end
```

---

## 2. 使用方法

### 步骤 1: 首次配对设置 (USB连接)
1. 在设备上开启“开发者选项”并启用“USB 调试”。
2. 使用 USB 数据线将设备连接至 PC。
3. 打开 `AdbTransfer.exe`，点击顶部的“**注册设备 (USB)**”按钮。
4. 注册完成后，Wi-Fi IP 地址将自动保存至 `config.ini`，并开放端口 5555。
5. 此时可断开并拔下 USB 数据线。

### 步骤 2: 日常无线文件传输
1. 确保 PC 与设备处于同一 Wi-Fi 网络（或同一路由器环境下），启动 `AdbTransfer.exe`。
2. 应用将自动通过 Wi-Fi 连接，状态指示灯变为绿色（●）。
3. 在左侧树状视图中选择内部存储或 MicroSD 卡上的目标文件夹。
   - 常用文件夹可点击“**+ 添加文件夹**”保存为快捷预设。
4. 将要传输的文件或文件夹拖拽至右侧传输列表。
5. 点击“**开始传输**”，即刻开始后台传输并执行严格的文件大小校验。

---

## 3. 目录与文件结构

```text
ADB Transfer/
├── bin/
│   ├── AdbTransfer.exe            # 编译生成的 GUI 应用程序
│   ├── lang/                      # 运行时多语言资源
│   │   ├── ja.json                # 日语字典
│   │   ├── en.json                # 英语字典
│   │   └── zh.json                # 简体中文字典
│   └── platform-tools/            # 运行所需的 Google 官方 Platform-Tools
├── doc/
│   ├── file_transfer_tool_specification.md  # 详细规格说明书
│   ├── task.md                              # 任务进度管理表
│   └── reviewer_report.md                   # QA与安全审查报告
├── include/
│   ├── AdbManager.h               # ADB 进程管道控制
│   ├── ConfigManager.h            # config.ini 持久化
│   ├── DeviceManager.h            # 设备注册与连接状态管理
│   ├── I18nManager.h              # 多语言管理器头文件
│   ├── StorageScanner.h           # 存储设备与目录遍历
│   ├── TransferEngine.h           # 传输引擎与字节校验
│   └── MainWindow.h               # Win32 GUI 主窗口
├── lang/                          # 多语言资源字典 (JSON)
│   ├── ja.json                    # 日语
│   ├── en.json                    # 英语 (English)
│   └── zh.json                    # 简体中文
├── src/                           # 源代码实现
├── tests/                         # 自动化单元测试套件 (4/4 全项通过)
├── Release/                       # GitHub Release 分发包
│   ├── AdbTransfer.exe
│   ├── adb.exe
│   ├── AdbWinApi.dll
│   ├── AdbWinUsbApi.dll
│   ├── lang/
│   │   ├── ja.json
│   │   ├── en.json
│   │   └── zh.json
│   └── README.txt
├── README.md                      # 日语文档
├── README_EN.md                   # 英语文档 (English)
└── README_ZH.md                   # 简体中文文档 (本文档)
```

*(※ 内部开发规格说明、任务进度管理、审查报告等 `doc/` 目录以及临时脚本目录 `scratch/` 均已通过 `.gitignore` 排除在 Git 追踪之外，确保公开仓库的整洁与安全)*
