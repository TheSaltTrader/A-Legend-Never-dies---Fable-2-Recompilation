"""Lodestone census for the Fable II port.

The one rule: a census enumerates its own subjects FROM THE ARTEFACT, never from
a list somebody typed here. A checker naming the settings it checks silently
stops covering the next setting added; a checker that finds every setting by
parsing the header and requires each to be *declared* cannot stop covering
anything without failing.

Ported from the NG2 port's census (tools/lodestone_census.py there), where its
first run found three real defects. Six sweeps, each with its subjects taken
from a different artefact:

  ACCOUNTING   subjects = every field in fable2_settings.h.
               Each must be reachable in the settings UI, or declared in
               settings-ledger.json with a reason nobody should reach it.

  DOCUMENTED   subjects = every field that ACCOUNTING found in the UI.
               Each must appear in the README's settings reference, or be
               declared. A setting a player can change and cannot read about
               is a setting they will change by guessing.

  DEFAULTS     subjects = every cvar that mirrors a setting.
               The cvar's compiled-in default must agree with the setting's.

  ROUNDTRIP    subjects = every field in fable2_settings.h again, this time
               against the SAVE and PARSE blocks. A field that is parsed but
               never written forgets itself at every launch, silently. This
               port had exactly that: Save() once wrote 23 keys while Apply()
               read 30, and changing any setting wiped the community patches.

  DELIVERY     subjects = every cvar fable2_tuning.h emits, and the setting
               field that drives it. A conditional emission may only be
               guarded by a condition that mentions the field it is emitting.
               On NG2 a guard reading `if (s.anisotropic >= 0)` had swallowed
               three unrelated settings, so ticking them did nothing unless
               anisotropic filtering was also overridden.

  ASSEMBLY     subjects = the files a PACKAGED build must and must not
               contain. Every tool the app looks up by name must be in the
               package, and no game data may be.

Run:  python tools/lodestone_census.py [--package <folder-or-zip>]
Exit: 0 when every subject is covered or declared, 1 otherwise.
"""

import argparse
import json
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
LEDGER = os.path.join(ROOT, "tools", "settings-ledger.json")
README = os.path.join(ROOT, "README.md")

SETTINGS_H = "fable2_settings.h"
SETTINGS_STRUCT = "struct Fable2Settings"
MENU_CPP = "fable2_menu.cpp"
TUNING_H = "fable2_tuning.h"
# Cvars this port defines to mirror settings carry this prefix; the setting
# field may or may not (fable2_60fps mirrors patch_60fps).
CVAR_PREFIX = "fable2_"
FIELD_PREFIXES_TRIED = ("", "patch_")
TITLE_ID = "4d5307f1"
GAME_DATA_EXT = (".xex", ".bik", ".bnk", ".xma", ".tex", ".bin", ".iso")

FIELD_RE = re.compile(
    r"^\s{2}(?:int|bool|float|double|uint32_t|std::string)\s+([a-z_][a-z0-9_]*)\s*(?:=|;)",
    re.MULTILINE,
)


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def load_ledger():
    if not os.path.isfile(LEDGER):
        return {}
    with open(LEDGER, encoding="utf-8") as f:
        data = json.load(f)
    return {entry["setting"]: entry for entry in data.get("declared", [])}


def settings_body():
    text = read(os.path.join(SRC, SETTINGS_H))
    start = text.index(SETTINGS_STRUCT)
    end = text.index("\n};", start)
    return text[start:end]


def enumerate_settings():
    """Subjects for ACCOUNTING: every field the settings struct declares."""
    return FIELD_RE.findall(settings_body())


def settings_used_in_ui():
    """Which settings the menu actually binds to a control."""
    text = read(os.path.join(SRC, MENU_CPP))
    found = set()
    for pattern in (r"\bs\.([a-z_][a-z0-9_]*)", r"settings_->([a-z_][a-z0-9_]*)",
                    r"\bsettings\.([a-z_][a-z0-9_]*)"):
        found.update(re.findall(pattern, text))
    return found


def cvar_defaults():
    """Subjects for DEFAULTS: cvars whose name mirrors a setting."""
    out = {}
    for name in os.listdir(SRC):
        if not name.endswith((".cpp", ".h")):
            continue
        text = read(os.path.join(SRC, name))
        for kind, cname, default in re.findall(
                r"REXCVAR_DEFINE_(INT32|BOOL)\(\s*(\w+)\s*,\s*([^,]+?)\s*,", text):
            out[cname] = (kind, default.strip(), name)
    return out


def setting_defaults():
    out = {}
    for field, value in re.findall(
            r"^\s{2}(?:int|bool|float|double)\s+([a-z_][a-z0-9_]*)\s*=\s*([^;]+);",
            settings_body(), re.MULTILINE):
        out[field] = value.strip()
    return out


def saved_fields():
    """Field names the Save() block actually writes."""
    text = read(os.path.join(SRC, SETTINGS_H))
    return set(re.findall(r'<<\s*"([a-z_][a-z0-9_]*)="', text))


def parsed_fields():
    """Field names Apply()/Load() actually reads back."""
    text = read(os.path.join(SRC, SETTINGS_H))
    return set(re.findall(r'k\s*==\s*"([a-z_][a-z0-9_]*)"', text))


def package_entries(package):
    if package is None:
        return None
    if os.path.isdir(package):
        entries = []
        for base, _dirs, files in os.walk(package):
            for f in files:
                rel = os.path.relpath(os.path.join(base, f), package)
                entries.append(rel.replace("\\", "/"))
        return entries
    if zipfile.is_zipfile(package):
        with zipfile.ZipFile(package) as z:
            return [n for n in z.namelist() if not n.endswith("/")]
    return None


def tools_the_app_looks_up():
    """Subjects for ASSEMBLY: every script that must be in the package.

    Two sources, because there are two ways a script gets needed and only one of
    them is visible from the C++: the app asks for it by name, via
    FindToolScript("..."), or a script the app asks for IMPORTS it.
    ai_upscale.py is only ever reached the second way.
    """
    tools_dir = os.path.join(ROOT, "tools")
    names = set()
    for name in os.listdir(SRC):
        if not name.endswith((".cpp", ".h")):
            continue
        text = read(os.path.join(SRC, name))
        names.update(re.findall(r'FindToolScript\("([^"]+)"\)', text))

    pending = list(names)
    while pending:
        script = pending.pop()
        path = os.path.join(tools_dir, script)
        if not os.path.isfile(path):
            continue
        body = read(path)
        for module in re.findall(r"^\s*(?:import|from)\s+([a-zA-Z_][\w]*)", body, re.MULTILINE):
            candidate = module + ".py"
            if candidate in names:
                continue
            if os.path.isfile(os.path.join(tools_dir, candidate)):
                names.add(candidate)
                pending.append(candidate)
    return names


FIELD_REF_RE = re.compile(r"\bs\.([a-z_][a-z0-9_]*)")


def tuning_emissions():
    """Every cvar fable2_tuning.h pushes, with its driving fields and guards.

    Returns a list of (cvar, fields_used, [guard_conditions]) taken by walking
    the file - not from a list here, so a cvar added tomorrow is a subject
    tomorrow. Guards are tracked by brace depth, which is what makes a
    misplaced closing brace visible: the statement simply reports the guard it
    is actually inside, rather than the one its indentation suggests.
    """
    src = read(os.path.join(SRC, TUNING_H))
    emissions = []
    guards = []          # (depth_when_opened, condition_text)
    depth = 0
    pending = None       # accumulating a multi-line push_back
    pending_guards = None
    for raw in src.splitlines():
        line = raw.split("//", 1)[0]

        if pending is not None:
            pending += " " + line.strip()
            if pending.count("(") <= pending.count(")"):
                m = re.search(r'push_back\(\{\s*"([a-z_0-9]+)"', pending)
                if m:
                    emissions.append((m.group(1),
                                      set(FIELD_REF_RE.findall(pending)),
                                      list(pending_guards)))
                pending = None
        elif "out.push_back(" in line:
            pending = line[line.index("out.push_back("):].strip()
            pending_guards = [c for _, c in guards]
            if pending.count("(") <= pending.count(")"):
                m = re.search(r'push_back\(\{\s*"([a-z_0-9]+)"', pending)
                if m:
                    emissions.append((m.group(1),
                                      set(FIELD_REF_RE.findall(pending)),
                                      list(pending_guards)))
                pending = None

        m = re.match(r"\s*(?:\}\s*else\s+)?if\s*\((.*)\)\s*\{\s*$", line)
        opened = m.group(1) if m else None

        for ch in line:
            if ch == "{":
                depth += 1
                if opened is not None:
                    guards.append((depth, opened))
                    opened = None
            elif ch == "}":
                while guards and guards[-1][0] >= depth:
                    guards.pop()
                depth -= 1
    return emissions


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", help="release folder or zip for the ASSEMBLY sweep")
    args = ap.parse_args()

    ledger = load_ledger()
    failures = []
    lines = []

    def sweep(title):
        lines.append("")
        lines.append("=== %s ===" % title)

    # ---------------------------------------------------------------- ACCOUNTING
    sweep("ACCOUNTING - every setting is reachable or declared")
    subjects = enumerate_settings()
    in_ui = settings_used_in_ui()
    covered = declared = uncovered = 0
    for field in subjects:
        if field in in_ui:
            covered += 1
        elif field in ledger and ledger[field].get("reason"):
            declared += 1
            lines.append("  declared  %-24s %s" % (field, ledger[field]["reason"]))
        else:
            uncovered += 1
            lines.append("  NOT COVERED  %s" % field)
            failures.append("setting '%s' is in no UI and is not declared" % field)
    lines.append("  %d subjects: %d in the UI, %d declared, %d uncovered"
                 % (len(subjects), covered, declared, uncovered))

    # ---------------------------------------------------------------- DOCUMENTED
    sweep("DOCUMENTED - every reachable setting appears in the README")
    readme = read(README) if os.path.isfile(README) else ""
    if not readme:
        failures.append("README.md is missing, so nothing can be documented")
        lines.append("  README.md not found")
    else:
        documented = undocumented = 0
        for field in subjects:
            if field not in in_ui:
                continue
            entry = ledger.get(field)
            if entry and entry.get("undocumented_ok"):
                continue
            # The README is prose, so the test is the setting's own words, not
            # its identifier: "texture_ai_strength" -> "detail strength".
            words = (entry or {}).get("doc_phrase") or field.replace("_", " ")
            if words.lower() in readme.lower() or field in readme:
                documented += 1
            else:
                undocumented += 1
                lines.append("  NOT DOCUMENTED  %-24s (looked for %r)" % (field, words))
                failures.append("setting '%s' is reachable but not in the README" % field)
        lines.append("  %d documented, %d undocumented" % (documented, undocumented))

    # ------------------------------------------------------------------ DEFAULTS
    sweep("DEFAULTS - a cvar mirroring a setting agrees with it")
    cvars = cvar_defaults()
    sdefaults = setting_defaults()
    checked = mismatched = 0
    for cname, (kind, cdefault, where) in sorted(cvars.items()):
        stem = cname[len(CVAR_PREFIX):] if cname.startswith(CVAR_PREFIX) else cname
        field = next((p + stem for p in FIELD_PREFIXES_TRIED if (p + stem) in sdefaults), None)
        if field is None:
            continue
        checked += 1
        want = sdefaults[field].rstrip("f")
        got = cdefault.rstrip("f")
        if want != got:
            mismatched += 1
            lines.append("  MISMATCH  %s = %s  but  %s = %s  (%s)"
                         % (cname, got, field, want, where))
            failures.append("cvar '%s' defaults to %s while setting '%s' defaults to %s"
                            % (cname, got, field, want))
    lines.append("  %d mirrored cvars checked, %d disagree" % (checked, mismatched))

    # ----------------------------------------------------------------- ROUNDTRIP
    sweep("ROUNDTRIP - every setting is both written and read back")
    written = saved_fields()
    parsed = parsed_fields()
    ok = broken = 0
    for field in subjects:
        entry = ledger.get(field)
        if entry and entry.get("not_persisted_ok"):
            continue
        in_save = field in written
        in_load = field in parsed
        if in_save and in_load:
            ok += 1
        elif in_load and not in_save:
            broken += 1
            lines.append("  PARSED BUT NEVER SAVED  %s  (resets every launch)" % field)
            failures.append("setting '%s' is read back but never written, so it "
                            "forgets itself at every launch" % field)
        elif in_save and not in_load:
            broken += 1
            lines.append("  SAVED BUT NEVER READ    %s  (write-only)" % field)
            failures.append("setting '%s' is written but never read back" % field)
        else:
            broken += 1
            lines.append("  NEITHER SAVED NOR READ  %s" % field)
            failures.append("setting '%s' is neither saved nor read back" % field)
    lines.append("  %d round-trip cleanly, %d do not" % (ok, broken))

    # ------------------------------------------------------------------ DELIVERY
    sweep("DELIVERY - a guarded setting is guarded by its own condition")
    emissions = tuning_emissions()
    unconditional = self_guarded = stranded = 0
    for cvar, fields, conds in emissions:
        if not conds:
            unconditional += 1
            continue
        # A setting may DECLARE that another setting decides whether it is
        # sent, when that is the design and not an accident: the FSR sharpness
        # is only meaningful while FSR is the chosen filter, and the ledger
        # entry for fsr_sharpness says so ("guarded_by"). Anything not declared
        # is the hazard the sweep exists for.
        declared_guards = set()
        for f in fields:
            declared_guards.update(ledger.get(f, {}).get("guarded_by", []))

        def acceptable(c):
            guard_fields = set(FIELD_REF_RE.findall(c))
            if not guard_fields:
                return True
            if fields & guard_fields:
                return True
            if guard_fields <= declared_guards:
                return True
            return any(g == cvar or cvar.endswith("_" + g) or cvar.startswith(g)
                       for g in guard_fields)

        bad = [c for c in conds if not acceptable(c)]
        if bad:
            stranded += 1
            lines.append("  STRANDED  %-38s driven by %s" %
                         (cvar, ", ".join(sorted(fields)) or "(no field)"))
            for c in bad:
                lines.append("            but gated on: if (%s)" % c.strip())
            failures.append("cvar '%s' is only emitted under a guard that does "
                            "not mention the setting driving it" % cvar)
        else:
            self_guarded += 1
    lines.append("  %d cvars emitted: %d unconditional, %d guarded by their own "
                 "setting, %d stranded"
                 % (len(emissions), unconditional, self_guarded, stranded))

    # ------------------------------------------------------------------ ASSEMBLY
    sweep("ASSEMBLY - the packaged build carries its tools and no game data")
    entries = package_entries(args.package)
    if args.package and entries is None:
        lines.append("  UNREADABLE  %s" % args.package)
        failures.append("--package %r is neither a folder nor a zip, so the "
                        "ASSEMBLY sweep covered nothing" % args.package)
    elif entries is None:
        lines.append("  not run (no --package given)")
    else:
        wanted = tools_the_app_looks_up()
        present = {os.path.basename(e) for e in entries}
        for tool in sorted(wanted):
            if tool in present:
                lines.append("  tool present  %s" % tool)
            else:
                lines.append("  TOOL MISSING  %s" % tool)
                failures.append("the app looks up '%s' but the package does not ship it" % tool)

        offenders = []
        for e in entries:
            low = e.lower()
            # The AI upscaler's network weights are the one .bin a release
            # carries on purpose - the engine's, not the game's - and they
            # live under tools/upscaler only (make_release.py exempts the
            # same subtree).
            if low.replace(chr(92), '/').startswith('tools/upscaler/'):
                continue
            if low.endswith(GAME_DATA_EXT) or TITLE_ID in low:
                if os.path.basename(low) in ("put_files_here.txt",):
                    continue
                offenders.append(e)
        if offenders:
            for e in offenders[:20]:
                lines.append("  GAME DATA  %s" % e)
                failures.append("package contains what looks like game data: %s" % e)
        else:
            lines.append("  no game data found in %d entries" % len(entries))

    print("\n".join(lines))
    print("")
    if failures:
        print("LODESTONE: %d failure(s)" % len(failures))
        for f in failures:
            print("  - %s" % f)
        return 1
    print("LODESTONE: every subject covered or declared")
    return 0


if __name__ == "__main__":
    sys.exit(main())
