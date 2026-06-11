/** @file gen.c
 * @brief OMF 2097 "Kyra" Expansion Pack tournament generator.
 *
 * Builds two new tournament (.TRN) files for OpenOMF by loading an original
 * tournament as a binary template (so the logo sprite, palette and the BK
 * cutscene / PIC photo bindings remain valid), then overwriting the roster,
 * mechs (HARs), pre-fight taunts, descriptions and the victory cutscene story.
 *
 * Output is byte-correct because it uses OpenOMF's own sd_tournament_save().
 *
 * Usage: expansion_gen <resource_dir> <output_dir>
 * @license MIT
 */

#include "formats/error.h"
#include "formats/pilot.h"
#include "formats/sprite.h"
#include "formats/tournament.h"
#include "formats/vga_image.h"
#include "game/common_defines.h"
#include "utils/allocator.h"
#include "utils/c_string_util.h"
#include "utils/path.h"
#include "video/vga_palette.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- small helpers --------------------------------------------------------

static void set_str(char **dst, const char *s) {
    if(*dst) {
        omf_free(*dst);
    }
    *dst = s ? omf_strdup(s) : NULL;
}

// Describes one enemy fighter in a tournament.
typedef struct {
    const char *name; // display name (<= 17 chars)
    int pilot_id;     // maps to an OMF pilot enum (cosmetic in tournament mode)
    int har_id;       // which mech (HAR_*)
    int difficulty;   // 0..3
    int power, agility, endurance; // 1..25
    int arm_power, leg_power, arm_speed, leg_speed, armor, stun; // 0..9
    int rank;         // story rank (1 = toughest/boss in OMF ranking is low number)
    int secret;       // gated boss (only appears after requirements met)
    int req_rank;     // appears when player reaches this rank (for secret bosses)
    const char *quote; // pre-fight taunt (advances the plot)
} enemy_spec;

// Overwrite a pilot slot with our data, preserving the template's photo_id,
// palette and photo sprite so the in-game pilot photo stays valid.
static void apply_enemy(sd_pilot *p, const enemy_spec *e) {
    snprintf(p->name, sizeof(p->name), "%s", e->name);
    // Never use PILOT_KREISSACK (10): the engine special-cases that id with a
    // Veteran-difficulty gate and a canned insult, which would override our
    // custom quote. Remap it to a normal pilot id.
    p->pilot_id = (uint8_t)((e->pilot_id >= PILOT_KREISSACK) ? PILOT_RAVEN : e->pilot_id);
    p->har_id = (uint8_t)e->har_id;
    p->difficulty = (uint8_t)(e->difficulty & 0x3);
    p->power = (uint8_t)e->power;
    p->agility = (uint8_t)e->agility;
    p->endurance = (uint8_t)e->endurance;
    p->arm_power = (uint8_t)e->arm_power;
    p->leg_power = (uint8_t)e->leg_power;
    p->arm_speed = (uint8_t)e->arm_speed;
    p->leg_speed = (uint8_t)e->leg_speed;
    p->armor = (uint8_t)e->armor;
    p->stun_resistance = (uint8_t)e->stun;
    p->rank = (uint8_t)e->rank;
    p->wins = (uint16_t)(40 - e->rank * 2);
    p->losses = (uint16_t)(e->rank);
    p->money = 5000 + (20 - e->rank) * 1500;

    // Offense/defense preference (100 baseline, higher = more aggressive).
    p->offense = (uint16_t)(110 + (20 - e->rank) * 4);
    p->defense = (uint16_t)(110 + (20 - e->rank) * 3);

    // AI attitude / behaviour, scaled by how far up the ladder they are.
    int aggr = 80 + (20 - e->rank) * 12;
    p->att_normal = 60;
    p->att_hyper = (uint8_t)(20 + (20 - e->rank) * 3);
    p->att_jump = 30;
    p->att_def = 40;
    p->att_sniper = 25;
    p->ap_throw = (int16_t)(aggr / 2);
    p->ap_special = (int16_t)aggr;
    p->ap_jump = 60;
    p->ap_high = 80;
    p->ap_low = 80;
    p->ap_middle = 80;
    p->pref_jump = 40;
    p->pref_fwd = (int16_t)(aggr);
    p->pref_back = 20;
    p->learning = (float)(2 + (20 - e->rank) * 0.5);
    p->forget = 1.0f;

    // Gating for secret bosses.
    p->secret = (uint8_t)e->secret;
    p->only_fight_once = 0;
    p->req_rank = (uint8_t)e->req_rank;
    p->req_max_rank = 0;

    // Pre-fight taunt (locale 0).
    set_str(&p->quotes[0], e->quote);
}

// Ensure the tournament has exactly `count` enemy slots, cloning from existing
// template pilots for any new slots so photo/palette data stays valid. Then
// apply our specs over each slot.
static void build_roster(sd_tournament_file *trn, const enemy_spec *specs, int count) {
    int base = trn->enemy_count;
    if(base <= 0) {
        fprintf(stderr, "template tournament has no enemies to clone\n");
        exit(1);
    }
    // Grow if needed (clone a valid template pilot for photo/palette).
    for(int i = base; i < count; i++) {
        trn->enemies[i] = omf_calloc(1, sizeof(sd_pilot));
        sd_pilot_create(trn->enemies[i]);
        sd_pilot_clone(trn->enemies[i], trn->enemies[i % base]);
    }
    // Shrink if needed.
    for(int i = count; i < base; i++) {
        if(trn->enemies[i]) {
            sd_pilot_free(trn->enemies[i]);
            omf_free(trn->enemies[i]);
            trn->enemies[i] = NULL;
        }
    }
    trn->enemy_count = (uint16_t)count;

    for(int i = 0; i < count; i++) {
        apply_enemy(trn->enemies[i], &specs[i]);
    }
}

// Clear all victory/story text pages in locale 0, then set the given pages.
static void set_story(sd_tournament_file *trn, const char *const *pages, int n) {
    sd_tournament_locale *loc = trn->locales[0];
    for(int har = 0; har < 11; har++) {
        for(int page = 0; page < 10; page++) {
            if(loc->end_texts[har][page]) {
                omf_free(loc->end_texts[har][page]);
                loc->end_texts[har][page] = NULL;
            }
        }
    }
    for(int page = 0; page < n && page < 10; page++) {
        loc->end_texts[0][page] = omf_strdup(pages[page]);
    }
}

static void finalize_locale(sd_tournament_file *trn, const char *title, const char *description) {
    sd_tournament_locale *loc = trn->locales[0];
    set_str(&loc->title, title);
    set_str(&loc->description, description);
    if(loc->stripped_description) {
        omf_free(loc->stripped_description);
        loc->stripped_description = NULL;
    }
    parse_tournament_description(loc);
}

// Replace the tournament logo with a generated raw logo file (.lgo) produced by
// expansion-art/quantize_logo.py. Format: <u16 w><u16 h><40*3 palette RGB><w*h
// index bytes>. Index 0 is transparent; opaque colours use indices 1..39. Those
// 40 palette entries are loaded into the tournament palette range (128..167) and
// the sprite pixels are remapped to match, so the logo renders correctly on the
// tournament-select screen. Reads raw bytes directly to avoid libpng in the
// standalone tool.
static int set_logo_from_lgo(sd_tournament_file *trn, const char *lgo_path, int pos_x, int pos_y) {
    FILE *f = fopen(lgo_path, "rb");
    if(!f) {
        fprintf(stderr, "  WARN: cannot open logo %s (keeping template logo)\n", lgo_path);
        return 1;
    }
    uint8_t hdr[4];
    uint8_t pal[120];
    if(fread(hdr, 1, 4, f) != 4 || fread(pal, 1, 120, f) != 120) {
        fprintf(stderr, "  WARN: short header in %s\n", lgo_path);
        fclose(f);
        return 1;
    }
    int w = hdr[0] | (hdr[1] << 8);
    int h = hdr[2] | (hdr[3] << 8);
    size_t n = (size_t)w * (size_t)h;

    sd_vga_image img;
    if(sd_vga_image_create(&img, w, h) != SD_SUCCESS) {
        fclose(f);
        return 1;
    }
    if(fread(img.data, 1, n, f) != n) {
        fprintf(stderr, "  WARN: short pixel data in %s\n", lgo_path);
        fclose(f);
        sd_vga_image_free(&img);
        return 1;
    }
    fclose(f);

    // Load the 40 palette entries into the tournament palette range 128..167.
    // (index 0 is the transparent marker and is never drawn.)
    for(int k = 0; k < 40; k++) {
        trn->pal.colors[128 + k].r = pal[k * 3 + 0];
        trn->pal.colors[128 + k].g = pal[k * 3 + 1];
        trn->pal.colors[128 + k].b = pal[k * 3 + 2];
    }
    // Remap pixel indices: 0 stays transparent; 1..39 -> 129..167.
    for(size_t p = 0; p < n; p++) {
        uint8_t v = (uint8_t)img.data[p];
        if(v != 0) {
            img.data[p] = (char)(128 + v);
        }
    }

    // Re-encode the locale 0 logo sprite from our image.
    sd_sprite *logo = trn->locales[0]->logo;
    sd_sprite_free(logo);
    sd_sprite_create(logo);
    if(sd_sprite_vga_encode(logo, &img) != SD_SUCCESS) {
        fprintf(stderr, "  WARN: failed to encode logo sprite\n");
        sd_vga_image_free(&img);
        return 1;
    }
    logo->pos_x = (int16_t)pos_x;
    logo->pos_y = (int16_t)pos_y;
    sd_vga_image_free(&img);
    printf("  logo replaced from %s (%dx%d at %d,%d)\n", lgo_path, logo->width, logo->height, pos_x, pos_y);
    return 0;
}

static int save_trn(sd_tournament_file *trn, const char *out_dir, const char *fname) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", out_dir, fname);
    path out;
    path_from_c(&out, buf);
    int ret = sd_tournament_save(trn, &out);
    if(ret != SD_SUCCESS) {
        fprintf(stderr, "FAILED to save %s: %s\n", buf, sd_get_error(ret));
        return 1;
    }
    printf("  wrote %s (%u enemies, bk=%s pic=%s id=%d fee=%d)\n", buf, trn->enemy_count, trn->bk_name, trn->pic_file,
           trn->tournament_id, trn->registration_fee);
    return 0;
}

static int load_base(sd_tournament_file *trn, const char *resource_dir, const char *fname) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", resource_dir, fname);
    path in;
    path_from_c(&in, buf);
    sd_tournament_create(trn);
    int ret = sd_tournament_load(trn, &in);
    if(ret != SD_SUCCESS) {
        fprintf(stderr, "FAILED to load template %s: %s\n", buf, sd_get_error(ret));
        return 1;
    }
    printf("loaded template %s: %u enemies, bk=%s pic=%s id=%d fee=%d\n", buf, trn->enemy_count, trn->bk_name,
           trn->pic_file, trn->tournament_id, trn->registration_fee);
    return 0;
}

// ===========================================================================
// TOURNAMENT 1 -- "Nightshade Concord"
// ===========================================================================
static int gen_nightshade(const char *resource_dir, const char *out_dir, const char *art_dir) {
    sd_tournament_file trn;
    if(load_base(&trn, resource_dir, "NORTH_AM.TRN"))
        return 1;

    // Keep NORTH_AM.BK / NORTH_AM.PIC from the template (valid binaries).
    trn.tournament_id = 11; // not 4 (4 = World Championship special-case)
    // Post-World-Championship content: fee/value sit just above WORLD (10000/58000).
    trn.registration_fee = 15000;
    trn.assumed_initial_value = 70000;
    trn.winnings_multiplier = 1.5f;

    // Concord enforcers. Stats are tuned to the World Championship envelope
    // (the hardest original enemies cap arm_power ~6 and difficulty 0-2), so the
    // pack is challenging for a post-World pilot but never one-hit-kills.
    // rank 1 is the final/boss in OMF's scheme; higher rank = earlier/weaker.
    static const enemy_spec roster[] = {
        {"Cossette", PILOT_COSSETTE, HAR_KATANA, 0, 14, 14, 12, 2, 2, 4, 4, 3, 2, 9, 0, 0,
         "You carry the blood of the one we buried, ~1. Walk away, or share that grave."},
        {"Milano", PILOT_MILANO, HAR_JAGUAR, 0, 15, 18, 12, 2, 2, 5, 5, 3, 2, 8, 0, 0,
         "Fast, like your kin at the end. We let the swift ones run -- for a moment, ~1."},
        {"Jean-Paul", PILOT_JEANPAUL, HAR_ELECTRA, 0, 17, 11, 14, 3, 3, 4, 4, 4, 3, 7, 0, 0,
         "The Concord collects what it wants. Your blood refused us. Now you kneel, ~1."},
        {"Christian", PILOT_CHRISTIAN, HAR_THORN, 1, 18, 8, 16, 4, 3, 3, 3, 5, 4, 6, 0, 0,
         "Cut one of us down and two more rise. You are a single blade. We are the night."},
        {"Angel", PILOT_ANGEL, HAR_PYROS, 1, 18, 12, 15, 4, 4, 4, 4, 5, 4, 5, 0, 0,
         "I wept at the staged funeral, ~1. Vengeance is a fire -- let me show you how it burns."},
        {"Ibrahim", PILOT_IBRAHIM, HAR_GARGOYLE, 1, 20, 6, 20, 5, 5, 3, 3, 6, 6, 4, 0, 0,
         "I am the wall before the truth. None pass the Mountain. None reach Vance."},
        {"Shirro", PILOT_SHIRRO, HAR_SHREDDER, 2, 22, 8, 18, 5, 5, 4, 4, 5, 5, 3, 0, 0,
         "You found the Concord's door. Behind it waits the Hand of Vance. It only closes."},
        {"Raven", PILOT_RAVEN, HAR_SHADOW, 2, 22, 16, 18, 5, 5, 6, 6, 5, 5, 2, 0, 0,
         "I am the whisper that lured your blood into the dark. Now I whisper your end, ~1."},
        {"Vance", PILOT_RAVEN, HAR_NOVA, 2, 26, 16, 22, 6, 6, 6, 6, 6, 6, 1, 1, 2,
         "I am Vance. I built the Concord on your family's bones. Come and inherit their grave, ~1."},
    };
    build_roster(&trn, roster, (int)(sizeof(roster) / sizeof(roster[0])));

    finalize_locale(&trn, "Nightshade Concord",
                    "{WIDTH 276}{CENTER 160}{VMOVE 72}{COLOR 6}Your blood was murdered by the Nightshade Concord, a "
                    "fight-cult ruled by the masked Vance. A trial for World Champions -- climb his enforcers and "
                    "claim your reckoning. Entry: 15,000cr.");

    // Generated title banner (see expansion-art/). Falls back to the template
    // logo if the art is missing.
    if(art_dir) {
        char logo[512];
        snprintf(logo, sizeof(logo), "%s/nightshade_logo.lgo", art_dir);
        set_logo_from_lgo(&trn, logo, 10, 4);
    }

    // Victory cutscene -- second person and gender-neutral; ~1 = player name.
    // Pages are kept short so they fit the cutscene text area.
    static const char *const story[] = {
        "Vance's NOVA folds at the knees, venting coolant like a dying breath. The crowd that paid to watch you fall "
        "goes silent.",
        "You walk to the cockpit. The mask cracks. Beneath it is only a tired figure wearing your blood's old squadron "
        "pin.",
        "\"We flew together,\" Vance rasps. \"Your kin wanted to expose the fixers. I wanted to own them. One of us had "
        "to fall first.\"",
        "You do not strike. You take the pin. \"They fell forward,\" you say. \"You only fell.\"",
        "By dawn you have broadcast the Concord's ledgers to every network -- the bribes, the vanished pilots, the "
        "buried names.",
        "But the ledger names a patron above Vance. A signature half-remembered from old letters. A debt unpaid since "
        "2097.",
        "You seal the pin into your own cockpit. The revenge is finished. The reckoning has only begun, ~1.",
    };
    set_story(&trn, story, (int)(sizeof(story) / sizeof(story[0])));

    int rc = save_trn(&trn, out_dir, "KYRA.TRN");
    sd_tournament_free(&trn);
    return rc;
}

// ===========================================================================
// TOURNAMENT 2 -- "Iron Reckoning" (same characters, different mechs)
// ===========================================================================
static int gen_reckoning(const char *resource_dir, const char *out_dir, const char *art_dir) {
    sd_tournament_file trn;
    if(load_base(&trn, resource_dir, "WAR.TRN"))
        return 1;

    trn.tournament_id = 12;
    // The hardest gauntlet -- sits at the top of the progression.
    trn.registration_fee = 25000;
    trn.assumed_initial_value = 90000;
    trn.winnings_multiplier = 1.8f;

    // Same Concord names -- new, deadlier prototype mechs. Stats are a notch
    // above Nightshade but still capped within the World Championship envelope
    // (arm_power <= 6, boss <= 7; difficulty <= 2) so it stays beatable.
    static const enemy_spec roster[] = {
        {"Milano", PILOT_MILANO, HAR_CHRONOS, 1, 16, 20, 13, 3, 3, 6, 6, 4, 3, 9, 0, 0,
         "You scattered us, ~1. The patron rebuilt us in steel you've never seen. Too late now."},
        {"Cossette", PILOT_COSSETTE, HAR_FLAIL, 1, 17, 12, 14, 4, 4, 4, 4, 4, 4, 8, 0, 0,
         "I mourned the Concord. Then the patron gave me a reason -- and a heavier flail."},
        {"Jean-Paul", PILOT_JEANPAUL, HAR_PYROS, 1, 18, 11, 15, 4, 4, 5, 5, 5, 4, 7, 0, 0,
         "Names are cheap, ~1. The patron deals in legacies older than your blood's grave."},
        {"Angel", PILOT_ANGEL, HAR_KATANA, 1, 18, 14, 15, 5, 4, 5, 5, 5, 4, 6, 0, 0,
         "Last time I burned. This time I cut. I begged you to walk away, ~1. Now I carve."},
        {"Christian", PILOT_CHRISTIAN, HAR_GARGOYLE, 2, 20, 6, 18, 5, 4, 3, 3, 6, 6, 5, 0, 0,
         "The wall got taller. The patron forged me from war-iron. Break yourself on me, ~1."},
        {"Shirro", PILOT_SHIRRO, HAR_THORN, 2, 22, 8, 18, 5, 5, 4, 4, 6, 5, 4, 0, 0,
         "Vance ruled shadows. The patron is a god of the old war. You woke something, ~1."},
        {"Raven", PILOT_RAVEN, HAR_NOVA, 2, 24, 16, 18, 6, 5, 6, 6, 6, 6, 3, 0, 0,
         "I feared for Vance. I am certain for the patron. Your blood's name is in his ledger twice."},
        {"Vance", PILOT_RAVEN, HAR_SHADOW, 2, 24, 18, 20, 6, 6, 6, 6, 6, 6, 2, 0, 0,
         "I lived, ~1. The patron stitched me from the wreck you made. Win, and free us both."},
        {"Kreissack", PILOT_RAVEN, HAR_NOVA, 2, 28, 16, 24, 7, 6, 6, 6, 7, 7, 1, 1, 2,
         "Children always hunt the hand behind the knife. I am that hand. I made your blood a legend, "
         "then a corpse, ~1."},
    };
    build_roster(&trn, roster, (int)(sizeof(roster) / sizeof(roster[0])));

    finalize_locale(&trn, "Iron Reckoning",
                    "{WIDTH 276}{CENTER 160}{VMOVE 72}{COLOR 6}The Concord's patron survives -- a financier in a green "
                    "machine. The survivors return in deadlier mechs. The circuit's hardest gauntlet, for proven "
                    "champions. Entry: 25,000cr.");

    if(art_dir) {
        char logo[512];
        snprintf(logo, sizeof(logo), "%s/reckoning_logo.lgo", art_dir);
        set_logo_from_lgo(&trn, logo, 10, 4);
    }

    static const char *const story[] = {
        "The ancient NOVA -- repainted Concord green -- finally stops moving. Kreissack's reactor goes dark for the "
        "last time.",
        "On every feed, the patron's web of fixers and silent partners scatters like roaches under sudden light.",
        "Vance pulls you from your smoking cockpit instead of finishing you. \"He's gone,\" he says. \"I belong to no "
        "one now.\"",
        "You study the one who killed your blood, and the prisoner the patron made of him, and can no longer tell them "
        "apart.",
        "\"Rebuild it clean,\" you tell him, and press your blood's pin into his hand. \"A circuit with no shadows.\"",
        "You walk out of the arena and do not look back. The reckoning is paid. The iron is quiet.",
        "Somewhere a child watches and learns the name ~1 -- the one who fought for the dead, then let the living go.",
    };
    set_story(&trn, story, (int)(sizeof(story) / sizeof(story[0])));

    int rc = save_trn(&trn, out_dir, "RECKON.TRN");
    sd_tournament_free(&trn);
    return rc;
}

int main(int argc, char *argv[]) {
    if(argc < 3) {
        fprintf(stderr, "Usage: %s <resource_dir> <output_dir>\n", argv[0]);
        return 1;
    }
    const char *resource_dir = argv[1];
    const char *out_dir = argv[2];
    const char *art_dir = (argc > 3) ? argv[3] : NULL;
    printf("OMF 2097 \"Kyra\" Expansion Pack generator\n");
    int rc = 0;
    rc |= gen_nightshade(resource_dir, out_dir, art_dir);
    rc |= gen_reckoning(resource_dir, out_dir, art_dir);
    if(rc == 0) {
        printf("Done. Two tournaments generated.\n");
    }
    return rc;
}
