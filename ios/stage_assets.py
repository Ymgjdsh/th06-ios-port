#!/usr/bin/env python3
"""Prepare the read-only game assets copied into th06.app."""

import argparse
import pathlib
import shutil
import sys
import zipfile


class Pbg3TableReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 4
        self.mask = 0x80
        self.current = 0

    def read_bit(self) -> int:
        if self.mask == 0x80:
            if self.offset >= len(self.data):
                raise ValueError("unexpected end of PBG3 archive")
            self.current = self.data[self.offset]
            self.offset += 1
        value = 1 if self.current & self.mask else 0
        self.mask >>= 1
        if self.mask == 0:
            self.mask = 0x80
        return value

    def read_int(self, bits: int) -> int:
        value = 0
        for bit in range(bits - 1, -1, -1):
            value |= self.read_bit() << bit
        return value

    def read_varint(self) -> int:
        header = (self.read_bit() << 1) | self.read_bit()
        return self.read_int((8, 16, 24, 32)[header])

    def seek(self, offset: int) -> None:
        if offset < 0 or offset >= len(self.data):
            raise ValueError(f"invalid PBG3 table offset: {offset}")
        self.offset = offset
        self.mask = 0x80

    def read_string(self) -> str:
        output = bytearray()
        for _ in range(256):
            value = self.read_int(8)
            if value == 0:
                return output.decode("latin-1")
            output.append(value)
        raise ValueError("unterminated PBG3 entry name")


def read_pbg3_entry_names(path: pathlib.Path) -> set[str]:
    data = path.read_bytes()
    if data[:4] != b"PBG3":
        raise ValueError(f"invalid PBG3 magic: {path}")
    reader = Pbg3TableReader(data)
    entry_count = reader.read_varint()
    table_offset = reader.read_varint()
    reader.seek(table_offset)
    names = set()
    for _ in range(entry_count):
        for _ in range(5):
            reader.read_varint()
        names.add(reader.read_string())
    return names


def copy_tree_contents(source: pathlib.Path, output: pathlib.Path) -> None:
    for child in source.iterdir():
        target = output / child.name
        if child.is_dir():
            shutil.copytree(child, target, dirs_exist_ok=True)
        else:
            shutil.copy2(child, target)


def extract_apk(apk: pathlib.Path, output: pathlib.Path) -> None:
    with zipfile.ZipFile(apk) as archive:
        for info in archive.infolist():
            if info.is_dir() or not info.filename.startswith("assets/"):
                continue
            relative = pathlib.PurePosixPath(info.filename).relative_to("assets")
            if ".." in relative.parts:
                raise RuntimeError(f"unsafe APK member: {info.filename}")
            destination = output.joinpath(*relative.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(info) as source, destination.open("wb") as target:
                shutil.copyfileobj(source, target)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--apk", type=pathlib.Path)
    parser.add_argument("--assets", type=pathlib.Path)
    parser.add_argument("--bgm", required=True, type=pathlib.Path)
    args = parser.parse_args()

    if (args.apk is None) == (args.assets is None):
        parser.error("provide exactly one of --apk or --assets")
    if not args.bgm.is_dir():
        parser.error(f"BGM directory does not exist: {args.bgm}")

    args.output.mkdir(parents=True, exist_ok=True)
    if args.apk is not None:
        if not args.apk.is_file():
            parser.error(f"APK does not exist: {args.apk}")
        extract_apk(args.apk, args.output)
    else:
        if not args.assets.is_dir():
            parser.error(f"asset directory does not exist: {args.assets}")
        copy_tree_contents(args.assets, args.output)

    bgm_output = args.output / "bgm"
    # Reused build directories may still contain WAV tracks staged by an older
    # build. Replace the directory atomically at the content level so stale
    # music cannot silently inflate the next IPA.
    if bgm_output.exists():
        shutil.rmtree(bgm_output)
    bgm_output.mkdir(parents=True, exist_ok=True)
    copy_tree_contents(args.bgm, bgm_output)

    tracks = [bgm_output / f"th06_{index:02d}.ogg" for index in range(1, 18)]
    missing_tracks = [track.name for track in tracks
                      if not track.is_file() or track.stat().st_size == 0]
    if missing_tracks:
        print("error: missing OGG BGM: " + ", ".join(missing_tracks),
              file=sys.stderr)
        return 2

    dat_files = [path for path in args.output.iterdir()
                 if path.is_file() and path.suffix.lower() == ".dat"]
    required_suffixes = ("_CM.DAT", "_ED.DAT", "_IN.DAT", "_MD.DAT",
                         "_ST.DAT", "_TL.DAT")
    missing = [suffix for suffix in required_suffixes
               if not any(path.name.lower().endswith(suffix.lower()) for path in dat_files)]
    if missing:
        print("error: missing required game DAT archives: " + ", ".join(missing),
              file=sys.stderr)
        return 2

    canonical_archives = {}
    for suffix in required_suffixes:
        matches = [path for path in dat_files
                   if path.name.lower().endswith(suffix.lower())]
        matches.sort(key=lambda path: (
            path.name.lower().startswith(("tolol", "totol")),
            not path.name.lower().startswith("th06c"),
            path.name.lower()))
        source = matches[0]
        canonical_name = "KOUMAKYO" + suffix[:-4] + ".dat"
        canonical = args.output / canonical_name
        if source.resolve() != canonical.resolve():
            shutil.copy2(source, canonical)
        canonical_archives[suffix] = canonical

    try:
        for archive in canonical_archives.values():
            names = read_pbg3_entry_names(archive)
            if not names:
                raise ValueError(f"PBG3 archive has no entries: {archive}")
        in_names = {name.lower() for name in
                    read_pbg3_entry_names(canonical_archives["_IN.DAT"])}
        required_entries = {"th06logo.jpg", "text.anm", "plst00.wav"}
        missing_entries = sorted(required_entries - in_names)
        if missing_entries:
            raise ValueError("IN archive is missing startup entries: " +
                             ", ".join(missing_entries))
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print(f"staged {len(dat_files)} source DAT archives, 6 canonical aliases, "
          f"and BGM into {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
