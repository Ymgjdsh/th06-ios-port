#!/usr/bin/env python3
"""Validate the complete iOS 14 netplay source tree before packaging."""

from __future__ import annotations

import argparse
import pathlib
import sys
import xml.etree.ElementTree as element_tree


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []

    def read(relative: str) -> str:
        path = root / relative
        if not path.is_file():
            failures.append(f"missing source file: {relative}")
            return ""
        return path.read_text(encoding="utf-8", errors="strict")

    cmake = read("CMakeLists.txt")
    build = read("ios/build_ios.sh")
    plist = read("ios/Info.plist.in")
    session = read("src/NetplaySession.cpp")
    session_header = read("src/NetplaySession.hpp")
    transport = read("src/NetplayTransport.cpp")
    internal = read("src/NetplayInternal.hpp")
    menu = read("src/OnlineMenu.cpp")
    renderer = read("src/RendererGLES.cpp")
    game_window = read("src/GameWindow.cpp")
    touch_buttons = read("src/TouchVirtualButtons.cpp")
    touch_input_header = read("src/AndroidTouchInput.hpp")
    touch_input = read("src/AndroidTouchInput.cpp")
    permission_probe = read("ios/LocalNetworkPermission.mm")
    bluetooth = read("ios/BluetoothPeerTransport.mm")
    controller = read("src/Controller.cpp")
    imgui_sdl = read("3rdparty/imgui/imgui_impl_sdl.cpp")
    shell = read("src/NetplayShell.cpp")
    presentation = read("src/NetplayAuthoritativePresentation.cpp")
    text_helper = read("src/TextHelper.cpp")
    gui_header = read("src/Gui.hpp")
    gui = read("src/Gui.cpp")
    result_header = read("src/ResultScreen.hpp")
    result = read("src/ResultScreen.cpp")
    replay = read("src/ReplayManager.cpp")
    main_menu = read("src/MainMenu.cpp")
    ecl = read("src/EclManager.cpp")
    sound = read("src/SoundPlayer.cpp")
    zwave = read("src/zwave.cpp")
    supervisor = read("src/Supervisor.cpp")
    practice = read("src/thprac_th06.cpp")
    main_source = read("src/main.cpp")

    markers = (
        ("iOS version", cmake, 'TH06_IOS_VERSION "1.2.5"'),
        ("iOS build", cmake, 'TH06_IOS_BUILD "22"'),
        ("build script version", build, "IOS_VERSION=${IOS_VERSION:-1.2.5}"),
        ("build script build", build, "IOS_BUILD=${IOS_BUILD:-22}"),
        ("stable lockstep default", cmake,
         'TH06_ENABLE_PREDICTION_ROLLBACK "编译预测回滚联机代码" OFF'),
        ("stable lockstep iOS build", build, "-DTH06_ENABLE_PREDICTION_ROLLBACK=OFF"),
        ("iPhone and iPad target", cmake, 'XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"'),
        ("JPEG decoder", cmake, "set(SDL2IMAGE_JPG ON"),
        ("arm64 target", build, "CMAKE_OSX_ARCHITECTURES=arm64"),
        ("iOS 14 deployment", build, "CMAKE_OSX_DEPLOYMENT_TARGET=14.0"),
        ("local-network permission", plist, "NSLocalNetworkUsageDescription"),
        ("Bonjour permission declaration", plist, "NSBonjourServices"),
        ("Bonjour service type", plist, "_th06-netplay._udp"),
        ("nearby service type", plist, "_th06-peer._tcp"),
        ("Bluetooth permission", plist, "NSBluetoothAlwaysUsageDescription"),
        ("portrait support", plist, "UIInterfaceOrientationPortrait"),
        ("native permission trigger source", cmake, "ios/LocalNetworkPermission.mm"),
        ("native Bonjour permission trigger", permission_probe, "searchForServicesOfType"),
        ("Bonjour host publication", permission_probe, "TH06_IOS_StartBonjourHost"),
        ("Bonjour host result", permission_probe, "TH06_IOS_PollBonjourHost"),
        ("Bonjour IPv6 discovery", permission_probe, "AF_INET6"),
        ("Bonjour scoped IPv6 discovery", permission_probe, "if_indextoname"),
        ("Bonjour hostname fallback", permission_probe, "using hostname fallback"),
        ("Bonjour publication success callback", permission_probe, "netServiceDidPublish"),
        ("Bonjour publication failure callback", permission_probe, "didNotPublish"),
        ("permission trigger diagnostic", permission_probe, "[local-network] Bonjour permission probe start"),
        ("permission state query", permission_probe, "TH06_IOS_GetLocalNetworkPermissionState"),
        ("permission probe idempotence", permission_probe, "if (_searching)"),
        ("shared protocol", session_header, "kProtocolVersion = 3806"),
        ("discovery API", session_header, "StartLanDiscovery"),
        ("discovery request", session, "TH06_DISCOVER"),
        ("discovery retry window", session, "kLanDiscoveryDurationMs = 10000"),
        ("discovery permission gate", session, "permissionWaitDeadlineTick"),
        ("discovery stale probe cleanup", session, "TH06_IOS_StopLocalNetworkPermissionProbe"),
        ("directed broadcast", session, "ifa_broadaddr"),
        ("host discovery response", transport, "TH06_OFFER"),
        ("automatic join", menu, "LAN discovery auto-joined host"),
        ("search uses visible LAN host port", menu, "StartLanDiscovery(g_State.hostPort"),
        ("single visible room port", menu, "##online_room_port"),
        ("nearby jitter buffer", menu, "constexpr int kNearbyDelay = 3;"),
        ("touchable LAN search", menu, "Search & join LAN"),
        ("nearby Bluetooth menu", menu, "Bluetooth nearby"),
        ("nearby Bluetooth transport", transport, "TH06_IOS_BluetoothSend"),
        ("nearby native bridge", bluetooth, "MCNearbyServiceAdvertiser"),
        ("nearby browser discovery callback", bluetooth, "foundPeer:(MCPeerID *)peerID"),
        ("Xcode 14 compatible nearby state initialization", bluetooth, 'status = "idle";'),
        ("nearby browser failure callback", bluetooth, "didNotStartBrowsingForPeers"),
        ("nearby stale-session guard", bluetooth, "session != self->session"),
        ("nearby reliable checkpoint", transport, "pack.ctrl.frame % 6"),
        ("quiet iOS joystick polling", controller, "nextIOSJoystickProbeTick"),
        ("quiet ImGui gamepad polling", imgui_sdl, "SDL_WasInit(SDL_INIT_GAMECONTROLLER)"),
        ("early permission probe", menu, "TH06_IOS_TriggerLocalNetworkPermission();"),
        ("Windows IPv4 LAN host preference", session, "const bool hostStarted = g_State.host.Start(\"\", listenPort, AF_INET)"),
        ("visible LAN window", menu, "ImVec2(450.0f, 460.0f)"),
        ("iPad edge controls", renderer, "hasPillarbox"),
        ("portrait gameplay renderer", renderer, "portraitGameplay"),
        ("orientation hint", game_window, "Portrait PortraitUpsideDown"),
        ("4:3 iPad touch hitbox", touch_buttons, "hitCenterX"),
        ("local-only menu tap API", touch_input_header, "ConsumeLocalTap"),
        ("local-only menu tap capture", touch_input, "g_LocalTapPending = true"),
        ("single-player displacement touch path", touch_input, "g_TouchAnalogInput.mode = AnalogMode::Displacement"),
        ("isolated synchronized touch queue", touch_input, "g_SynchronizedTapPending"),
        ("local touch presentation prediction", presentation, "TouchFrameData::kFlagAnalog"),
        ("lockstep presentation reconciliation", session, "SyncLocalPresentation"),
        ("netplay displacement always enabled", menu, "bool IsNetplayDisplacementDisabled()"),
        ("remote retry touch selection", shell, "HitMenuItem(RETRY_MENU_SPRITE_YES"),
        ("remote pause return-title touch", shell, "HitMenuItem(GAME_MENU_SPRITE_CURSOR_QUIT"),
        ("guest touch confirmation ordering", shell, "pendingTouchConfirmShellSerial"),
        ("responsive retry menu input", shell, "const bool inputReady = phaseFrames >= 4;"),
        ("dynamic font edge sharpening", text_helper, "sharpenedAlpha"),
        ("drawable FBO preservation", renderer, "SDL drawable FBO"),
        ("NPOT surface clamp", renderer, "640x480 NPOT textures"),
        ("opaque surface copy", renderer, "D3D's surface copy overwrites"),
        ("wire packet size", internal, "sizeof(Pack) == 437"),
        ("32-bit MSG offsets", gui_header, "instrOffsets[1]"),
        ("bounded MSG parsing", gui, "IsMsgFileRangeValid"),
        ("MSG diagnostics", gui, "[MsgProbe]"),
        ("fixed-width score.dat header", result_header, "OnDiskScoreDat"),
        ("bounded score.dat records", result, "NextScoreRecord"),
        ("broken arm64 score.dat migration", result, "broken64-migrated"),
        ("score.dat diagnostics", result, "[ScoreProbe]"),
        ("bounded Replay layout", replay, "ValidateReplayLayout"),
        ("Replay file-size cap", replay, "kMaxReplayFileSize"),
        ("Replay diagnostics", replay, "[ReplayProbe]"),
        ("Replay selection null guard", main_menu, "currentReplay != NULL"),
        ("Boss effect color bound", ecl, "EFFECT_COLOR_COUNT = 28"),
        ("lazy iOS sound-effect state", sound, "g_IosSoundLoadAttempted"),
        ("BGM loop-end guard", zwave, "effectiveEnd == 0"),
        ("scene transition diagnostics", supervisor, "[StateProbe]"),
        ("Result diagnostics", result, "[ResultProbe]"),
        ("F11 menu diagnostics", practice, "[F11Probe]"),
        ("quiet default iOS log level", main_source, "SDL_LOG_PRIORITY_INFO"),
        ("bounded accurate-input grace wait", read("src/NetplayRollback.cpp"),
         "kAccurateInputGraceWaitMs"),
        ("repeated-frame FBO hold", game_window,
         "ShouldHoldRepeatedFramePresentation"),
        ("observable netplay pacing", read("src/NetplayRollback.cpp"),
         "grace_recovered="),
    )
    for label, text, marker in markers:
        if marker not in text:
            failures.append(f"missing {label}: {marker}")

    for incompatible_initializer in (
            "bool hostRole = false;",
            "bool connected = false;",
            'std::string status = "idle";'):
        if incompatible_initializer in bluetooth:
            failures.append(
                "Xcode 14 incompatible Objective-C++ ivar initializer: "
                f"{incompatible_initializer}")

    forbidden_markers = (
        ("invalid Multipeer browser callback selector", bluetooth,
         "didDiscoverPeer:(MCPeerID *)peerID"),
        ("native pointer MSG table", gui_header, "MsgRawInstr *instrs[1]"),
        ("unchecked Replay stage selection", main_menu,
         "currentReplay[this->cursor].stageReplayData"),
        ("native-pointer score.dat header cast", result,
         "sd = (ScoreDat *)fileBuffer"),
        ("verbose default iOS logging", main_source,
         "SDL_LOG_PRIORITY_VERBOSE"),
        ("duplicate LAN listen-port field", menu, "##online_listen_port"),
    )
    for label, text, marker in forbidden_markers:
        if marker in text:
            failures.append(f"forbidden regression ({label}): {marker}")

    plist_path = root / "ios/Info.plist.in"
    if plist_path.is_file():
        try:
            element_tree.parse(plist_path)
        except (element_tree.ParseError, OSError) as error:
            failures.append(f"invalid ios/Info.plist.in: {error}")

    for dependency in ("SDL2", "SDL2_image", "SDL2_mixer"):
        if not (root / "ios/vendor" / dependency / "CMakeLists.txt").is_file():
            failures.append(f"missing vendored dependency: {dependency}")

    assets = root / "ios/assets"
    bgm = root / "ios/bgm"
    dat_count = len(list(assets.glob("*.DAT"))) if assets.is_dir() else 0
    music_count = 0
    wav_count = 0
    music_size = 0
    if bgm.is_dir():
        music_files = [path for path in bgm.iterdir() if path.is_file()]
        music_count = sum(1 for path in music_files
                          if path.suffix.lower() == ".ogg")
        wav_count = sum(1 for path in music_files
                        if path.suffix.lower() == ".wav")
        music_size = sum(path.stat().st_size for path in music_files
                         if path.suffix.lower() in {".ogg", ".wav"})
    if dat_count < 7:
        failures.append(f"game data incomplete: expected at least 7 DAT, found {dat_count}")
    if music_count != 17:
        failures.append(f"BGM incomplete: expected 17 OGG tracks, found {music_count}")
    if wav_count:
        failures.append(f"uncompressed BGM found: remove {wav_count} WAV tracks")
    if music_size > 80 * 1024 * 1024:
        failures.append(
            f"BGM is unexpectedly large: {music_size / 1024 / 1024:.1f} MiB")

    if failures:
        print("iOS 14 source preflight failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    source_count = len(list((root / "src").glob("*.cpp")))
    print(f"iOS 14 source preflight passed ({source_count} top-level C++ files)")
    print(f"Netplay protocol 3806, wire packet 437 bytes, DAT={dat_count}, "
          f"OGG={music_count}, BGM={music_size / 1024 / 1024:.1f} MiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
