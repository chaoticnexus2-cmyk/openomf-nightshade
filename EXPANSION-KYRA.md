# Expansion design & build notes

Detailed notes on how the Nightshade Edition expansion is built. For an
overview and how to play, see [README.md](README.md); for the diff against
vanilla, see [CHANGES.md](CHANGES.md).

## The two tournaments

| | Nightshade Concord (`KYRA.TRN`) | Iron Reckoning (`RECKON.TRN`) |
|---|---|---|
| Template | `NORTH_AM.TRN` | `WAR.TRN` |
| Cutscene BK | `north_am.bk` (from template) | `war.bk` (from template) |
| Portrait PIC | `NIGHTSHD.PIC` | `NIGHTSHD.PIC` |
| Entry fee | 15,000 cr | 25,000 cr |
| Assumed value | 70,000 | 90,000 |
| Final boss | **The Cardinal** (rank 1) | **The Meridian** (rank 1) |

Both feature an **original masked cast** — the Nightshade Concord — a fight-cult
of enforcers with hand-generated portraits (none reused from the base game):
Sparrow, Wager, Vesper, Seraph, Cinder, Bastion, Marrow, Requiem, and the boss
**the Cardinal**. Iron Reckoning brings the survivors back in deadlier prototype
mechs behind the true architect, **the Meridian**. Behind the masks each enemy
keeps a classic pilot AI profile so they still fight like OMF veterans.

The story is a **father-revenge arc**: the player hunts the syndicate that
murdered their father (the champion who trained them), and the revenge matures
into justice by the end of Iron Reckoning.

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

## Roster ordering — a real engine constraint

The engine (`sd_chr_from_trn` in `formats/chr.c`) assigns each non-secret enemy
a rank **sequentially by array order**: `roster[0]` becomes rank 1, `roster[1]`
rank 2, and so on. The player then climbs **down** toward rank 1
(`mechlab_next_opponent` picks `rank == player_rank - 1`, and `arena.c`
decrements the winner's rank), and the victory cutscene only fires when the
player beats the **rank-1** pilot while sitting at rank 2 (the `champion` check
in `newsroom.c`).

Two consequences drive the design:

- **The final boss must be `roster[0]`** (rank 1, fought last), and the weakest
  first-fought enemy must be the **last** array element. Each roster in `gen.c`
  is therefore authored **boss-first**.
- **Bosses must be normal (non-secret) pilots.** Secret/gated pilots have rank 0
  and so can *never* be the rank-1 champion — a gated boss would never trigger
  the ending cutscene. So the Cardinal and the Meridian are ordinary rank-1
  pilots, not `secret` challengers.

## How a tournament is generated (`tools/expansion/gen.c`)

1. Load an original tournament as a **binary template** (keeps a valid logo
   sprite, palette, and the `.BK` cutscene binding).
2. Point the tournament at `NIGHTSHD.PIC` for portraits
   (`sd_tournament_set_pic_name`).
3. Overwrite the roster **boss-first** (see above): names, mechs (HAR ids),
   stats, AI, difficulty, per-enemy `photo_id` (into `NIGHTSHD.PIC`), and taunts.
   Boss `pilot_id` is remapped off `PILOT_KREISSACK` to avoid the engine's
   hardcoded Veteran-gate + canned insult.
4. Set the title, description, and the multi-page victory story.
5. Bake a custom title banner into `locales[0].logo`: the indexed `.lgo` art
   (from `expansion-art/`) is loaded, its 40 colours written into the tournament
   palette range (128–167), and the sprite re-encoded with the engine's
   `sd_sprite_vga_encode()`.
6. Save with `sd_tournament_save()` → byte-correct `.TRN`.

The engine auto-discovers `*.TRN` in the resources dir, so no recompile is
needed.

## Original antagonist portraits (`tools/expansion/mkportrait.c`)

Tournament enemies do not embed a portrait on disk — each references a face by
`photo_id` into the tournament's shared PIC. To give the Concord an original
look, the expansion **appends new faces** to a copy of `WORLD.PIC`:

1. `expansion-art/portraits/<stem>.png` — the hand-generated source portraits
   (one per cast member; see `enum concord_face` in `tools/expansion/vance.h`).
2. `quantize_portrait.py` crops each to the in-game portrait footprint (51×61),
   quantizes to ≤47 colours, keys out the dark backdrop to transparency (index
   0), and emits a raw `.vph` (`<u16 w><u16 h><48·RGB palette><w·h indices>`).
3. `mkportrait` loads `WORLD.PIC`, pads the gap up to `CONCORD_FACE_BASE` (50)
   with a valid filler face, encodes each `.vph` with `sd_sprite_vga_encode()`,
   stores it at its `enum concord_face` index, and saves `NIGHTSHD.PIC` via the
   engine's own `sd_pic_save()`.

The face indices (50+) sit safely above `WORLD.PIC`'s ~44 real faces, so nothing
collides with a classic-pilot portrait. `vance.h` is the single source of truth
for the cast → index → filename mapping, shared by `gen.c` and `mkportrait.c`.

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
