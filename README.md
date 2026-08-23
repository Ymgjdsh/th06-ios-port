# 東方紅魔郷 SDL2 移植版 / th06-sdl2

> ⚠️ **归档通知 / Archived**
>
> 由于作者精力有限,本项目进入归档期。作者不会持续维护这个项目,仅在有兴致时随缘处理。
>
> Due to limited bandwidth, this project is now in archive mode. The author will no longer actively maintain it and will only touch it occasionally when the mood strikes.

**[English](#english)** | **[中文](#中文)**

---

<a id="english"></a>

## English

### About

This is a cross-platform port of [Touhou Koumakyou ~ the Embodiment of Scarlet Devil (東方紅魔郷)](https://en.touhouwiki.net/wiki/Embodiment_of_Scarlet_Devil) v1.02h, built on top of the [community-reconstructed source code](https://github.com/happyhavoc/th06). Direct3D 8 and the Win32 window/audio stack have been replaced with **SDL2** and a set of pluggable renderers.

### Rendering backends (runtime-selectable)

Three backends are compiled into the same executable. The active one is chosen by `--backend=...` on startup and clamped to whatever the device actually supports:

| Backend | Source | Default on | Notes |
|---|---|---|---|
| **Desktop OpenGL (fixed-function)** | `src/sdl2_renderer.cpp` | Windows / Linux (x86) | Closest to the original D3D8 behaviour; the `NoGouraud`-style legacy flags only apply here |
| **OpenGL ES 2.0 (shader)** | `src/RendererGLES.cpp` + `src/gles_shaders.h` | Android | Runs on any GLES2-capable GPU; used for the Android APK and for desktops without a working GL 2.1 |
| **Vulkan 1.x** | `src/RendererVulkan.cpp` + `src/vulkan/` | opt-in | Loaded via [volk](https://github.com/zeux/volk), allocator via [VMA](https://gpuopen.com/vulkan-memory-allocator/); enabled at build time with `-DTH06_USE_VULKAN=ON` |

Pick one at runtime:

```
th06.exe --backend=gl        # desktop fixed-function GL
th06.exe --backend=gles      # GLES 2.0 shader path
th06.exe --backend=vulkan    # Vulkan (requires TH06_USE_VULKAN build)
```

The thprac config window (**Extra config → Renderer**) exposes the same choice and persists it to `th06.cfg`. Backends not compiled in or not available on the device are hidden from the radio set.

### Other features

- **SDL2_mixer** for BGM/SE, **SDL2_image** for runtime textures (PNG/BMP)
- **OGG BGM**, legacy WAV tree, and MIDI playback (through `SoundPlayer` / `MidiOutput`)
- **Runtime text-encoding detection** so the same binary can read GBK (Chinese) and Shift-JIS (Japanese) game data without a rebuild
- **thprac practice overlay** — stage select, practice tools, renderer/quality toggles, rendered with Dear ImGui
- **Runtime i18n** (`src/i18n.tpl` + `scripts/gen_i18n.py` → `i18n.hpp`)
- **PBG3 archive reader** in `src/pbg3/` for the original `.dat` files
- **Authoritative-model netplay** in `src/Netplay*` (prediction/rollback path is gated behind `-DTH06_ENABLE_PREDICTION_ROLLBACK=ON`; off by default)
- **Crash handler + watchdog** on both Windows (`CrashHandlerWin` / `WatchdogWin`) and POSIX
- **Android port** with its own Gradle project (see below)
- **CMake** build system with per-backend feature gates

### Requirements

- CMake ≥ 3.20
- C++17 compiler: MSVC (Visual Studio 2022) on Windows, GCC 13+ or Clang on Linux
- Python 3 for the `i18n.hpp` and encoding-table generators
- **Game data files** from `東方紅魔郷.exe` v1.02h — see *Running*
- *For the Vulkan backend:* LunarG Vulkan SDK (`VULKAN_SDK` env var) so CMake can locate headers and `glslangValidator`
- *For Android:* Android SDK + NDK `21.4.7075529` and JDK 17

The game file format and many structs assume `sizeof(void*) == 4`. Non-MSVC builds are therefore forced to `-m32`.

### Building

Windows (x86, default GL + GLES, no Vulkan):

```
cmake -B build_sdl2 -A Win32
cmake --build build_sdl2 --config Release --target th06
```

Windows (x86, GL + GLES + Vulkan):

```
cmake -B build_vk -A Win32 -DTH06_USE_VULKAN=ON
cmake --build build_vk --config Release --target th06
```

Linux (32-bit, GL + GLES):

```
PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig \
cmake -B build_linux_gles -DCMAKE_BUILD_TYPE=Release
cmake --build build_linux_gles --target th06
```

You will need the i386 dev packages: `sudo apt install gcc-multilib g++-multilib libsdl2-dev:i386 libsdl2-image-dev:i386 libsdl2-mixer-dev:i386 libgl-dev:i386`.

Android (Gradle wrapper drives CMake via the NDK):

```
cd android
./gradlew assembleRelease        # -> android/app/build/outputs/apk/release/app-release.apk
```

The APK only ships `armeabi-v7a` (for the `sizeof(void*) == 4` reason above). Set `TH06_STORE_PASSWORD` / `TH06_KEY_PASSWORD` env vars before building if you replace the bundled debug keystore.

### Running

The game needs access to the original data files (`th06*_CM.DAT`, `...`_IN.DAT`, `..._MD.DAT`, `..._ST.DAT`, etc.) and optional OGG BGM under `BGM/`. Place them alongside the executable or run the executable from the data directory:

```
cd <path-to-game-data>
<path-to>/build_sdl2/Release/th06.exe --backend=gl
```

The Android build bundles the DAT files + OGG BGM into the APK (`android/app/src/main/assets/`); the `syncGameAssets` Gradle task copies them from the Windows build directory before packaging.

### Project layout

```
CMakeLists.txt              # top-level build (th06 + vk_smoketest + th06_sdl2_test)
3rdparty/
  SDL2/ SDL2_image/ SDL2_mixer/   # Windows prebuilts; Linux uses pkg-config
  imgui/                          # thprac overlay + ImGui GL2/GL3/Vulkan backends
  volk/  Vulkan/                  # Vulkan loader + headers/VMA
  Detours/ rapidjson/ zstd/ munit/
src/
  main.cpp                  # CLI parse, backend selection, restart loop
  sdl2_renderer.cpp         # Desktop OpenGL backend (IRenderer impl)
  RendererGLES.cpp          # OpenGL ES 2.0 backend (FBO-based fullscreen scaler)
  gles_shaders.h            # Embedded GLSL ES shader source
  RendererVulkan.cpp        # Vulkan backend (IRenderer impl)
  vulkan/                   # VkContext / VkSwapchain / VkFrameContext /
                            # VkRenderTarget / VkPipelineCache / VkResources /
                            # VkTextureManager / VmaUsage
  IRenderer.hpp             # Backend abstraction + runtime selection glue
  BackendAvailability.cpp   # Probe / ResolveBackend / PlatformDefaultBackend
  GameWindow.cpp            # SDL2 window, frame timing, renderer switch
  SoundPlayer.cpp           # BGM/SE via SDL2_mixer
  MidiOutput.cpp            # MIDI route
  TextHelper.cpp            # stb_truetype + runtime GBK/Shift-JIS detection
  AnmManager, BulletManager, EnemyManager, Player, Stage,
  EclManager, GameManager, Supervisor, ScreenEffect, ...
                            # game logic ported from the D3D8 reconstruction
  Netplay*                  # authoritative-model netplay + (optional) rollback
  thprac_*                  # practice-mode overlay (ImGui)
  pbg3/                     # PBG3 archive reader
  CrashHandlerWin/Posix, WatchdogWin/Posix
  AndroidTouchInput, TouchVirtualButtons, MenuTouchButtons
shaders_vk/                 # GLSL sources for the Vulkan backend (glslangValidator)
android/                    # Android Gradle project (SDL2 Java glue + JNI CMake)
  app/src/main/java/com/th06/game/   # CompatTouchOverlay, GamePerformanceService,
                                     # SessionLogCollector (Android-specific
                                     # quality-of-life work; applicationId is
                                     # tianqi233.th06.game)
scripts/                    # gen_i18n.py, gen_encoding_tables.py, Ghidra export,
                            # decomp/objdiff pipeline helpers
config/                     # decomp mapping data (mapping.csv, globals.csv, ...)
tools/                      # vk_smoketest (Vulkan gating harness) + relay_service
tests/                      # minimal munit-based tests (PBG3)
```

### Platform notes

- **Android — tested on Mali GPUs after #5**: fragment shaders have no default `int` precision in GLSL ES 1.00, so the GLES preamble injects `precision highp float; precision highp int;` from `CompileShader` to keep VS/FS uniform precision matching on strict drivers (Mali-G51 etc.). `gles_shaders.h` is kept source-clean so desktop GL is unaffected.
- **Android — vivo OriginOS "PEM" freezing**: the app declares a `FOREGROUND_SERVICE_SPECIAL_USE` service (`GamePerformanceService`) plus a persistent notification to stop the OS from cgroup-freezing the process while it is supposed to be interactive.
- **Android — touch dispatch on Android 12+**: a `CompatTouchOverlay` sits on top of the SDL surface to bypass BLAST-pipeline input-throttling and recover dropped historical events.
- **Windows x86 Release**: core gameplay translation units are built with `/arch:IA32 /fp:strict /Ob1 /Oy- /Od` to stay close to the original VC7 codegen so stock replays / demos stay bit-for-bit compatible.

### Credits

- [happyhavoc/th06](https://github.com/happyhavoc/th06) — reverse-engineered baseline source
- [Team Shanghai Alice](https://www16.big.or.jp/~zun/) / ZUN — original game
- [SDL2](https://www.libsdl.org/), [Vulkan](https://www.khronos.org/vulkan/), [Dear ImGui](https://github.com/ocornut/imgui), [volk](https://github.com/zeux/volk), [VMA](https://gpuopen.com/vulkan-memory-allocator/), [zstd](https://facebook.github.io/zstd/), [rapidjson](https://rapidjson.org/)

### ⚠️ Developer Disclaimer (A Note on this Project's "Silicon Content")

> **TL;DR:** Yes, this project is 100% **vibe-coded**!
>
> Confession time: A significant portion of the underlying code in this repo was generated with the help of LLMs. If you're scrolling through the commit history and catch a strong whiff of "AI," trust your instincts — your radar is spot on.
>
> My primary goal was straightforward: **achieve cross-platform compatibility** and get the game running smoothly with **full hardware-accelerated rendering on modern machines**, finally breaking free from the ancient shackles of D3D8. To quickly validate this idea and hit that goal, I happily outsourced all the tedious C++ grunt work, API swapping, and low-level duct-taping to my AI assistant.
>
> The core philosophy here is simple: "The code might look a bit abstract, but hey, it actually runs on modern hardware." 🛠️

---

<a id="中文"></a>

## 中文

### 项目简介

本仓库是 [東方紅魔郷 ～ the Embodiment of Scarlet Devil](https://en.touhouwiki.net/wiki/Embodiment_of_Scarlet_Devil) v1.02h 的跨平台移植,基于社区 [逆向重建的源代码](https://github.com/happyhavoc/th06)。Direct3D 8 和 Win32 的窗口/音频栈被替换成了 **SDL2** 以及一组可切换的渲染后端。

### 渲染后端(运行时可切换)

三种后端都编进同一份可执行文件,启动时用 `--backend=...` 选择,实际使用的后端会被 "device 支持性检测" 再夹紧一次:

| 后端 | 源码 | 默认平台 | 备注 |
|---|---|---|---|
| **Desktop OpenGL(固定管线)** | `src/sdl2_renderer.cpp` | Windows / Linux(x86) | 行为最接近原版 D3D8;`NoGouraud` 等遗留选项仅在该路径生效 |
| **OpenGL ES 2.0(Shader)** | `src/RendererGLES.cpp` + `src/gles_shaders.h` | Android | 覆盖所有 GLES2 设备;Android APK 与桌面端 GL 2.1 不可用时走这一条 |
| **Vulkan 1.x** | `src/RendererVulkan.cpp` + `src/vulkan/` | 可选 | 通过 [volk](https://github.com/zeux/volk) 运行期加载,分配器使用 [VMA](https://gpuopen.com/vulkan-memory-allocator/);需 `-DTH06_USE_VULKAN=ON` |

运行时选择:

```
th06.exe --backend=gl        # 桌面固定管线 GL
th06.exe --backend=gles      # GLES 2.0 Shader 路径
th06.exe --backend=vulkan    # Vulkan(需 TH06_USE_VULKAN 构建)
```

thprac 的扩展配置面板(**扩展配置 → 渲染器**)也能切换,并写回 `th06.cfg`。当前设备/构建不可用的后端会从单选按钮里自动隐藏。

### 其他特性

- **SDL2_mixer** 负责 BGM/SE;**SDL2_image** 负责运行期纹理加载(PNG/BMP)
- 支持 **OGG BGM**、旧版 WAV 目录和 **MIDI**(`SoundPlayer` / `MidiOutput`)
- **运行时编码检测**:同一份二进制可直接读取 GBK(中文版)与 Shift-JIS(日文版)的游戏数据,无需重新编译
- **thprac 练习模式覆盖层**:关卡选择、练习工具、渲染/画质选项,使用 Dear ImGui 渲染
- **运行时 i18n**:`src/i18n.tpl` + `scripts/gen_i18n.py` 生成 `i18n.hpp`
- **PBG3 归档解析器** 位于 `src/pbg3/`,用于读取原版 `.dat`
- **权威模型联机** 位于 `src/Netplay*`,预测回滚路径由 `-DTH06_ENABLE_PREDICTION_ROLLBACK=ON` 开启,默认关闭
- **崩溃处理 + Watchdog**:Windows(`CrashHandlerWin` / `WatchdogWin`)与 POSIX 两套实现
- **Android 移植**:独立 Gradle 工程(详见下文)
- **CMake** 构建系统,按后端分别开关

### 环境要求

- CMake ≥ 3.20
- C++17 编译器:Windows 下 MSVC(Visual Studio 2022),Linux 下 GCC 13+ 或 Clang
- Python 3(用于生成 `i18n.hpp` 与编码表)
- **原版 `東方紅魔郷.exe` v1.02h 的游戏数据文件** — 见下文"运行"
- *启用 Vulkan 后端:* LunarG Vulkan SDK(设置 `VULKAN_SDK` 环境变量),CMake 需要 headers 和 `glslangValidator`
- *Android:* Android SDK + NDK `21.4.7075529` + JDK 17

游戏文件格式与大量结构体假设 `sizeof(void*) == 4`,所以 GCC/Clang 路径会强制 `-m32`。

### 构建

Windows(x86,默认 GL + GLES,不带 Vulkan):

```
cmake -B build_sdl2 -A Win32
cmake --build build_sdl2 --config Release --target th06
```

Windows(x86,GL + GLES + Vulkan):

```
cmake -B build_vk -A Win32 -DTH06_USE_VULKAN=ON
cmake --build build_vk --config Release --target th06
```

Linux(32 位,GL + GLES):

```
PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig \
cmake -B build_linux_gles -DCMAKE_BUILD_TYPE=Release
cmake --build build_linux_gles --target th06
```

需先装好 i386 dev 包:`sudo apt install gcc-multilib g++-multilib libsdl2-dev:i386 libsdl2-image-dev:i386 libsdl2-mixer-dev:i386 libgl-dev:i386`。

Android(Gradle wrapper 会通过 NDK 驱动 CMake):

```
cd android
./gradlew assembleRelease        # 产物:android/app/build/outputs/apk/release/app-release.apk
```

APK 仅包含 `armeabi-v7a`(同前面那个 `sizeof(void*) == 4` 的原因)。如果要替换内置的 debug keystore,构建前设置 `TH06_STORE_PASSWORD` / `TH06_KEY_PASSWORD` 环境变量即可。

### 运行

游戏需要访问原版数据文件(`th06*_CM.DAT`、`..._IN.DAT`、`..._MD.DAT`、`..._ST.DAT` 等)以及 `BGM/` 下可选的 OGG 音乐。把它们放在可执行文件旁边,或者直接从游戏数据目录启动:

```
cd <游戏数据路径>
<路径>/build_sdl2/Release/th06.exe --backend=gl
```

Android 构建会把 DAT + OGG BGM 一起打进 APK 的 `android/app/src/main/assets/`;Gradle 里的 `syncGameAssets` 任务会在打包前从 Windows 构建目录同步这些资源。

### 项目结构

```
CMakeLists.txt              # 顶层构建(th06 + vk_smoketest + th06_sdl2_test)
3rdparty/
  SDL2/ SDL2_image/ SDL2_mixer/   # Windows 内置预编译库;Linux 走 pkg-config
  imgui/                          # thprac 覆盖层 + ImGui GL2/GL3/Vulkan 后端
  volk/  Vulkan/                  # Vulkan 加载器 + headers/VMA
  Detours/ rapidjson/ zstd/ munit/
src/
  main.cpp                  # CLI 解析、后端选择、重启循环
  sdl2_renderer.cpp         # Desktop OpenGL 后端(IRenderer 实现)
  RendererGLES.cpp          # OpenGL ES 2.0 后端(基于 FBO 的全屏缩放)
  gles_shaders.h            # 内嵌 GLSL ES shader 源
  RendererVulkan.cpp        # Vulkan 后端(IRenderer 实现)
  vulkan/                   # VkContext / VkSwapchain / VkFrameContext /
                            # VkRenderTarget / VkPipelineCache / VkResources /
                            # VkTextureManager / VmaUsage
  IRenderer.hpp             # 后端抽象 + 运行时选择
  BackendAvailability.cpp   # Probe / ResolveBackend / PlatformDefaultBackend
  GameWindow.cpp            # SDL2 窗口、帧计时、渲染器切换
  SoundPlayer.cpp           # SDL2_mixer 做 BGM/SE
  MidiOutput.cpp            # MIDI 路径
  TextHelper.cpp            # stb_truetype + 运行时 GBK/Shift-JIS 检测
  AnmManager, BulletManager, EnemyManager, Player, Stage,
  EclManager, GameManager, Supervisor, ScreenEffect, ...
                            # 从 D3D8 逆向版移植过来的游戏逻辑
  Netplay*                  # 权威模型联机 +(可选)回滚
  thprac_*                  # 练习模式覆盖层(ImGui)
  pbg3/                     # PBG3 归档解析
  CrashHandlerWin/Posix, WatchdogWin/Posix
  AndroidTouchInput, TouchVirtualButtons, MenuTouchButtons
shaders_vk/                 # Vulkan 后端的 GLSL 源(glslangValidator 编译)
android/                    # Android Gradle 工程(SDL2 Java 胶水 + JNI CMake)
  app/src/main/java/com/th06/game/   # CompatTouchOverlay、GamePerformanceService、
                                     # SessionLogCollector 等 Android 端稳定性代码
                                     # (applicationId = tianqi233.th06.game)
scripts/                    # gen_i18n.py、gen_encoding_tables.py、Ghidra 导出脚本、
                            # 反编译/objdiff 流水线辅助
config/                     # 反编译映射数据(mapping.csv、globals.csv 等)
tools/                      # vk_smoketest(Vulkan Gate 用的独立测试)+ relay_service
tests/                      # 基于 munit 的最小测试(PBG3)
```

### 平台小贴士

- **Android — 针对 Mali GPU 的 #5 修复**:GLSL ES 1.00 的片段着色器 `int` 没有默认精度,`CompileShader` 里统一在 VS/FS 前注入 `precision highp float; precision highp int;` 前缀,让 Mali-G51 之类严格派驱动认可 uniform 精度匹配。`gles_shaders.h` 保持不改,桌面 GL 路径不受影响。
- **Android — 对抗 vivo OriginOS 的 PEM 冻结**:AndroidManifest 声明了 `FOREGROUND_SERVICE_SPECIAL_USE` 前台服务(`GamePerformanceService`)+ 常驻通知,阻止系统在交互中途把进程放进 `/background` cgroup。
- **Android — Android 12+ 触控**:在 SDL surface 上叠了一层 `CompatTouchOverlay`,用于绕过 BLAST 渲染管线带来的输入节流,并补回被系统吞掉的 historical events。
- **Windows x86 Release**:核心玩法 .cpp 用 `/arch:IA32 /fp:strict /Ob1 /Oy- /Od` 编译,尽量贴近原版 VC7 代码生成,让标准 replay / demo 保持位级兼容。

### 致谢

- [happyhavoc/th06](https://github.com/happyhavoc/th06) — 原始逆向重建源码
- [上海爱丽丝幻乐团](https://www16.big.or.jp/~zun/) / ZUN — 原版游戏
- [SDL2](https://www.libsdl.org/)、[Vulkan](https://www.khronos.org/vulkan/)、[Dear ImGui](https://github.com/ocornut/imgui)、[volk](https://github.com/zeux/volk)、[VMA](https://gpuopen.com/vulkan-memory-allocator/)、[zstd](https://facebook.github.io/zstd/)、[rapidjson](https://rapidjson.org/)

### ⚠️ 开发者叠甲时间(关于本项目含硅量的声明)

> **太长不看版:** 没错,这是一个纯正的 **Vibe-coded(凭感觉编程)** 项目!
>
> 坦白局:本项目含有大量由 LLM 辅助生成的底层代码。如果你在翻看 commit 记录时闻到了一股浓浓的"AI 味",自信点,你的直觉非常准确。
>
> 我的核心目的其实非常纯粹:**实现跨平台运行**,并让游戏在**现代机器上能顺利跑满硬件加速**,彻底摆脱古老的 D3D8 的束缚。为了快速达成这个目标,那些繁琐的 C++ 搬砖活儿、API 替换和底层缝合,就全权委托给 AI 助理了。
>
> 主打一个"代码虽然抽象,但它在现代机器上真能跑"。🛠️
