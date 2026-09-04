"""Identify an Xbox 360 STFS package (CON / LIVE / PIRS).

DLC arrives as opaque 40-hex-character filenames, so the only way to know what
a package is - which expansion, which title, whether it is licence-bound - is
to read its header.

    python tools/stfs_info.py <file-or-directory> [...]

Prints title id, content type, display name, size, and the licence table. A
LIVE package's licence entries are what tie it to a console or profile; an
all-zero table means it is unlocked, and anything else needs the runtime to
fake entitlement.
"""

import argparse
import os
import struct
import sys

MAGICS = {b"CON ": "CON (console-signed)",
          b"LIVE": "LIVE (Xbox Live signed)",
          b"PIRS": "PIRS (Microsoft signed, non-Live)"}

# Offsets into the STFS header, after the 0x228-byte signature block.
OFF_LICENSES = 0x22C
OFF_HEADER_SHA1 = 0x32C
OFF_HEADER_SIZE = 0x340
OFF_CONTENT_TYPE = 0x344
OFF_METADATA_VER = 0x348
OFF_CONTENT_SIZE = 0x34C
OFF_MEDIA_ID = 0x354
OFF_VERSION = 0x358
OFF_BASE_VERSION = 0x35C
OFF_TITLE_ID = 0x360
OFF_PLATFORM = 0x364
OFF_DISC_NUMBER = 0x366
OFF_DISPLAY_NAME = 0x411
OFF_TITLE_NAME = 0x1691

# Values taken from the SDK's own XContentType enum (rex/system/xcontent.h),
# not from memory - an earlier hand-typed table had several of these wrong.
CONTENT_TYPES = {
    0x00000001: "SavedGame",
    0x00000002: "MarketplaceContent (DLC)",
    0x00000003: "Publisher",
    0x00001000: "Xbox360Title",
    0x00002000: "IptvPauseBuffer",
    0x00003000: "XNACommunity",
    0x00004000: "InstalledGame",
    0x00005000: "XboxTitle",
    0x00006000: "SocialTitle",
    0x00007000: "GamesOnDemand",
    0x00008000: "SUStoragePack",
    0x00009000: "AvatarItem",
    0x00010000: "Profile",
    0x00020000: "GamerPicture",
    0x00030000: "Theme",
    0x00040000: "CacheFile",
    0x00050000: "StorageDownload",
    0x00060000: "XboxSavedGame",
    0x00070000: "XboxDownload",
    0x00080000: "GameDemo",
    0x00090000: "Video",
    0x000A0000: "GameTitle",
    0x000B0000: "Installer",
    0x000C0000: "GameTrailer",
    0x000D0000: "ArcadeTitle",
    0x000E0000: "XNA",
    0x000F0000: "LicenseStore",
    0x00100000: "Movie",
    0x00200000: "TV",
    0x00300000: "MusicVideo",
    0x00400000: "GameVideo",
    0x00500000: "PodcastVideo",
    0x00600000: "ViralVideo",
    0x02000000: "CommunityGame",
}


def utf16be(data):
    text = data.decode("utf-16-be", errors="replace")
    return text.split("\0", 1)[0].strip()


def describe(path):
    with open(path, "rb") as f:
        head = f.read(0x1800)
    if len(head) < 0x1800:
        return f"{os.path.basename(path)}: too small to be an STFS package"

    magic = head[:4]
    if magic not in MAGICS:
        return f"{os.path.basename(path)}: not STFS (magic {magic!r})"

    content_type = struct.unpack_from(">I", head, OFF_CONTENT_TYPE)[0]
    title_id = struct.unpack_from(">I", head, OFF_TITLE_ID)[0]
    media_id = struct.unpack_from(">I", head, OFF_MEDIA_ID)[0]
    version = struct.unpack_from(">I", head, OFF_VERSION)[0]
    base_version = struct.unpack_from(">I", head, OFF_BASE_VERSION)[0]
    content_size = struct.unpack_from(">Q", head, OFF_CONTENT_SIZE)[0]
    display = utf16be(head[OFF_DISPLAY_NAME:OFF_DISPLAY_NAME + 0x100])
    title = utf16be(head[OFF_TITLE_NAME:OFF_TITLE_NAME + 0x100])

    lines = [f"{os.path.basename(path)}",
             f"   package       {MAGICS[magic]}",
             f"   title         {title!r}  (id {title_id:08X}, media {media_id:08X})",
             f"   display name  {display!r}",
             f"   content type  {content_type:08X}  "
             f"{CONTENT_TYPES.get(content_type, 'unknown')}",
             f"   version       {version} (base {base_version})",
             f"   content size  {content_size:,} bytes  "
             f"(file is {os.path.getsize(path):,})"]

    # Licence table: 16 entries of {licensee id (8), bits (4), flags (4)}.
    licences = []
    for i in range(16):
        o = OFF_LICENSES + i * 0x10
        licensee, bits, flags = struct.unpack_from(">QII", head, o)
        if licensee or bits or flags:
            licences.append((i, licensee, bits, flags))
    if not licences:
        lines.append("   licences      none set - not bound to a console or profile")
    else:
        lines.append("   licences      " + str(len(licences)) + " entry/entries:")
        for i, licensee, bits, flags in licences:
            kind = ("console-bound" if licensee >> 48 == 0x0009 else
                    "profile-bound" if licensee >> 48 == 0x0002 else
                    "unrestricted (not console- or profile-bound)"
                    if licensee == 0xFFFFFFFFFFFFFFFF else
                    "other")
            lines.append(f"       [{i}] licensee {licensee:016X} bits {bits:08X} "
                         f"flags {flags:08X}  ({kind})")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args()

    files = []
    for p in args.paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files.extend(os.path.join(root, n) for n in sorted(names))
        else:
            files.append(p)

    for path in files:
        print(describe(path))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
