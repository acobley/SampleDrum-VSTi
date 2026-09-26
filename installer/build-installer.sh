#!/bin/bash
#-----------------------------------------------------------------------------
# Build a distributable .pkg installer for SampleDrum.
#
# MUST RUN ON macOS: pkgbuild, productbuild and codesign are Apple's, and none
# of them exists anywhere else. Build the plug-in first, then run this.
#
#   ./setup-xcode.sh --no-open && cmake --build build --config Release
#   installer/build-installer.sh
#
# The result is installer/SampleDrum-<version>.pkg, which installs
#
#   /Library/Audio/Plug-Ins/VST3/SampleDrum.vst3
#   /Library/Audio/Plug-Ins/Components/SampleDrum.component
#
# as two separately choosable components, so somebody who only wants one
# format gets only that one - and, when samples/ has any .wav files in it, a
# third: the factory samples, into /Users/Shared/SampleDrum/Samples.
#
# SIGNING - read this before sending the result to anyone:
#
#   With no arguments the payload is ad-hoc signed and the .pkg is not signed
#   at all. That installs fine on THIS machine and is fine over AirDrop or a
#   USB stick. A .pkg DOWNLOADED from the internet is quarantined, and an
#   unsigned, un-notarised one is refused by Gatekeeper - the person has to go
#   to System Settings -> Privacy & Security and allow it by hand, which is
#   exactly what an installer from an untrusted stranger looks like.
#
#   For real distribution you need BOTH halves of a Developer ID and a
#   notarisation:
#
#     installer/build-installer.sh \
#       --sign-app       "Developer ID Application: Your Name (TEAMID)" \
#       --sign-installer "Developer ID Installer: Your Name (TEAMID)" \
#       --notarize       my-notary-profile
#
#   where my-notary-profile was stored once with
#     xcrun notarytool store-credentials my-notary-profile \
#         --apple-id you@example.com --team-id TEAMID --password <app-specific>
#-----------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

NAME="SampleDrum"
VST3_ID="audio.sampledrum.vst3"
AU_ID="audio.sampledrum.audiounit"
SAMPLES_ID="audio.sampledrum.samples"

# THE FACTORY SAMPLE FOLDER. Must be the same string as kFactorySampleRoot in
# source/SampleDrumSample.h - a cell is recognised as factory by that prefix,
# and a sample installed anywhere else would be embedded in every project
# that used it. Once a project has been saved referring to this folder,
# moving it breaks that project, so it is checked here rather than trusted.
SAMPLES_DEST="/Users/Shared/SampleDrum/Samples"
grep -q "kFactorySampleRoot = \"$SAMPLES_DEST\"" "$ROOT/source/SampleDrumSample.h" || {
    echo "build-installer: SAMPLES_DEST is not kFactorySampleRoot in SampleDrumSample.h" >&2
    exit 1
}

#-----------------------------------------------------------------------------
# THE VERSION COMES OUT OF CMakeLists.txt, not out of a second copy here. Two
# places to edit is one place to forget.
#-----------------------------------------------------------------------------
VERSION="$(sed -n 's/^set(PLUGIN_VERSION[[:space:]]*"\([^"]*\)").*/\1/p' "$ROOT/CMakeLists.txt")"
if [ -z "$VERSION" ]; then
    echo "build-installer: could not read PLUGIN_VERSION out of CMakeLists.txt" >&2
    exit 1
fi

SIGN_APP="-"          # ad-hoc, which is what the build itself uses
SIGN_INSTALLER=""
NOTARY_PROFILE=""
CONFIG="Release"

while [ $# -gt 0 ]; do
    case "$1" in
        --sign-app)       SIGN_APP="$2"; shift 2 ;;
        --sign-installer) SIGN_INSTALLER="$2"; shift 2 ;;
        --notarize)       NOTARY_PROFILE="$2"; shift 2 ;;
        --config)         CONFIG="$2"; shift 2 ;;
        --list-identities)
            echo "Code-signing identities (for --sign-app):"
            security find-identity -v -p codesigning || true
            echo
            echo "All identities (the Developer ID INSTALLER one is here, not above,"
            echo "because it is not a code-signing certificate - for --sign-installer):"
            security find-identity -v || true
            exit 0 ;;
        -h|--help)        sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "build-installer: unknown argument '$1'" >&2; exit 1 ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "build-installer: this needs macOS - pkgbuild and productbuild are Apple's." >&2
    exit 1
fi

for tool in pkgbuild productbuild codesign; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-installer: $tool not found. Install the Xcode command line tools." >&2
        exit 1
    }
done

#-----------------------------------------------------------------------------
# THE IDENTITIES HAVE TO EXIST, AND THIS HAS TO BE CHECKED FIRST.
#
# codesign matches the string you give it as a LITERAL SUBSTRING of the
# certificate's common name, so a single stray character - most often a
# double space after "Application:", straight out of a copy and paste - makes
# it report "no identity found", which reads exactly like a missing
# certificate and sends you looking in Keychain Access for something that was
# there all along.
#
# Checked here, before any staging or signing, and the message says what is
# actually wrong. The SHA-1 hash from --list-identities is always accepted and
# cannot be mistyped this way; a hash is passed straight through.
#-----------------------------------------------------------------------------
check_identity () {
    label="$1"; wanted="$2"; available="$3"

    # A 40-character hex hash is an identity in its own right - nothing to match.
    case "$wanted" in
        [0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f])
            return 0 ;;
    esac

    if printf '%s\n' "$available" | grep -Fq "$wanted"; then
        return 0
    fi

    echo "build-installer: no $label identity matches" >&2
    echo "    $wanted" >&2
    echo >&2

    # THE USUAL CULPRIT, NAMED. Collapsing runs of spaces on both sides turns
    # "Application:  Andy" into "Application: Andy" - if THAT matches, the
    # certificate is fine and the string is not.
    squashed="$(printf '%s' "$wanted" | tr -s ' ')"
    if printf '%s\n' "$available" | tr -s ' ' | grep -Fq "$squashed"; then
        echo "It matches once runs of spaces are collapsed, so the certificate is" >&2
        echo "there and the string is wrong - almost certainly a doubled space." >&2
        echo "Use this instead, or pass the SHA-1 hash:" >&2
        echo "    $squashed" >&2
    else
        echo "Available:" >&2
        printf '%s\n' "$available" | sed 's/^/    /' >&2
    fi
    exit 1
}

if [ "$SIGN_APP" != "-" ]; then
    check_identity "--sign-app" "$SIGN_APP" "$(security find-identity -v -p codesigning 2>/dev/null)"
fi

if [ -n "$SIGN_INSTALLER" ]; then
    # NOT -p codesigning: a Developer ID Installer certificate is not a
    # code-signing certificate and does not appear in that list at all.
    check_identity "--sign-installer" "$SIGN_INSTALLER" "$(security find-identity -v 2>/dev/null | grep "Developer ID Installer")"
fi

BUILT="$ROOT/build/VST3/$CONFIG"
VST3_SRC="$BUILT/$NAME.vst3"
AU_SRC="$BUILT/$NAME.component"

for bundle in "$VST3_SRC" "$AU_SRC"; do
    [ -d "$bundle" ] || {
        echo "build-installer: $bundle is not there." >&2
        echo "  Build first:  ./setup-xcode.sh --no-open && cmake --build build --config $CONFIG" >&2
        exit 1
    }
done

WORK="$HERE/build"
rm -rf "$WORK"
mkdir -p "$WORK/root-vst3" "$WORK/root-au" "$WORK/pkgs"

# THE FACTORY SAMPLES: only what a cell will take - .wav, keeping
# sub-folders. The README and anything else in samples/ stays behind. No
# samples, no third component - an empty choice in the installer would be
# worse than none.
mkdir -p "$WORK/root-samples"
SAMPLE_COUNT=0
if [ -d "$ROOT/samples" ]; then
    while IFS= read -r -d '' f; do
        rel="${f#"$ROOT/samples/"}"
        mkdir -p "$WORK/root-samples/$(dirname "$rel")"
        cp "$f" "$WORK/root-samples/$rel"
        SAMPLE_COUNT=$((SAMPLE_COUNT + 1))
    done < <(find "$ROOT/samples" -type f -iname '*.wav' -print0)
fi

echo "==> SampleDrum $VERSION, from $CONFIG"
if [ "$SAMPLE_COUNT" -gt 0 ]; then
    echo "==> $SAMPLE_COUNT factory sample(s) staged for $SAMPLES_DEST"
else
    echo "==> no factory samples in samples/ - that component is left out"
fi

#-----------------------------------------------------------------------------
# STAGE. cp -R and not a move: the build tree is left exactly as it was, so
# running this never costs you a rebuild.
#-----------------------------------------------------------------------------
cp -R "$VST3_SRC" "$WORK/root-vst3/$NAME.vst3"
cp -R "$AU_SRC"   "$WORK/root-au/$NAME.component"

#-----------------------------------------------------------------------------
# THE SYMLINK THAT WOULD HAVE SHIPPED A DEAD AUDIO UNIT.
#
# Steinberg's AU wrapper has no plug-in code of its own: it loads the VST3 out
# of its own bundle, at Contents/Resources/plugin.vst3. CMake puts a SYMLINK
# there, pointing at an ABSOLUTE PATH inside this machine's build tree:
#
#   .../SampleDrum.component/Contents/Resources/plugin.vst3
#       -> /Users/<you>/DXi-DEv/SampleDrum-VSTi/build/VST3/Release/SampleDrum.vst3
#
# That is right for development - rebuild the VST3 and the AU follows - and it
# is fatal in an installer. Copied as-is onto another machine the symlink
# dangles, the wrapper finds nothing to load, and the AU fails to instantiate
# with no useful error. A .pkg built by pointing pkgbuild straight at the
# build directory has exactly this bug and looks perfect until somebody else
# tries it.
#
# So the link is replaced with a REAL COPY of the VST3 bundle, and the
# .component becomes self-contained.
#-----------------------------------------------------------------------------
AU_RESOURCES="$WORK/root-au/$NAME.component/Contents/Resources"
if [ -L "$AU_RESOURCES/plugin.vst3" ] || [ -e "$AU_RESOURCES/plugin.vst3" ]; then
    rm -rf "$AU_RESOURCES/plugin.vst3"
fi
mkdir -p "$AU_RESOURCES"
cp -R "$VST3_SRC" "$AU_RESOURCES/plugin.vst3"
echo "==> embedded a real copy of $NAME.vst3 inside the .component"

#-----------------------------------------------------------------------------
# AND THE GUARD, because the above fixes the symlink we know about and this
# catches the one somebody adds later. Any symlink pointing at an absolute
# path is a link to this machine and cannot survive the trip.
#-----------------------------------------------------------------------------
escapes=""
while IFS= read -r link; do
    target="$(readlink "$link")"
    case "$target" in
        /*) escapes="$escapes
  $link -> $target" ;;
    esac
done < <(find "$WORK/root-vst3" "$WORK/root-au" "$WORK/root-samples" -type l)

if [ -n "$escapes" ]; then
    echo "build-installer: absolute symlinks in the payload - these point at THIS" >&2
    echo "machine and would dangle on any other:$escapes" >&2
    exit 1
fi
echo "==> no symlink in the payload points outside it"

#-----------------------------------------------------------------------------
# SIGN, INNERMOST FIRST. The nested plugin.vst3 has to be signed before the
# .component that contains it, or signing the outer bundle seals a signature
# that is then invalidated by signing the inner one.
#-----------------------------------------------------------------------------
#
# AND THE FLAGS ARE NOT THE SAME FOR AD-HOC AND FOR A DEVELOPER ID.
#
# Notarisation requires BOTH a secure timestamp and the hardened runtime, and
# Apple checks what is INSIDE the package as well as the package itself. A
# payload signed the ad-hoc way and then wrapped in a properly signed .pkg is
# rejected, with a message about the payload rather than about these flags.
#
# Ad-hoc signing cannot carry a timestamp - there is no certificate for a
# timestamp authority to countersign - so --timestamp=none is right there and
# only there. Asking for one anyway makes every local build wait on Apple's
# timestamp server for nothing.
#-----------------------------------------------------------------------------
if [ "$SIGN_APP" = "-" ]; then
    CODESIGN_FLAGS=(--force --timestamp=none)
    echo "==> signing payload ad-hoc (local use only - cannot be notarised)"
else
    CODESIGN_FLAGS=(--force --timestamp --options runtime)
    echo "==> signing payload as: $SIGN_APP"
    echo "    with a secure timestamp and the hardened runtime, both of which"
    echo "    notarisation requires"
fi

# INNERMOST FIRST, still: see above.
codesign "${CODESIGN_FLAGS[@]}" --sign "$SIGN_APP" "$AU_RESOURCES/plugin.vst3"
codesign "${CODESIGN_FLAGS[@]}" --sign "$SIGN_APP" "$WORK/root-au/$NAME.component"
codesign "${CODESIGN_FLAGS[@]}" --sign "$SIGN_APP" "$WORK/root-vst3/$NAME.vst3"

codesign --verify --deep --strict "$WORK/root-vst3/$NAME.vst3"
codesign --verify --deep --strict "$WORK/root-au/$NAME.component"
echo "==> signatures verify"

#-----------------------------------------------------------------------------
# COMPONENT PACKAGES, one per format, so the choice pane can offer them
# separately.
#-----------------------------------------------------------------------------
pkgbuild --quiet \
    --root "$WORK/root-vst3" \
    --identifier "$VST3_ID.pkg" \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "$WORK/pkgs/$NAME-VST3.pkg"

#-----------------------------------------------------------------------------
# THE SCRIPTS ARE STAGED AND MADE EXECUTABLE RATHER THAN USED IN PLACE.
#
# pkgbuild takes the --scripts directory as it finds it, and a postinstall
# without its execute bit is simply never run: the install still reports
# success and the Audio Unit just does not appear until the next log-out.
# The bit does not survive every way these files travel - a plain cp, a zip,
# AirDrop and an editor that rewrites in place have all dropped it - so it is
# set here on a copy instead of being relied on in the checkout.
#-----------------------------------------------------------------------------
mkdir -p "$WORK/scripts-au"
cp "$HERE/scripts-au/postinstall" "$WORK/scripts-au/postinstall"
chmod +x "$WORK/scripts-au/postinstall"

pkgbuild --quiet \
    --root "$WORK/root-au" \
    --identifier "$AU_ID.pkg" \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    --scripts "$WORK/scripts-au" \
    "$WORK/pkgs/$NAME-AU.pkg"

# The samples are data, not code, so nothing here is signed - but they ride
# inside the same notarised product archive as everything else. Not
# relocatable, readable by everyone, owned by root so one user cannot change
# what another user's projects sound like.
if [ "$SAMPLE_COUNT" -gt 0 ]; then
    pkgbuild --quiet \
        --root "$WORK/root-samples" \
        --identifier "$SAMPLES_ID.pkg" \
        --version "$VERSION" \
        --install-location "$SAMPLES_DEST" \
        --ownership recommended \
        "$WORK/pkgs/$NAME-Samples.pkg"
    echo "==> $SAMPLE_COUNT sample file(s) packaged for $SAMPLES_DEST"
fi

echo "==> component packages built"

#-----------------------------------------------------------------------------
# THE PRODUCT ARCHIVE. distribution.xml carries the panes and the two
# choices; the version is substituted rather than duplicated.
#-----------------------------------------------------------------------------
mkdir -p "$WORK/resources"
cp "$HERE/resources/welcome.html" "$HERE/resources/conclusion.html" "$WORK/resources/"
cp "$ROOT/LICENSE" "$WORK/resources/license.txt"

sed "s/__VERSION__/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

# A distribution that names a package productbuild was not given is one
# Installer cannot resolve, and it fails at the page stage naming none of
# it - so with no sample files, every block between the SAMPLES markers comes
# out. Marked as a RANGE rather than line by line, because a multi-line XML
# element cannot carry a comment on each of its lines.
if [ "$SAMPLE_COUNT" -eq 0 ]; then
    sed '/BEGIN SAMPLES/,/END SAMPLES/d' "$WORK/distribution.xml" > "$WORK/distribution-trimmed.xml"
    mv "$WORK/distribution-trimmed.xml" "$WORK/distribution.xml"
    echo "==> no samples, so the samples choice is not in the distribution"
fi

#-----------------------------------------------------------------------------
# VALIDATE THE DISTRIBUTION, TWICE, because a bad one is not caught by
# building - it is caught by somebody else's Mac refusing the package with
# "com.apple.installer.pagecontroller error -1", which says nothing about what
# is actually wrong. Installer reads this XML to build its panes, and if it
# cannot parse or resolve it, that is the error you get.
#
# Once on the substituted source, and once on the copy that actually ended up
# INSIDE the product archive - which is the one the other machine will read,
# and is not necessarily byte-identical to what went in.
#-----------------------------------------------------------------------------
if command -v xmllint >/dev/null 2>&1; then
    xmllint --noout "$WORK/distribution.xml" || {
        echo "build-installer: distribution.xml is not well-formed XML." >&2
        exit 1
    }
    echo "==> distribution.xml is well-formed"
fi

UNSIGNED="$WORK/$NAME-$VERSION-unsigned.pkg"
FINAL="$HERE/$NAME-$VERSION.pkg"

productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK/pkgs" \
    --resources "$WORK/resources" \
    "$UNSIGNED"

#-----------------------------------------------------------------------------
# AND THE ONE THAT SHIPPED. pkgutil --expand unpacks the product archive; the
# Distribution inside it is what Installer will read on the far machine.
#-----------------------------------------------------------------------------
if command -v xmllint >/dev/null 2>&1; then
    rm -rf "$WORK/expanded"
    pkgutil --expand "$UNSIGNED" "$WORK/expanded"

    [ -f "$WORK/expanded/Distribution" ] || {
        echo "build-installer: the product archive has no Distribution file." >&2
        exit 1
    }

    xmllint --noout "$WORK/expanded/Distribution" || {
        echo "build-installer: the Distribution INSIDE the package is not" >&2
        echo "well-formed. This is the file the installing machine reads." >&2
        exit 1
    }

    # EVERY COMPONENT PACKAGE THE DISTRIBUTION NAMES MUST ACTUALLY BE IN
    # THERE. A pkg-ref to a package productbuild did not embed is a
    # distribution Installer cannot resolve, and it fails at the page stage
    # naming none of this.
    #
    # FLATTENED FIRST. pkg-ref elements are written across several lines, and
    # a line-based scan of them finds nothing - and then reports that nothing
    # is missing, which is a guard that passes everything. Asked for it, got
    # it, tested it, and that is exactly what the first version did.
    refs=$(tr '\n' ' ' < "$WORK/expanded/Distribution" \
           | grep -o '<pkg-ref[^>]*>[^<]*\.pkg</pkg-ref>' \
           | sed 's/.*>\([^<>]*\.pkg\)<.*/\1/')

    if [ -z "$refs" ]; then
        echo "build-installer: found no pkg-ref in the shipped Distribution." >&2
        echo "Either the archive is malformed or this check has stopped working;" >&2
        echo "either way it must not pass silently." >&2
        exit 1
    fi

    missing=""
    for ref in $refs; do
        ref="${ref#\#}"                   # both "name.pkg" and "#name.pkg" are legal
        [ -e "$WORK/expanded/$ref" ] || missing="$missing $ref"
    done

    if [ -n "$missing" ]; then
        echo "build-installer: the Distribution names packages that are not" >&2
        echo "inside the archive:$missing" >&2
        exit 1
    fi

    echo "==> the shipped Distribution is well-formed and every pkg-ref resolves"
fi

if [ -n "$SIGN_INSTALLER" ]; then
    echo "==> signing the installer as: $SIGN_INSTALLER"
    productsign --sign "$SIGN_INSTALLER" "$UNSIGNED" "$FINAL"
    pkgutil --check-signature "$FINAL"
else
    cp "$UNSIGNED" "$FINAL"
    echo "==> NOT SIGNED. Fine locally; Gatekeeper will refuse it if it is"
    echo "    downloaded. See --sign-installer in the header of this script."
fi

if [ -n "$NOTARY_PROFILE" ]; then
    [ -n "$SIGN_INSTALLER" ] || {
        echo "build-installer: --notarize needs --sign-installer; Apple will not" >&2
        echo "notarise an unsigned package." >&2
        exit 1
    }
    [ "$SIGN_APP" != "-" ] || {
        echo "build-installer: --notarize needs --sign-app with a real Developer ID" >&2
        echo "too. Apple notarises the package AND what is inside it; an ad-hoc" >&2
        echo "signed payload is rejected however well the package itself is signed." >&2
        exit 1
    }
    echo "==> submitting for notarisation (this waits, and can take minutes)"
    xcrun notarytool submit "$FINAL" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$FINAL"
    xcrun stapler validate "$FINAL"
    echo "==> notarised and stapled"

    #-------------------------------------------------------------------------
    # THE ONLY TEST THAT MEANS ANYTHING: ask Gatekeeper, on this machine, the
    # same question it will be asked on the one that downloads it. Everything
    # up to here says the paperwork is in order; this says the answer is yes.
    #-------------------------------------------------------------------------
    assessment="$(spctl --assess --type install -vv "$FINAL" 2>&1 || true)"
    echo "$assessment"

    if printf '%s' "$assessment" | grep -q "source=Notarized Developer ID"; then
        echo "==> Gatekeeper accepts it as a notarised Developer ID package"
    else
        echo "build-installer: Gatekeeper did NOT accept the finished package." >&2
        echo "It is signed and stapled but would still be refused on a machine" >&2
        echo "that downloads it. Do not ship this." >&2
        exit 1
    fi
fi


#-----------------------------------------------------------------------------
# RECORD WHICH COMMIT THIS PACKAGE CAME FROM.
#
# publish-release.sh tags the commit the binary was built from, and by the
# time it runs HEAD has usually moved: pasting the checksum into the release
# notes is itself a commit. Tagging HEAD then marks a tree that did not
# produce the binary, which is exactly the thing a tag is supposed to settle.
#
# Recorded here, where the answer is still right, rather than inferred later.
# `dirty` matters: a package built from uncommitted changes has no commit that
# describes it, and the publish script refuses rather than tagging something
# close to it.
#-----------------------------------------------------------------------------
if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    {
        echo "commit=$(git -C "$ROOT" rev-parse HEAD)"
        echo "version=$VERSION"
        if [ -n "$(git -C "$ROOT" status --porcelain 2>/dev/null)" ]; then
            echo "dirty=yes"
        else
            echo "dirty=no"
        fi
    } > "$HERE/.built-from"
    echo "==> recorded the build commit in installer/.built-from"
fi

rm -rf "$WORK"

echo
echo "    $FINAL"
echo
echo "    Installs:  /Library/Audio/Plug-Ins/VST3/$NAME.vst3"
echo "               /Library/Audio/Plug-Ins/Components/$NAME.component"
if [ "$SAMPLE_COUNT" -gt 0 ]; then
echo "               $SAMPLES_DEST ($SAMPLE_COUNT files)"
fi
echo
echo "    Check what is really in it with:"
echo "      pkgutil --payload-files \"$FINAL\" | head"
