#!/usr/bin/env python3
"""Create and verify the complete standalone iOS 14 source archive."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import stat
import zipfile


ARCHIVE_ROOT = "th06-ios14-netplay-v1.2.5-source"
EXCLUDED_DIR_NAMES = {
    ".git", ".github", ".devcontainer", "__pycache__", ".idea", ".vs",
}
EXCLUDED_TOP_LEVEL = {
    "android", "build", "build_sdl2", "dist", "tests", "tools", "shaders_vk",
}
EXCLUDED_SUFFIXES = (".pyc", ".pyo", ".ipa", ".zip", ".7z", ".rar")


def should_include(root: pathlib.Path, path: pathlib.Path) -> bool:
    relative = path.relative_to(root)
    parts = relative.parts
    if not parts:
        return False
    if (parts[0] in EXCLUDED_TOP_LEVEL or
            parts[0].startswith("build-") or
            parts[0].startswith("build_") or
            parts[0].startswith("_baseline_")):
        return False
    if any(part in EXCLUDED_DIR_NAMES for part in parts[:-1]):
        return False
    return not path.name.lower().endswith(EXCLUDED_SUFFIXES)


def archive_mode(path: pathlib.Path) -> int:
    if path.suffix in {".sh", ".py"} or path.name in {"configure", "autogen.sh"}:
        return stat.S_IFREG | 0o755
    return stat.S_IFREG | 0o644


def required_members() -> set[str]:
    base = ARCHIVE_ROOT + "/"
    members = {
        base + "CMakeLists.txt",
        base + "ios/README.md",
        base + "ios/build_ios.sh",
        base + "ios/package_ipa.sh",
        base + "ios/stage_assets.py",
        base + "ios/check_ios_source.py",
        base + "ios/Info.plist.in",
        base + "ios/LocalNetworkPermission.mm",
        base + "ios/vendor/SDL2/CMakeLists.txt",
        base + "ios/vendor/SDL2_image/CMakeLists.txt",
        base + "ios/vendor/SDL2_mixer/CMakeLists.txt",
        base + "scripts/package_ios_source.py",
        base + "src/NetplaySession.cpp",
        base + "src/NetplaySession.hpp",
        base + "src/NetplayTransport.cpp",
        base + "src/NetplayShell.cpp",
        base + "src/NetplayInternal.hpp",
        base + "src/OnlineMenu.cpp",
        base + "src/LocalNetworkPermissionIOS.hpp",
    }
    members.update(base + f"ios/assets/th06c_{name}.DAT"
                   for name in ("CM", "ED", "IN", "MD", "ST", "TL"))
    members.update(base + f"ios/bgm/th06_{index:02d}.ogg"
                   for index in range(1, 18))
    return members


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    root = args.root.resolve()
    output = (args.output.resolve() if args.output else
              root / "dist" / f"{ARCHIVE_ROOT}.zip")
    output.parent.mkdir(parents=True, exist_ok=True)

    files = sorted(path for path in root.rglob("*")
                   if path.is_file() and should_include(root, path))
    if not files:
        raise SystemExit("error: no source files selected")

    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=6, allowZip64=True) as archive:
        for path in files:
            relative = path.relative_to(root).as_posix()
            info = zipfile.ZipInfo.from_file(path, ARCHIVE_ROOT + "/" + relative)
            info.create_system = 3
            info.external_attr = archive_mode(path) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with path.open("rb") as source, archive.open(info, "w") as target:
                while True:
                    block = source.read(1024 * 1024)
                    if not block:
                        break
                    target.write(block)

    with zipfile.ZipFile(output, "r") as archive:
        bad_member = archive.testzip()
        if bad_member is not None:
            raise SystemExit(f"error: ZIP CRC verification failed: {bad_member}")
        names = set(archive.namelist())
        missing = sorted(required_members() - names)
        if missing:
            raise SystemExit("error: archive is incomplete:\n  " + "\n  ".join(missing))
        dat_count = sum(name.startswith(ARCHIVE_ROOT + "/ios/assets/") and
                        name.upper().endswith(".DAT") for name in names)
        music_count = sum(name.startswith(ARCHIVE_ROOT + "/ios/bgm/") and
                          name.lower().endswith(".ogg") for name in names)
        wav_count = sum(name.startswith(ARCHIVE_ROOT + "/ios/bgm/") and
                        name.lower().endswith(".wav") for name in names)
        if dat_count < 7 or music_count != 17:
            raise SystemExit(
                f"error: resource count mismatch: DAT={dat_count}, OGG={music_count}")
        if wav_count:
            raise SystemExit(f"error: archive unexpectedly contains {wav_count} WAV tracks")
        for script in ("ios/build_ios.sh", "ios/package_ipa.sh",
                       "ios/check_ios_source.py", "scripts/package_ios_source.py"):
            info = archive.getinfo(ARCHIVE_ROOT + "/" + script)
            if ((info.external_attr >> 16) & 0o111) == 0:
                raise SystemExit(f"error: executable mode missing from {script}")

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    checksum = output.with_suffix(output.suffix + ".sha256.txt")
    checksum.write_text(f"{digest}  {output.name}\n", encoding="ascii")
    print(f"created {output} ({output.stat().st_size / 1024 / 1024:.1f} MiB, "
          f"{len(files)} files)")
    print(f"SHA-256 {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
