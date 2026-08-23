# iOS 14 / TrollStore build

This target builds a native arm64 iPad application with SDL2 and OpenGL ES 2.
It does not use an emulator and it does not require App Store submission.

Requirements:

- macOS with Xcode 14 and its command line tools selected
- CMake 3.20 or newer
- Python 3
- The bundled `ios/assets` and `ios/bgm` directories, prepared from the
  user-provided game installation and Android package

Build from Terminal on the Mac:

```sh
cd /path/to/th06-ios14-netplay-v1.2.5-source
chmod +x ios/build_ios.sh ios/package_ipa.sh
./ios/build_ios.sh
```

Build from the Windows project directory and place the verified IPA on a
remote Mac desktop:

```powershell
powershell -ExecutionPolicy Bypass -File .\ios\build_on_mac.ps1 `
  -MacHost 10.0.0.142 -MacUser dick -IosVersion 1.2.5 -IosBuild 20
```

The remote builder uses the `th07_mac` OpenSSH key by default, creates a
unique temporary run directory under `~/th06-build`, validates the IPA, then
atomically installs it as `~/Desktop/th06-ios-1.2.5-20-<commit>.ipa`. Source
archives, Xcode/CMake build directories, and temporary extraction files are
removed from the Mac after success or failure. A failed build leaves only
diagnostic logs under the local `dist/mac-build` directory.

`ASSET_APK`, `ASSET_DIR`, and `BGM_DIR` remain available as overrides when a
different lawful game data set should be packaged.

The result is a uniquely named IPA such as
`build-ios/th06-ios-1.2.5-20.ipa`. Install it with TrollStore. The bundle
identifier is `com.th06.sdl2.ios`; save/config/replay files are written under
the app's iOS data container via `SDL_GetPrefPath`.

Version 1.2.5 adds local presentation prediction for remote netplay. Each
device displays its own touch displacement immediately, while the canonical
simulation and the peer still consume the exact serialized trajectory at the
configured delay. Remote analog data is routed only to the peer player's lane
and can no longer overwrite local touch state. Synchronized UI taps/swipes use
a separate queue, preserving dialogue, result, and Replay touch behavior. The
pause quit confirmation also uses larger stable touch targets during its menu
animation.

Version 1.2.5 retains the four-mode launcher introduced in 1.2.3: Nearby
LAN, Direct address, Relay room, and Bluetooth nearby devices. Nearby LAN uses
Bonjour for iOS discovery and waits for local-network authorization before any
UDP fallback, avoiding the iOS 15 `sendto` error 65 race seen after a second
launch. The launcher fills the discovered address automatically and starts the
Guest connection. Tap the searching button again to cancel; closing the launcher
cleans up the prior socket, Bonjour probe, and published host service.

For a LAN game, put both devices on the same Wi-Fi. On the host choose `Start as
host`; on the other device choose `Nearby LAN`, enter the same `Room port` shown
in the host's launcher (for example `3037`), and then choose `Search & join LAN`.
The launcher uses one port for discovery and both local sockets. On the first search, tap
`Allow` when iOS asks for local-network access. If it was denied earlier, enable
`Settings > Privacy > Local Network > Touhou 06` and restart the search. The
router must allow wireless clients to communicate with one another; guest or
public Wi-Fi networks often enable client isolation and cannot be discovered.

Version 1.2.5 also retains iPhone/iPad portrait orientation, the portrait
scoreboard/playfield layout, and virtual buttons on 4:3 iPads. It retains JPEG
decoding for title/menu/result backgrounds and
packages the 17 BGM tracks as OGG. The source validator rejects stale WAV files
so a reused build directory cannot silently produce a 300+ MiB IPA again.
It also forces GLES 2.0-compatible clamp sampling for the 640x480 JPEG surfaces
and draws them as opaque framebuffer copies. The build script performs a clean
build by default and rejects binaries that do not contain the 3.9.0 netplay UI
marker. In remote netplay, gameplay dragging uses the same pixel-displacement
touch path as single-player. Pause and retry choices accept a direct single
tap; guest confirmation is sent only after the host acknowledges the selected
row, so reordered UDP packets cannot confirm the previous choice. Retry input
no longer waits through the old 30-frame default-selection delay. Dynamic game
text keeps the same content and layout while using higher-contrast antialiased
edges for a clearer result on Retina displays.

Xcode project signing is disabled and the completed bundle is ad-hoc signed
after assets are copied. No Apple developer team is required for TrollStore.

The build intentionally uses conservative floating-point flags
(`-fno-fast-math -ffp-contract=off`) and signed `char` to reduce replay drift
relative to the original x86 game. Exact stock replay parity on arm64 still
requires device testing because ARM and x87 floating-point execution differ.

Bluetooth nearby uses Apple's MultipeerConnectivity framework. One device
chooses `Create nearby room`, the other chooses `Join nearby room`; both must
allow Bluetooth/Nearby Devices when iOS asks. The transport uses low-latency
15-frame input windows with periodic reliable checkpoints, while recovery and
state sidebands remain reliable. This mode is iOS-only and still requires two
real iOS devices for final pairing and radio-range testing.

Version 1.2.5 retains AppleClang 14-compatible nearby-transport state
initialization, implements the required browser failure callback, and uses the correct
`foundPeer:withDiscoveryInfo:` MultipeerConnectivity selector. This prevents
iOS 15 from terminating the app when the guest discovers the host.

Version 1.2.5 also retains the validated v17 stability fixes that were absent
from the first netplay source line: 32-bit MSG offsets on arm64, fixed-width
score.dat serialization and migration, bounded Replay parsing, Result cleanup,
Boss effect/BGM guards, lazy iOS sound effects, and scene-transition probes.

For device diagnostics, open macOS Console, select the connected iPad, choose
the `All Messages` level (not only `Errors`), search for `th06`, and launch the
app. Useful lines start with `[IOS-BOOT]`, `[AssetProbe]`, or `[RendererGLES]`.
