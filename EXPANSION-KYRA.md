# Expansion design & build notes

Detailed notes on how the Nightshade Edition expansion is built. For an
overview and how to play, see [README.md](README.md); for the diff against
vanilla, see [CHANGES.md](CHANGES.md).

## The two tournaments

| | Nightshade Concord (`KYRA.TRN`) | Iron Reckoning (`RECKON.TRN`) |
|---|---|---|
| Template | `NORTH_AM.TRN` | `WAR.TRN` |
| Cutscene / photos | `north_am.bk` / `north_am.pic` | `war.bk` / `war.pic` |
| Entry fee | 15,000 cr | 25,000 cr |
| Assumed value | 70,000 | 90,000 |
| Final boss | Vance (gated) | Kreissack, the patron (gated) |

Both feature the classic OMF roster (Cossette, Milano, Jean-Paul, Christian,
Angel, Ibrahim, Shirro, Raven) in escalating mechs, with a new antagonist,
**Vance**.

## Storytelling channels

OMF tournaments tell story through three places, all authored here:

1. **Tournament description** — the intro shown on the select screen
   (`locales[0].description`, with `{WIDTH}/{CENTER}/{VMOVE}/{COLOR}` layout
   tags).
2. **Pre-fight taunts** — each enemy's `quotes[0]`, shown on the VS screen.
3. **Victory cutscene** — up to 10 pages in `locales[0].end_texts[0][...]`,
   advanced with Punch/Kick.

All text is second-person and gender-neutral and uses the `~1` token, which the
engine substitutes with the player's chosen pilot name (see `vs.c` /
`cutscene.c` in [CHANGES.md](CHANGES.md)).

## How a tournament is generated (`tools/expansion/gen.c`)

1. Load an original tournament as a **binary template** (keeps a valid logo
   sprite, palette, and the `.BK`/`.PIC` bindings).
2. Overwrite the roster: names, mechs (HAR ids), stats, AI, difficulty, ranks,
   and per-enemy taunts. Boss `pilot_id` is remapped off `PILOT_KREISSACK` to
   avoid the engine's hardcoded Veteran-gate + canned insult.
3. Set the title, description, and the multi-page victory story.
4. Bake a custom title banner into `locales[0].logo`: the indexed `.lgo` art
   (from `expansion-art/`) is loaded, its 40 colours written into the tournament
   palette range (128–167), and the sprite re-encoded with the engine's
   `sd_sprite_vga_encode()`.
5. Save with `sd_tournament_save()` → byte-correct `.TRN`.

The engine auto-discovers `*.TRN` in the resources dir, so no recompile is
needed.

## Title-banner art (`expansion-art/`)

- `*_raw.png` — source banners (generated).
- `quantize_logo.py` — crops, downscales, and quantizes each banner to ≤39
  colours, emitting a raw `.lgo` (`<w><h><40·RGB palette><indices>`) that the
  generator bakes into the tournament logo. Index 0 is transparent.

## Test pilot (`tools/expansion/mkpilot.c`)

Builds a fresh `CHAMPION.CHR` from scratch — fully-upgraded Jaguar, veteran
attributes, 200,000 cr, between tournaments — pulling a real portrait + palette
straight from `PLAYERS.PIC`. Saved into the OpenOMF save directory so it appears
under **Load Game**.

## HD Remaster mod (`tools/expansion/hdmod_pack.py`)

The upstream [HD mod](https://github.com/omf2097/openomf-hdremaster-mod) ships
RGBA assets, but this engine's mod loader only accepts 8-bit **paletted** PNGs.
The packer quantizes each RGBA portrait to its sibling `har_color.png` palette
(the palette the engine uses to colour portraits), keeps the already-paletted
scene frames, adds a `manifest.ini`, and zips it into `build/mods/`.

## Reproduce everything

```bash
./build-expansion.sh
```
