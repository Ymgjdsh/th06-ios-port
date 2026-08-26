# TH06 iOS Port

将《东方红魔乡 ～ the Embodiment of Scarlet Devil》移植到 iPhone 和 iPad 的非官方开源项目。

本项目基于社区重建的 TH06 源码和 SDL2 移植工作，重点维护 iOS 14+ 的原生 arm64 版本，并提供触控操作、竖屏布局、OpenGL ES 2.0 渲染和跨平台联机。它不是原作官方版本，也与上海爱丽丝幻乐团、ZUN 或 Apple 无关。

> [!IMPORTANT]
> 本项目的大部分新增、移植、联机和平台适配代码具有明显的 **vibe coding** 属性，主要由 AI/LLM 在开发者指导下生成或反复修改。代码能够编译、打包或在部分设备上运行，不等于它已经经过充分的人工设计审查、安全审计和跨设备测试。请把真机测试、双方日志和可复现步骤视为判断修复是否有效的必要证据。

## 当前状态

- 当前 iOS 版本：`1.2.5 (22)`
- 最低系统：iOS / iPadOS 14.0
- 架构：arm64
- 安装方式：使用 TrollStore 安装 ad-hoc 签名 IPA
- 渲染后端：SDL2 + OpenGL ES 2.0
- 已验证：完整源码可在 macOS 12.7.6、Xcode 14.0 和 iPhoneOS SDK 16.0 下构建并生成结构有效的 IPA
- 尚未完成的验证：当前版本仍需要在真实设备上继续确认闪屏、画面割裂和联机整体卡顿问题

构建成功只代表源码可以编译和打包，不代表所有运行问题已经解决。请在提交问题时附上设备型号、系统版本、联机方式和完整客机/主机日志。

## 主要功能

- iPhone 和 iPad 原生运行，支持触控拖动、虚拟按键和菜单触控
- 针对手机和平板的竖屏游戏区、计分区和 4:3 iPad 布局
- GLES 2.0 渲染路径，支持游戏内 JPEG 画面和 PBG3 资源
- SDL2_mixer 音频，打包 17 首 OGG BGM
- 中文、英文和日文联机界面
- 保存、配置和 Replay 写入独立的 iOS 应用数据目录
- iOS 启动、资源、渲染、场景切换和崩溃诊断日志
- 自动化 macOS 远程构建、IPA 校验、桌面交付和临时文件清理

## 联机方式

| 模式 | iOS 与 iOS | iOS 与 Windows | 说明 |
| --- | --- | --- | --- |
| 附近局域网 | 支持 | 支持 | 设备连接同一 Wi-Fi，并使用相同房间端口 |
| 直连地址 | 支持 | 支持 | 客机填写主机 IPv4 地址和相同端口 |
| 中继房间 | 支持 | 支持 | 双方填写相同中继端点和房间码 |
| 蓝牙附近设备 | 支持 | 不支持 | 使用 Apple MultipeerConnectivity，仅限 iOS 设备 |

局域网联机建议使用端口 `3037`。首次搜索时必须允许“本地网络”权限；如果曾拒绝，请到系统设置中重新启用。路由器需要允许无线客户端互相访问，访客 Wi-Fi 或开启 AP 隔离的网络无法正常发现对方。

Windows 联机版由同一份源码构建：

```text
dist/th06-windows-netplay-win32/
dist/th06-windows-netplay-win32.zip
```

Windows Defender 弹出提示时，请允许 `th06.exe` 访问专用网络。

## 安装 IPA

1. 在兼容设备上安装并打开 TrollStore。
2. 导入本项目生成的 `.ipa`。
3. 首次启动联机功能时，允许本地网络和蓝牙权限。
4. 如果覆盖安装后出现异常，先保留 Replay 和存档，再记录日志后进行干净安装对比。

仓库不提供 App Store 签名，也不需要 Apple Developer Team。应用 Bundle ID 为 `com.th06.sdl2.ios`。

## 在 macOS 上构建

### 环境要求

- macOS 与 Xcode 14 或更高版本
- CMake 3.20 或更高版本
- Python 3
- 可合法使用的 TH06 游戏数据和 OGG BGM

项目默认从 `ios/assets` 和 `ios/bgm` 暂存资源。也可以通过 `ASSET_DIR`、`ASSET_APK` 和 `BGM_DIR` 指定其他资源位置。

```sh
chmod +x ios/build_ios.sh ios/package_ipa.sh
./ios/build_ios.sh
```

默认产物：

```text
build-ios/th06-ios-1.2.5-22.ipa
```

构建流程会执行源码预检、干净构建、资源完整性检查、版本检查、Bonjour/蓝牙权限声明检查、ad-hoc 签名和 IPA 打包。Release 构建默认删除旧的 `build-ios`，避免陈旧对象或资源混入新包。

## 从 Windows 自动构建 IPA

仓库提供 SSH 远程构建脚本，可将完整源码快照上传到 Mac，在 Mac 上构建和验证 IPA，然后放到 Mac 桌面：

```powershell
powershell -ExecutionPolicy Bypass -File .\ios\build_on_mac.ps1 `
  -MacHost 10.0.0.142 `
  -MacUser dick `
  -IosVersion 1.2.5 `
  -IosBuild 22
```

脚本成功或失败后都会清理 Mac 上对应的临时源码包和构建目录。成功产物名称包含 Git 短提交号，例如：

```text
~/Desktop/th06-ios-1.2.5-22-<commit>.ipa
```

提交号比重复使用的应用版本号更可靠。定位回归时，请同时记录完整 Git commit 和唯一 tag，不要只记录 `1.2.5 (20)`。

## 构建 Windows 联机版

在安装 Visual Studio 2022 C++ 桌面工具和 CMake 的 Windows 电脑上运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_windows_netplay.ps1 `
  -Architecture Win32 `
  -Clean
```

脚本会生成可运行目录和 ZIP，并将与当前 iOS 源码匹配的游戏程序、SDL 运行库、资源和联机说明放入包中。

## 日志与问题报告

在 Mac 上打开“控制台”，选择已连接的 iPhone 或 iPad，将消息级别切换为“所有信息”，搜索 `th06` 后启动游戏。常用日志前缀包括：

- `[IOS-BOOT]`：启动、版本和主循环状态
- `[AssetProbe]`：游戏资源检查
- `[RendererGLES]`：GLES 渲染、FBO 和画面呈现
- `[netplay]`、`[netplay/discovery]`：联机、Bonjour 和数据传输
- `[IOS-CRASH]`：原生崩溃信息

报告闪屏、割裂或联机卡顿时，请至少提供：

- 主机和客机各自的设备型号、iOS 版本与 Git commit
- 使用的联机模式、房间端口、网络环境和主客机角色
- 从进入联机菜单前到问题出现后的双方完整日志
- 问题是否在单机模式出现，以及是否能稳定复现

只有客机日志通常不足以判断网络同步问题；主机和客机同一时间段的日志才能区分渲染、帧调度、网络抖动与状态恢复问题。

## 项目结构

```text
ios/                         iOS 工程配置、平台桥接、资源和构建脚本
src/                         游戏逻辑、GLES 渲染、触控和联机实现
scripts/build_windows_netplay.ps1
                             Windows 联机版构建与打包
scripts/publish_github_version.ps1
                             完整源码提交、标签和 GitHub 发布检查
tools/relay_service/         中继服务
3rdparty/                    第三方依赖
```

更详细的 iOS 构建说明见 [`ios/README.md`](ios/README.md)。

## 版本与源码保存

Git 历史是项目唯一可信的源码版本记录。每个交付测试的版本应满足：

1. 完整、可构建的源码已经提交并推送到 `origin/main`。
2. 使用一个从未使用过的 annotated tag，禁止移动或覆盖旧标签。
3. IPA、ZIP、日志、构建缓存、签名材料和私钥不进入 Git。
4. 发布说明准确区分“编译通过”“IPA 校验通过”和“真机测试通过”。

## Vibe Coding 声明

这个项目不是传统方式下由稳定团队逐行设计、评审和维护的移植工程。大部分项目特有代码是在快速试验过程中由 AI/LLM 辅助完成，包括但不限于 iOS 平台适配、渲染修复、触控、联机、诊断和自动构建脚本。

这意味着：

- 代码中可能存在重复实现、脆弱假设、历史修补残留和缺少测试的路径。
- 版本号、文件名和注释可能落后于真实行为，应以 Git commit、实际源码和运行证据为准。
- AI 给出的“根因”或“已修复”结论不能代替可复现测试和日志证据。
- 欢迎审查、重构和提交可验证的修复；报告问题时请尽量提供双方完整日志。

我们不会隐瞒这种开发方式，也不会把一次成功编译包装成完整质量保证。

## 致谢

- [happyhavoc/th06](https://github.com/happyhavoc/th06)：TH06 社区逆向重建源码
- th06-sdl2 及相关社区工作：SDL2 与跨平台移植基础
- [SDL](https://www.libsdl.org/)：窗口、输入、音频与平台支持
- 上海爱丽丝幻乐团 / ZUN：《东方红魔乡》原作及其全部游戏内容

## 版权与免责声明

本仓库是非官方的技术研究和移植项目。原作名称、角色、音乐、图像及其他游戏内容的权利归其各自权利人所有。请仅使用自己合法取得并有权使用的游戏数据；不要借助本项目传播未经授权的原作内容。

源码授权信息见 [`LICENSE`](LICENSE)。第三方组件可能使用各自的许可证，请同时查看相应目录中的许可文件。本软件按现状提供，不保证适用于特定设备、系统或网络环境，使用前请自行备份存档和 Replay。
