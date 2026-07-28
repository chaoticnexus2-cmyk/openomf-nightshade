#!/usr/bin/env bash
# Build + install the OMF "Kyra" expansion:
#   1. The two story tournaments (KYRA.TRN, RECKON.TRN) into build/resources
#   2. The HD Remaster mod zip into build/mods (RGBA portraits converted to the
#      paletted format this engine accepts)
#   3. A ready-to-play test pilot (CHAMPION.CHR) into the savegame directory
set -euo pipefail
cd "$(dirname "$0")"

REPO_ROOT="$(cd .. && pwd)"
HD_MOD_DIR="$REPO_ROOT/hdremaster-mod"
HD_MOD_URL="https://github.com/omf2097/openomf-hdremaster-mod.git"
SAVE_DIR="$HOME/Library/Application Support/OpenOMF/save"

# Build the expansion tools.
cmake -DBUILD_EXPANSION=ON -S . -B build >/dev/null
cmake --build build --target expansion_gen mkpilot mkportrait

# 1. Regenerate the indexed logo art (if Pillow + raw banners present).
if [ -f expansion-art/nightshade_raw.png ]; then
    python3 expansion-art/quantize_logo.py expansion-art >/dev/null || \
        echo "(logo quantize skipped -- install Pillow to regenerate art)"
fi

# 1b. Regenerate the original Nightshade Concord cast portraits (one .vph per
#     masked enforcer) and inject them into a copy of WORLD.PIC (-> NIGHTSHD.PIC),
#     which both tournaments reference for their faces. None reuse a base-game
#     face; each is a hand-generated original portrait.
if [ -d expansion-art/portraits ]; then
    python3 expansion-art/quantize_portrait.py expansion-art >/dev/null || \
        echo "(portrait quantize skipped -- install Pillow to regenerate the cast faces)"
fi
INJECTED_CAST=0
if [ -d expansion-art/portraits ] && [ -f build/resources/WORLD.PIC ]; then
    if ./build/mkportrait build/resources/WORLD.PIC expansion-art/portraits \
        build/resources/NIGHTSHD.PIC; then
        echo "Injected original Concord cast portraits -> build/resources/NIGHTSHD.PIC"
        INJECTED_CAST=1
    else
        echo "(cast portrait injection failed -- falling back to WORLD.PIC faces)"
    fi
fi
# Guarantee the tournaments' portrait PIC always exists: if injection was skipped
# or failed, use a plain copy of WORLD.PIC so faces still resolve (the cast shows
# placeholder faces instead of crashing).
if [ "$INJECTED_CAST" -eq 0 ] && [ -f build/resources/WORLD.PIC ]; then
    cp build/resources/WORLD.PIC build/resources/NIGHTSHD.PIC
    echo "(using WORLD.PIC copy as NIGHTSHD.PIC -- run with Pillow to bake the cast portraits)"
fi

# 2. Generate the tournaments (originals as binary templates + our logos).
./build/expansion_gen build/resources build/resources expansion-art

# 3. Package the optional HD Remaster mod (converts RGBA portraits -> indexed).
#    The HD assets live in their own upstream repo; fetch them if not present.
if [ ! -d "$HD_MOD_DIR" ]; then
    echo "Fetching HD Remaster assets from $HD_MOD_URL ..."
    git clone --depth 1 "$HD_MOD_URL" "$HD_MOD_DIR" 2>/dev/null || \
        echo "(could not fetch HD mod -- skipping)"
fi
if [ -d "$HD_MOD_DIR" ]; then
    mkdir -p build/mods
    python3 tools/expansion/hdmod_pack.py "$HD_MOD_DIR" build/mods/openomf-hdremaster.zip >/dev/null \
        && echo "Installed HD Remaster mod -> build/mods/openomf-hdremaster.zip" \
        || echo "(HD mod packaging skipped -- install Pillow)"
fi

# 4. Create the ready-to-play test pilot (maxed Jaguar, 200,000cr).
mkdir -p "$SAVE_DIR"
./build/mkpilot build/resources/PLAYERS.PIC "$SAVE_DIR/CHAMPION.CHR" "CHAMPION" 0

echo
echo "Done. Launch with ./play.sh:"
echo "  - Load Game -> CHAMPION (maxed Jaguar, 200,000cr) to test the new tournaments"
echo "  - Mechlab -> Select Tournament -> Nightshade Concord / Iron Reckoning"
