# VST3 / AUv2 porting template

The build system from the SpaceDub DXi→VST3 port, with the plug-in taken out
of it. Copy this folder, edit one block, and you have a VST3 and an Audio Unit
building on macOS — without rediscovering the SDK fetching, the AudioUnitSDK
version pin, the code-signing workaround or the `CMAKE_MODULE_PATH` trap.

**`PORTING-GUIDE.md` next to this file is the other half**: how to get a DXi's
DSP, parameters and dialog across. Read that first; this file is only about
getting something to build.

**`PORT-CHECKLIST.md`** is the working copy — six phases, tickable, with the
decisions to record at the top and the guide's section numbers against each
step. It comes with the template, so a fresh copy already has one.

## Using it

```sh
cp -R vst3-port-template ~/DXi-DEv/MyPlugin-VSTi
cd ~/DXi-DEv/MyPlugin-VSTi
```

Then:

1. **Edit the `PLUG-IN IDENTITY` block** at the top of `CMakeLists.txt` — name,
   version, description, company, and the two bundle identifiers. Nothing else
   in that file needs changing.
2. **Edit `resource/au-info.plist`** — every `<string>` marked `CHANGE ME`,
   in particular the three four-character codes. Skip this if you pass
   `--no-au`.
3. **Put your sources in `source/`** and your artwork in `resource/`. Both are
   globbed, so there is no file list to maintain.
4. Build:

```sh
./setup-xcode.sh
```

The first configure clones the VST3 SDK (~250 MB) into `external/`, so it
takes a few minutes; later ones reuse it. `./setup-xcode.sh --help` lists the
options — `--no-au`, `--no-validator`, `--makefiles`, `--clean`.

## What you must supply

The template builds nothing on its own; `source/` is empty. A minimal VST3
needs:

| File | What it is |
|---|---|
| `<Name>IDs.h` | freshly generated class UIDs — **never change once shipped** |
| `<Name>Params.*` | the parameter table (guide §4) |
| `<Name>Processor.*` | `AudioEffect` — the DSP and `process()` |
| `<Name>Controller.*` | `EditControllerEx1` — parameters and host plumbing |
| `<Name>Entry.cpp` | the plug-in factory |
| `<Name>Editor.*` | the window, if it has one |
| `version.h` | version strings |

`../Spaceduo-VSTi/source/` is a complete worked example of all seven.

## Two things that are deliberate

**Sources are globbed with `CONFIGURE_DEPENDS`,** so there is no file list to
maintain — at the cost of one wrinkle under the Xcode generator: the first
build after you *add* a source file regenerates the project mid-build and
compiles the old file list anyway. It shows up as the SDK post-build check
saying *"Bundle does not export the required 'GetPluginFactory' function"*.
A PRE_BUILD check catches this: it compares `source/` against the list
recorded when the project was generated and stops the build naming what
changed. Re-run `./setup-xcode.sh --no-open` and build again. Guide §7 has
the detail.

**Cache options are prefixed `PORT_`,** not the plug-in's name:
`-DPORT_BUILD_AU=OFF`, `-DPORT_CODE_SIGN_IDENTITY=...`,
`-DPORT_AU_SDK_TAG=...`. That way the template needs no editing beyond the
identity block. Rename them if you prefer, but rename them in
`setup-xcode.sh` too.

**`setup-xcode.sh` reads the plug-in name out of `CMakeLists.txt`** rather
than keeping its own copy, so there is exactly one place the name lives.

## Signing

Both bundles are ad-hoc signed automatically, which needs no Apple developer
certificate and is enough for macOS to load them, Apple Silicon included. To
sign properly:

```sh
cmake -B build -G Xcode -DPORT_CODE_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
```

`security find-identity -v -p codesigning` lists what you have. The reasoning
behind not leaving this to the SDK is in the `CMakeLists.txt` comments and in
guide §9.
