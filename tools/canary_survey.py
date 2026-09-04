"""Census the divergence between the ReXGlue GPU code and Xenia Canary.

    python tools/canary_survey.py                 # every mapped file, ranked
    python tools/canary_survey.py --file draw     # detail for one pair
    python tools/canary_survey.py --missing       # only functions Canary has and we do not

ReXGlue's GPU code is a REFACTORED fork of Canary: different namespaces, file
layout and logging. `git diff` is useless across the two, so this enumerates
FUNCTIONS on both sides and reports what is present on one and not the other.

It is a census, not a sample: the file map below is checked, and any file in
either tree that the map does not mention is reported as UNMAPPED rather than
quietly ignored. A gap nobody has looked at must be visible, not averaged away.

Line counts are a weak signal (formatting and logging differ), so the ranking
is by the number of functions only Canary has.
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OURS_ROOT = os.path.join(ROOT, "..", "rexglue-src", "src", "graphics")
CANARY_ROOT = os.path.join(ROOT, "..", "..", "Ninja Gaiden 2 Xbox360",
                           "xenia-canary-src", "src", "xenia", "gpu")

# ours (under src/graphics) -> canary (under src/xenia/gpu)
FILE_MAP = {
    "pipeline/render_target/cache.cpp": "render_target_cache.cc",
    "d3d12/render_target_cache.cpp": "d3d12/d3d12_render_target_cache.cc",
    "vulkan/render_target_cache.cpp": "vulkan/vulkan_render_target_cache.cc",
    "d3d12/command_processor.cpp": "d3d12/d3d12_command_processor.cc",
    "vulkan/command_processor.cpp": "vulkan/vulkan_command_processor.cc",
    "command_processor.cpp": "command_processor.cc",
    "primitive_processor.cpp": "primitive_processor.cc",
    "util/draw.cpp": "draw_util.cc",
    "pipeline/texture/cache.cpp": "texture_cache.cc",
    "d3d12/texture_cache.cpp": "d3d12/d3d12_texture_cache.cc",
    "vulkan/texture_cache.cpp": "vulkan/vulkan_texture_cache.cc",
    "d3d12/pipeline_cache.cpp": "d3d12/pipeline_cache.cc",
    "vulkan/pipeline_cache.cpp": "vulkan/vulkan_pipeline_cache.cc",
    "shared_memory.cpp": "shared_memory.cc",
    "d3d12/shared_memory.cpp": "d3d12/d3d12_shared_memory.cc",
    "vulkan/shared_memory.cpp": "vulkan/vulkan_shared_memory.cc",
    "pipeline/shader/translator.cpp": "shader_translator.cc",
    "pipeline/shader/dxbc_translator.cpp": "dxbc_shader_translator.cc",
    "pipeline/shader/dxbc_translator_om.cpp": "dxbc_shader_translator_om.cc",
    "pipeline/shader/dxbc_translator_alu.cpp": "dxbc_shader_translator_alu.cc",
    "pipeline/shader/dxbc_translator_fetch.cpp": "dxbc_shader_translator_fetch.cc",
    "pipeline/shader/spirv_translator.cpp": "spirv_shader_translator.cc",
    "pipeline/shader/spirv_translator_rb.cpp": "spirv_shader_translator_rb.cc",
    "pipeline/shader/spirv_translator_alu.cpp": "spirv_shader_translator_alu.cc",
    "pipeline/shader/spirv_translator_fetch.cpp": "spirv_shader_translator_fetch.cc",
    "pipeline/shader/spirv_translator_memexport.cpp":
        "spirv_shader_translator_memexport.cc",
    "pipeline/shader/interpreter.cpp": "shader_interpreter.cc",
    "register_file.cpp": "register_file.cc",
    "pipeline/shader/shader.cpp": "shader.cc",
    "util/draw_extent_estimator.cpp": "draw_extent_estimator.cc",
    "d3d12/deferred_command_list.cpp": "d3d12/deferred_command_list.cc",
    "d3d12/primitive_processor.cpp": "d3d12/d3d12_primitive_processor.cc",
    "d3d12/shader.cpp": "d3d12/d3d12_shader.cc",
    "d3d12/graphics_system.cpp": "d3d12/d3d12_graphics_system.cc",
    "vulkan/deferred_command_buffer.cpp": "vulkan/deferred_command_buffer.cc",
    "vulkan/primitive_processor.cpp": "vulkan/vulkan_primitive_processor.cc",
    "vulkan/shader.cpp": "vulkan/vulkan_shader.cc",
    "vulkan/graphics_system.cpp": "vulkan/vulkan_graphics_system.cc",
    "pipeline/shader/dxbc.cpp": "dxbc_shader.cc",
    "pipeline/shader/dxbc_translator_memexport.cpp":
        "dxbc_shader_translator_memexport.cc",
    "pipeline/shader/spirv.cpp": "spirv_shader.cc",
    "pipeline/shader/spirv_builder.cpp": "spirv_builder.cc",
    "pipeline/shader/translator_disasm.cpp": "shader_translator_disasm.cc",
    "pipeline/texture/extent.cpp": "texture_extent.cc",
    "pipeline/texture/info.cpp": "texture_info.cc",
    "pipeline/texture/info_formats.cpp": "texture_info_formats.cc",
    "pipeline/texture/util.cpp": "texture_util.cc",
    "pipeline/texture/conversion.cpp": "texture_conversion.cc",
    "registers.cpp": "registers.cc",
    "sampler_info.cpp": "sampler_info.cc",
    "format/ucode.cpp": "ucode.cc",
    "flags.cpp": "gpu_flags.cc",
    "graphics_system.cpp": "graphics_system.cc",
    "packet_disassembler.cpp": "packet_disassembler.cc",
}

# Canary files with no counterpart on our side, and why that is fine.
CANARY_IGNORED = {
    "trace_viewer.cc": "tooling - trace viewer app",
    "trace_dump.cc": "tooling - trace dump app",
    "trace_writer.cc": "tooling - trace capture",
    "trace_reader.cc": "tooling - trace capture",
    "texture_dump.cc": "tooling - texture dump",
    "packet_disassembler.cc": "tooling - PM4 disassembly",
    "shader_compiler_main.cc": "tooling - standalone shader compiler",
    "xenos.cc": "constants; SDK splits these differently",
    "trace_player.cc": "tooling - trace playback",
    "d3d12/d3d12_trace_dump_main.cc": "tooling - trace dump entry point",
    "d3d12/d3d12_trace_viewer_main.cc": "tooling - trace viewer entry point",
    "vulkan/vulkan_trace_dump_main.cc": "tooling - trace dump entry point",
    "vulkan/vulkan_trace_viewer_main.cc": "tooling - trace viewer entry point",
    "null/null_command_processor.cc": "null backend; the SDK ships no null GPU",
    "null/null_graphics_system.cc": "null backend; the SDK ships no null GPU",
    "d3d12/d3d12_zpd_query_pool.cc": "ZPD occlusion queries - a Canary feature "
                                     "we do not have at all (see notes)",
    "vulkan/vulkan_zpd_query_pool.cc": "ZPD occlusion queries - ditto",
}

FUNC = re.compile(r"^[A-Za-z_][\w:<>,\s\*&~]*?\b(\w+)::(~?\w+)\s*\(", re.M)

# draw_util and the texture helpers are namespaces of FREE functions, so a
# Class::method matcher sees almost nothing in them and would report two very
# different files as identical. Catch a top-level definition too: a line that
# starts in column 0 with a return type and an identifier, and is not a call,
# a control keyword or a declaration ending in ';'.
FREE = re.compile(
    r"^(?!(?:if|for|while|switch|return|else|case|template|namespace|using|"
    r"struct|class|enum|typedef|extern|static_assert)\b)"
    r"[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)\s*\([^;]*?\)\s*(?:const\s*)?\{", re.M)


def functions(path):
    """function name -> body line count, for one file."""
    if not os.path.exists(path):
        return None
    text = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for m in FREE.finditer(text):
        name = m.group(1)
        i = text.rfind("{", m.start(), m.end())
        depth, j = 0, i
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out.setdefault(name, text.count("\n", i, j))
    for m in FUNC.finditer(text):
        cls, name = m.group(1), m.group(2)
        out.pop(name, None)  # prefer the qualified form
        i = text.find("{", m.end() - 1)
        if i < 0:
            continue
        depth, j = 0, i
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out["%s::%s" % (cls, name)] = text.count("\n", i, j)
    return out


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", help="substring of a mapped path, for detail")
    ap.add_argument("--missing", action="store_true",
                    help="list every function only Canary has")
    args = ap.parse_args()

    if not os.path.isdir(CANARY_ROOT):
        sys.exit("Canary tree not found: %s" % CANARY_ROOT)

    rows = []
    for ours_rel, can_rel in sorted(FILE_MAP.items()):
        o = functions(os.path.join(OURS_ROOT, ours_rel))
        c = functions(os.path.join(CANARY_ROOT, can_rel))
        rows.append((ours_rel, can_rel, o, c))

    if args.file:
        rows = [r for r in rows if args.file in r[0] or args.file in r[1]]
        if not rows:
            sys.exit("no mapped file matches %r" % args.file)

    total_missing = 0
    print("%-46s %6s %6s %5s %5s" % ("file", "ours", "canary", "miss", "extra"))
    print("-" * 74)
    ranked = []
    for ours_rel, can_rel, o, c in rows:
        if o is None or c is None:
            print("%-46s  %s" % (ours_rel,
                                 "OURS MISSING" if o is None else "CANARY MISSING"))
            continue
        miss = sorted(set(c) - set(o))
        extra = sorted(set(o) - set(c))
        total_missing += len(miss)
        ranked.append((len(miss), ours_rel, can_rel, o, c, miss, extra))

    for n, ours_rel, can_rel, o, c, miss, extra in sorted(ranked, reverse=True):
        print("%-46s %6d %6d %5d %5d"
              % (ours_rel, len(o), len(c), len(miss), len(extra)))
        if args.file or args.missing:
            for m in miss:
                print("      only canary: %-52s %4d lines" % (m, c[m]))
            if args.file:
                for e in extra:
                    print("      only ours:   %s" % e)

    # --- the census half: anything the map does not cover ---------------------
    mapped_canary = set(FILE_MAP.values()) | set(CANARY_IGNORED)
    unmapped = []
    for base, dirs, files in os.walk(CANARY_ROOT):
        dirs[:] = [d for d in dirs if d not in (".git", "test", "testing")]
        for f in files:
            if not f.endswith(".cc") or f.endswith("_test.cc"):
                continue
            rel = os.path.relpath(os.path.join(base, f), CANARY_ROOT).replace("\\", "/")
            if rel not in mapped_canary:
                unmapped.append(rel)

    print("\ntotal functions only Canary has: %d" % total_missing)
    if unmapped:
        print("\nUNMAPPED Canary files (%d) - not looked at by this survey:"
              % len(unmapped))
        for u in sorted(unmapped):
            print("   %s" % u)
    else:
        print("every Canary GPU source file is either mapped or explicitly ignored")
    return 0


if __name__ == "__main__":
    sys.exit(main())
