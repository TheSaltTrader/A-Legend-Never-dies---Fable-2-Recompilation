"""Decode dumped guest textures to PNG, upscale them, and write a texture pack.

The plugin dumps the RAW GUEST bytes plus a TextureKey, not finished pixels -
texture conversion in the plugin happens in a GPU compute shader, so host-format
pixels never exist on the CPU and reading them back would cost a fence and a
stall per texture. Untiling is a pure function of (x, y, pitch, bpp), so it is
reproduced here exactly instead.

    python tools/upscale_textures.py --dir C:/ng2tex            # decode only
    python tools/upscale_textures.py --dir C:/ng2tex --upscale  # decode + upscale

Progress is printed as machine-readable lines so the in-game options screen can
show a percentage and a time estimate:

    PROGRESS <done> <total> <name>

Xenos tiling is taken from the SDK's GetTiledOffset2D, and block-compressed
formats (DXT1/3/5) are decoded here rather than pulled from a library so the
tool has no dependencies beyond Pillow, which the upscaler needs anyway.
"""

import argparse
import os
import struct
import sys
import time

# TextureFormat values that matter for this title. Anything else is reported
# and skipped rather than guessed at - a wrong guess produces a plausible
# looking texture that is subtly wrong, which is worse than a gap.
FMT_8 = 2
FMT_1_5_5_5 = 3
FMT_5_6_5 = 4
FMT_8_8_8_8 = 6
FMT_8_8 = 10
FMT_8_8_8_8_A = 14
FMT_4_4_4_4 = 15
FMT_DXT1 = 18
FMT_DXT2_3 = 19
FMT_DXT4_5 = 20

try:
    from PIL import Image
    Image_LANCZOS = Image.Resampling.LANCZOS
except ImportError:      # reported properly in main()
    Image = None
    Image_LANCZOS = None

FMT_NAMES = {
    FMT_8: "k_8", FMT_1_5_5_5: "k_1_5_5_5", FMT_5_6_5: "k_5_6_5",
    FMT_8_8_8_8: "k_8_8_8_8", FMT_8_8: "k_8_8", FMT_8_8_8_8_A: "k_8_8_8_8_A",
    FMT_4_4_4_4: "k_4_4_4_4", FMT_DXT1: "k_DXT1", FMT_DXT2_3: "k_DXT2_3",
    FMT_DXT4_5: "k_DXT4_5",
}

# bytes per block, and block size in pixels
FMT_INFO = {
    FMT_8: (1, 1), FMT_1_5_5_5: (2, 1), FMT_5_6_5: (2, 1), FMT_4_4_4_4: (2, 1),
    FMT_8_8: (2, 1), FMT_8_8_8_8: (4, 1), FMT_8_8_8_8_A: (4, 1),
    FMT_DXT1: (8, 4), FMT_DXT2_3: (16, 4), FMT_DXT4_5: (16, 4),
}


def align(v, a):
    return (v + a - 1) // a * a


def tiled_offset_2d(x, y, pitch, bpb_log2):
    """Xenos 2D tiling, ported verbatim from the SDK's GetTiledOffset2D."""
    pitch = align(pitch, 32)
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bpb_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bpb_log2
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F))


# Xenos endianness (XE_GPU_ENDIAN). The dump records it per texture and it was
# being PARSED AND THEN IGNORED, which is why Fable II's DXT1 art decoded as
# psychedelic noise: a DXT1 block opens with two 16-bit colour endpoints, so
# without the 8in16 swap every colour is byte-reversed. NG2's textures are
# k_8 / k_8_8_8_8 with no swap needed, so the gap never showed there.
ENDIAN_NONE, ENDIAN_8IN16, ENDIAN_8IN32, ENDIAN_16IN32 = 0, 1, 2, 3


def swap_endian(data, endian):
    """Undo the guest's byte order. Returns data unchanged for kNone."""
    if endian == ENDIAN_NONE or not data:
        return data
    b = bytearray(data)
    if endian == ENDIAN_8IN16:
        b[0:len(b) - len(b) % 2:2], b[1:len(b) - len(b) % 2:2] = (
            bytes(b[1:len(b) - len(b) % 2:2]), bytes(b[0:len(b) - len(b) % 2:2]))
    elif endian == ENDIAN_8IN32:
        n = len(b) - len(b) % 4
        for i in range(0, n, 4):
            b[i:i + 4] = b[i:i + 4][::-1]
    elif endian == ENDIAN_16IN32:
        n = len(b) - len(b) % 4
        for i in range(0, n, 4):
            b[i:i + 4] = b[i + 2:i + 4] + b[i:i + 2]
    return bytes(b)


def untile(data, w, h, bpb, block, pitch_blocks):
    """Return linear block data for a tiled texture."""
    bw, bh = (w + block - 1) // block, (h + block - 1) // block
    bpb_log2 = bpb.bit_length() - 1
    out = bytearray(bw * bh * bpb)
    for by in range(bh):
        for bx in range(bw):
            off = tiled_offset_2d(bx, by, pitch_blocks, bpb_log2)
            if off + bpb <= len(data):
                dst = (by * bw + bx) * bpb
                out[dst:dst + bpb] = data[off:off + bpb]
    return bytes(out)


def _c565(v):
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def decode_dxt(data, w, h, fmt):
    """DXT1/2_3/4_5 to RGBA. Written out rather than imported: the tool would
    otherwise need a compression library purely for three well-documented
    block formats."""
    bw, bh = (w + 3) // 4, (h + 3) // 4
    px = bytearray(w * h * 4)
    stride = 8 if fmt == FMT_DXT1 else 16
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * stride
            if o + stride > len(data):
                continue
            alpha = None
            co = o
            if fmt == FMT_DXT2_3:
                a = data[o:o + 8]
                alpha = [((a[i >> 1] >> ((i & 1) * 4)) & 0xF) * 17 for i in range(16)]
                co = o + 8
            elif fmt == FMT_DXT4_5:
                a0, a1 = data[o], data[o + 1]
                bits = int.from_bytes(data[o + 2:o + 8], "little")
                tbl = [a0, a1]
                if a0 > a1:
                    tbl += [((7 - i) * a0 + (1 + i) * a1) // 7 for i in range(6)]
                else:
                    tbl += [((5 - i) * a0 + (1 + i) * a1) // 5 for i in range(4)] + [0, 255]
                alpha = [tbl[(bits >> (3 * i)) & 7] for i in range(16)]
                co = o + 8
            c0, c1 = struct.unpack_from("<HH", data, co)
            bits = struct.unpack_from("<I", data, co + 4)[0]
            r0, g0, b0 = _c565(c0)
            r1, g1, b1 = _c565(c1)
            if c0 > c1 or fmt != FMT_DXT1:
                pal = [(r0, g0, b0, 255), (r1, g1, b1, 255),
                       ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
                       ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255)]
            else:
                pal = [(r0, g0, b0, 255), (r1, g1, b1, 255),
                       ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255), (0, 0, 0, 0)]
            for i in range(16):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x >= w or y >= h:
                    continue
                c = pal[(bits >> (2 * i)) & 3]
                d = (y * w + x) * 4
                px[d:d + 4] = bytes((c[0], c[1], c[2],
                                     alpha[i] if alpha is not None else c[3]))
    return bytes(px)


def decode_plain(data, w, h, fmt):
    """Non-block formats to RGBA. The 360 stores multi-byte texels big-endian."""
    px = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            d = i * 4
            if fmt == FMT_8:
                if i >= len(data):
                    break
                v = data[i]
                px[d:d + 4] = bytes((v, v, v, 255))
            elif fmt == FMT_8_8:
                o = i * 2
                if o + 2 > len(data):
                    break
                px[d:d + 4] = bytes((data[o], data[o + 1], 0, 255))
            elif fmt in (FMT_8_8_8_8, FMT_8_8_8_8_A):
                o = i * 4
                if o + 4 > len(data):
                    break
                a, r, g, b = data[o], data[o + 1], data[o + 2], data[o + 3]
                px[d:d + 4] = bytes((r, g, b, a))
            elif fmt == FMT_5_6_5:
                o = i * 2
                if o + 2 > len(data):
                    break
                v = struct.unpack_from(">H", data, o)[0]
                r, g, b = _c565(v)
                px[d:d + 4] = bytes((r, g, b, 255))
            elif fmt == FMT_1_5_5_5:
                o = i * 2
                if o + 2 > len(data):
                    break
                v = struct.unpack_from(">H", data, o)[0]
                px[d:d + 4] = bytes((((v >> 10) & 31) * 255 // 31,
                                     ((v >> 5) & 31) * 255 // 31,
                                     (v & 31) * 255 // 31,
                                     255 if v & 0x8000 else 0))
            elif fmt == FMT_4_4_4_4:
                o = i * 2
                if o + 2 > len(data):
                    break
                v = struct.unpack_from(">H", data, o)[0]
                px[d:d + 4] = bytes((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17,
                                     (v & 15) * 17, ((v >> 12) & 15) * 17))
    return bytes(px)


DXT_FORMATS = (FMT_DXT1, FMT_DXT2_3, FMT_DXT4_5)


def pack_reason(w, h, fmt):
    """Why a texture is NOT a pack candidate, or None if it is.

    Two things have to be kept out, and they fail for different reasons.

    RENDER TARGETS AND VIDEO PLANES. The resident-memory load path carries the
    scene resolve, the HDR buffer and the video decoder's luma/chroma planes as
    well as art. Replacing any of those from a pack would be ruinous, and they
    are the easiest thing in the world to mistake for a texture: the first dump
    taken from this game was 71 files of which not one was art. The tell is
    that shipped 360 art is either block-compressed or power-of-two, and a
    framebuffer is neither - 1280x720, 1120x584, 320x580 and 890x224 are all
    render targets or video planes, and none is a power of two.

    FONTS AND HUD. An AI model invents detail in glyph edges and shifts them,
    which is the usual reason a texture pack looks broken. Small, single-channel
    and long-thin textures are the reliable signature.
    """
    if fmt not in FMT_INFO:
        return "unsupported format"
    if fmt in (FMT_8, FMT_8_8):          # masks, ramps, font coverage, video
        return "single-channel"
    if w <= 64 or h <= 64:
        return "too small"
    if max(w, h) / float(min(w, h)) >= 8.0:
        return "thin strip"
    if fmt not in DXT_FORMATS and not (_pot(w) and _pot(h)):
        # Uncompressed and not power-of-two: a framebuffer or a video plane.
        return "not art (npot uncompressed)"
    return None


def _pot(v):
    return v > 0 and (v & (v - 1)) == 0


def make_upscaler(model_name, scale):
    """Return a function (PIL RGBA) -> upscaled PIL RGBA.

    Real-ESRGAN if it is installed, otherwise Lanczos. The fallback still
    enlarges: "--upscale did nothing and said so quietly" is a worse outcome
    than a plainly-labelled resample, and the pack is still usable.

    The model is NOT installed automatically. It needs a CUDA torch matched to
    the card, and picking that wrongly is an unpleasant thing to undo on
    someone else's machine.
    """
    try:
        import numpy as np
        from realesrgan import RealESRGANer
        from basicsr.archs.rrdbnet_arch import RRDBNet
    except ImportError:
        print("NOTE: Real-ESRGAN not installed - using Lanczos instead.")
        print("      For the AI model:  pip install realesrgan basicsr")
        print("      plus a CUDA torch matching your card.")

        def lanczos(img):
            return img.resize((img.width * scale, img.height * scale),
                              Image_LANCZOS)
        return lanczos

    net = RRDBNet(num_in_ch=3, num_out_ch=3, num_feat=64, num_block=23,
                  num_grow_ch=32, scale=4)
    model_path = os.path.join("models", model_name + ".pth")
    if not os.path.isfile(model_path):
        print("NOTE: %s not found - using Lanczos instead." % model_path)

        def lanczos2(img):
            return img.resize((img.width * scale, img.height * scale),
                              Image_LANCZOS)
        return lanczos2

    eng = RealESRGANer(scale=4, model_path=model_path, model=net,
                       tile=512, tile_pad=16, half=True)
    print("Real-ESRGAN ready: %s" % model_path)

    def esrgan(img):
        # The model is RGB; alpha is carried separately and resized with
        # Lanczos. Feeding a premultiplied or model-invented alpha back into a
        # game texture produces halos around cut-outs, which on a kunai or a
        # leaf is immediately visible.
        rgb = np.asarray(img.convert("RGB"))
        out, _ = eng.enhance(rgb, outscale=scale)
        res = Image.fromarray(out).convert("RGBA")
        alpha = img.getchannel("A").resize(res.size, Image_LANCZOS)
        res.putalpha(alpha)
        return res

    return esrgan


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True, help="folder holding dump/ and pack/")
    ap.add_argument("--upscale", action="store_true", help="run the AI upscaler")
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--model", default="RealESRGAN_x4plus")
    ap.add_argument("--include-ui", action="store_true",
                    help="upscale UI/font textures too (usually a bad idea)")
    args = ap.parse_args()

    dump = os.path.join(args.dir, "dump")
    pack = os.path.join(args.dir, "pack")
    index = os.path.join(dump, "index.txt")
    if not os.path.isfile(index):
        print("no index.txt in %s - run the game with texture dumping on first" % dump)
        return 1
    os.makedirs(pack, exist_ok=True)

    entries = []
    for line in open(index):
        p = line.split()
        if len(p) >= 9:
            entries.append((p[0], int(p[1]), int(p[2]), int(p[3]), int(p[4]),
                            int(p[5]), int(p[6]), int(p[7]), int(p[8])))
    # The game re-dumps a texture it has seen if the cache evicted it, so the
    # index can hold duplicates. Keep the first of each id.
    seen, uniq = set(), []
    for e in entries:
        if e[0] not in seen:
            seen.add(e[0])
            uniq.append(e)

    if Image is None:
        print("Pillow is required: pip install pillow")
        return 1

    up = None
    if args.upscale:
        up = make_upscaler(args.model, args.scale)

    total = len(uniq)
    done = skipped = failed = ui = 0
    skips = {}
    started = time.time()
    for tid, w, h, fmt, tiled, pitch, endian, dim, size in uniq:
        name = "%s_%dx%d_%s" % (tid, w, h, FMT_NAMES.get(fmt, "fmt%d" % fmt))
        done += 1
        print("PROGRESS %d %d %s" % (done, total, name), flush=True)

        if fmt not in FMT_INFO:
            skipped += 1
            continue
        raw = os.path.join(dump, "tex_%s.bin" % tid)
        if not os.path.isfile(raw):
            failed += 1
            continue
        data = open(raw, "rb").read()
        # Before anything else: the bytes are in the guest's order.
        data = swap_endian(data, endian)

        bpb, block = FMT_INFO[fmt]
        if tiled:
            # The key's pitch is in units of 32 TEXELS, and tiled_offset_2d
            # wants a pitch in BLOCKS. Dividing by bytes-per-block instead of
            # by the block width put every macro-tile row at the wrong stride,
            # which decoded as recognisable art sliced into horizontal bands.
            pitch_texels = max(pitch * 32, w)
            pitch_blocks = max(1, pitch_texels // block)
            data = untile(data, w, h, bpb, block, pitch_blocks)
        try:
            if fmt in (FMT_DXT1, FMT_DXT2_3, FMT_DXT4_5):
                px = decode_dxt(data, w, h, fmt)
            else:
                px = decode_plain(data, w, h, fmt)
            img = Image.frombytes("RGBA", (w, h), px)
        except Exception as exc:                                # noqa: BLE001
            print("  decode failed for %s: %s" % (name, exc))
            failed += 1
            continue

        img.save(os.path.join(dump, name + ".png"))

        reason = pack_reason(w, h, fmt)
        if reason and not args.include_ui:
            skips[reason] = skips.get(reason, 0) + 1
            ui += 1
            continue
        if up:
            img = up(img)
        img.save(os.path.join(pack, "%s.png" % tid))

    took = time.time() - started
    for why, n in sorted(skips.items(), key=lambda kv: -kv[1]):
        print("  not packed - %-28s %d" % (why, n))
    print("DONE decoded=%d skipped_format=%d skipped_ui=%d failed=%d in %.1fs"
          % (done - skipped - failed, skipped, ui, failed, took))
    return 0


if __name__ == "__main__":
    sys.exit(main())
