#!/usr/bin/env python3
"""SampleDrum - the Audio Unit factory presets, made from the VST3 ones.

    tools/make-presets.py            write / refresh installer/presets/*.aupreset
    tools/make-presets.py --check    change nothing; fail if any is missing or stale
                                     (installer/build-release.sh runs this)

THE KITS ARE AUTHORED IN A VST3 HOST - you load the samples, set the levels,
and save a .vstpreset. This makes the matching .aupreset beside each one, so
the Audio Unit ships the same factory set without a kit being built twice.

It can, because both formats carry the same thing: the bytes
SampleDrumProcessor::getState wrote. A .vstpreset holds them as its `Comp`
chunk; Steinberg's AU wrapper keeps them in the preset's plist under
"Processor State" and hands them straight back to setState
(auwrapper.mm, SaveState / restoreState). So the conversion copies one
into the other - nothing is re-derived, and the two files cannot disagree
about what a kit sounds like. (ForTran goes the other way, .aupreset ->
.vstpreset, because its patches are authored in Logic: its
tools/make-presets.cpp. Same idea, opposite source.)

THE AU WRAPPER IS PARTICULAR, and each of these is a preset that silently
does not load: "version" must be 0; "subtype" and "manufacturer" must be
the component's own (read from resource/au-info.plist, not retyped here);
"Processor State" must not be empty; and "Controller State" must be
PRESENT, even empty, or the wrapper never hands the state to the
controller and the panel shows the previous kit.

What it checks in each .vstpreset, because a factory kit that loads wrong
is worse than one that does not load:

  * the class ID is SampleDrum's processor UID (from SampleDrumIDs.h) - a
    host ignores a preset whose UID does not match, and a preset saved from
    another plug-in must not ship under this name;
  * the state parses all the way through - version, parameters, bypass,
    the 32 paths and the embedded files - by the layout in
    SampleDrumState.h;
  * EVERY LOADED CELL CARRIES ITS FILE, or is a factory file under
    kFactorySampleRoot. A path with nothing embedded is a sample that was
    on the author's Mac and will not be on anyone else's: that kit would
    ship with silent cells, so it is refused.

The parameter count in each stream is reported: a kit saved by an older
build is shorter, and the controls it predates load at their defaults. A
count of 0 - a kit written by a generator rather than saved from the
plug-in - is valid: every control at its default.

Python, not C++ against the SDK, so it runs on the Linux VM and inside
installer/test-build-release.sh's stubbed project, and needs no build.
"""

import argparse
import pathlib
import plistlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PRESETS = ROOT / "installer" / "presets"


# ---------------------------------------------------------------------------
# The identity, read from where it is defined rather than copied in here.
# ---------------------------------------------------------------------------
def processor_uid():
    text = (ROOT / "source" / "SampleDrumIDs.h").read_text()
    m = re.search(r"kSampleDrumProcessorUID\s*\(\s*0x([0-9A-Fa-f]{8})\s*,\s*0x([0-9A-Fa-f]{8})\s*,"
                  r"\s*0x([0-9A-Fa-f]{8})\s*,\s*0x([0-9A-Fa-f]{8})\s*\)", text)
    if not m:
        sys.exit("make-presets: cannot find kSampleDrumProcessorUID in source/SampleDrumIDs.h")
    # FUID::toString - four 32-bit words, upper-case hex, as a .vstpreset stores it.
    return "".join(g.upper() for g in m.groups())


def au_codes():
    with open(ROOT / "resource" / "au-info.plist", "rb") as f:
        info = plistlib.load(f)
    comp = info["AudioComponents"][0]

    def four_cc(s):
        b = s.encode("ascii")
        if len(b) != 4:
            sys.exit(f"make-presets: '{s}' in au-info.plist is not a four-character code")
        return struct.unpack(">I", b)[0]

    return four_cc(comp["type"]), four_cc(comp["subtype"]), four_cc(comp["manufacturer"])


def state_version():
    text = (ROOT / "source" / "SampleDrumState.h").read_text()
    m = re.search(r"kStateVersion\s*=\s*(\d+)", text)
    if not m:
        sys.exit("make-presets: cannot find kStateVersion in source/SampleDrumState.h")
    return int(m.group(1))


def factory_root():
    text = (ROOT / "source" / "SampleDrumSample.h").read_text()
    m = re.search(r'kFactorySampleRoot\s*=\s*"([^"]+)"', text)
    if not m:
        sys.exit("make-presets: cannot find kFactorySampleRoot in source/SampleDrumSample.h")
    return m.group(1).rstrip("/")


CELLS = 32   # kCellCount: 8 columns x 4 rows (SampleDrumKit.h)


# ---------------------------------------------------------------------------
# The .vstpreset: 'VST3', int32 version, 32-char class ID, int64 offset of
# the chunk list; the list is 'List', int32 count, then per chunk a 4-char
# id, int64 offset and int64 size. All little-endian (vstpresetfile.cpp).
# ---------------------------------------------------------------------------
class Bad(Exception):
    pass


def read_vstpreset(data):
    if len(data) < 48 or data[:4] != b"VST3":
        raise Bad("is not a .vstpreset")
    class_id = data[8:40].decode("ascii", "replace")
    (list_at,) = struct.unpack_from("<q", data, 40)
    if list_at < 48 or list_at + 8 > len(data) or data[list_at:list_at + 4] != b"List":
        raise Bad("has no chunk list")
    (count,) = struct.unpack_from("<i", data, list_at + 4)
    chunks = {}
    at = list_at + 8
    for _ in range(max(0, count)):
        if at + 20 > len(data):
            raise Bad("has a damaged chunk list")
        cid = data[at:at + 4].decode("ascii", "replace")
        offset, size = struct.unpack_from("<qq", data, at + 4)
        if offset < 0 or size < 0 or offset + size > len(data):
            raise Bad(f"has a '{cid}' chunk past the end of the file")
        chunks[cid] = data[offset:offset + size]
        at += 20
    if "Comp" not in chunks:
        raise Bad("has no processor state ('Comp' chunk)")
    return class_id, chunks["Comp"], chunks.get("Cont", b"")


# ---------------------------------------------------------------------------
# The state, by SampleDrumState.h's layout. Returns a short report.
# ---------------------------------------------------------------------------
def check_state(state, version_wanted, factory):
    at = 0

    def take(fmt):
        nonlocal at
        size = struct.calcsize(fmt)
        if at + size > len(state):
            raise Bad("state stops short")
        values = struct.unpack_from(fmt, state, at)
        at += size
        return values

    def take_bytes(n, what):
        nonlocal at
        if n < 0 or at + n > len(state):
            raise Bad(f"state has a bad length for {what}")
        out = state[at:at + n]
        at += n
        return out

    (version,) = take("<i")
    if version < 1 or version > version_wanted:
        raise Bad(f"state version {version}, this build reads up to {version_wanted}")
    (params,) = take("<i")
    # The plug-in's own reader takes 0..100000: 0 is a kit written with no
    # parameter values at all (a generated kit), every control at default.
    if params < 0 or params > 100000:
        raise Bad(f"state claims {params} parameters")
    values = take(f"<{params}d")
    if any(not (0.0 <= v <= 1.0) for v in values):
        raise Bad("a parameter is outside 0..1")
    take("<i")                                   # bypass
    (cells,) = take("<i")
    if cells != CELLS:
        raise Bad(f"state has {cells} cells, not {CELLS}")
    paths = [take_bytes(take("<i")[0], "a path").decode("utf-8", "replace") for _ in range(cells)]

    if at == len(state):
        embedded = [b""] * cells                 # saved before files were embedded
    else:
        (count,) = take("<i")
        if count != CELLS:
            raise Bad(f"embedded block has {count} cells, not {CELLS}")
        embedded = [take_bytes(take("<i")[0], "an embedded file") for _ in range(count)]

    loaded = 0
    for cell, (path, blob) in enumerate(zip(paths, embedded)):
        if not path:
            continue
        loaded += 1
        if path.startswith(factory + "/"):
            continue
        if not blob:
            raise Bad(f"cell {cell} ({pathlib.PurePosixPath(path).name}) has no embedded file "
                      f"and is not factory content - it would be silent on any other Mac")
        if blob[:4] != b"RIFF" or blob[8:12] not in (b"WAVE",):
            raise Bad(f"cell {cell}'s embedded file is not a WAV")

    return params, loaded


# ---------------------------------------------------------------------------
def aupreset_bytes(name, processor, controller, codes):
    au_type, au_subtype, au_manufacturer = codes
    plist = {
        "Controller State": controller,          # PRESENT, even empty - see the banner
        "Processor State": processor,
        "manufacturer": au_manufacturer,
        "name": name,
        "subtype": au_subtype,
        "type": au_type,
        "version": 0,
    }
    return plistlib.dumps(plist, fmt=plistlib.FMT_XML, sort_keys=True)


def aupreset_matches(path, name, processor, codes):
    try:
        with open(path, "rb") as f:
            p = plistlib.load(f)
    except Exception:
        return False
    au_type, au_subtype, au_manufacturer = codes
    return (p.get("Processor State") == processor and "Controller State" in p
            and p.get("type") == au_type and p.get("subtype") == au_subtype
            and p.get("manufacturer") == au_manufacturer and p.get("version") == 0
            and p.get("name") == name)


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="write nothing; exit 1 if any .aupreset is missing or stale")
    ap.add_argument("dir", nargs="?", default=str(PRESETS))
    args = ap.parse_args()

    folder = pathlib.Path(args.dir)
    vst = sorted(folder.glob("*.vstpreset"))
    if not vst:
        print(f"make-presets: no .vstpreset files in {folder}")
        return 0

    uid, codes, version_wanted, factory = processor_uid(), au_codes(), state_version(), factory_root()
    bad = written = stale = 0

    for v in vst:
        name = v.stem
        au = v.with_suffix(".aupreset")
        try:
            class_id, processor, controller = read_vstpreset(v.read_bytes())
            if class_id.upper() != uid:
                raise Bad(f"is for another plug-in (class {class_id})")
            params, loaded = check_state(processor, version_wanted, factory)
        except Bad as e:
            print(f"  {name:28} {e}")
            bad += 1
            continue

        note = (f"{params:3d} parameters" if params else "no parameters (defaults)") + f", {loaded:2d} cells"
        if au.exists() and aupreset_matches(au, name, processor, codes):
            print(f"  {name:28} {note}   .aupreset current")
            continue
        if args.check:
            print(f"  {name:28} {note}   .aupreset {'STALE' if au.exists() else 'MISSING'}")
            stale += 1
            continue
        au.write_bytes(aupreset_bytes(name, processor, controller, codes))
        print(f"  {name:28} {note}   -> {au.name}")
        written += 1

    for a in sorted(folder.glob("*.aupreset")):
        if not a.with_suffix(".vstpreset").exists():
            print(f"  {a.stem:28} .aupreset with no .vstpreset - the VST3 would ship without it")
            bad += 1

    print(f"\n{len(vst)} preset(s): {written} written, {stale} missing or stale, {bad} refused")
    if args.check and stale:
        print("Run tools/make-presets.py, load the result in an AU host, commit it.")
    return 1 if (bad or stale) else 0


if __name__ == "__main__":
    sys.exit(main())
