/** @file mkpilot.c
 * @brief Create a ready-to-play test pilot savegame (.CHR) for the expansion.
 *
 * Builds a fresh, fully-upgraded Jaguar champion from scratch -- pulling a real
 * portrait sprite + palette directly out of PLAYERS.PIC (a pure file load, so no
 * engine subsystems need to be initialized). The pilot has enough credits to
 * enter the new post-World-Championship tournaments (Nightshade Concord
 * 15,000cr, Iron Reckoning 25,000cr) and is left "between tournaments" so it can
 * select either from the Mechlab.
 *
 * Usage: mkpilot <PLAYERS.PIC path> <output.CHR> <PILOT NAME> [photo_id]
 * @license MIT
 */

#include "formats/chr.h"
#include "formats/error.h"
#include "formats/pic.h"
#include "formats/pilot.h"
#include "formats/sprite.h"
#include "game/common_defines.h"
#include "utils/allocator.h"
#include "utils/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if(argc < 4) {
        fprintf(stderr, "Usage: %s <PLAYERS.PIC path> <output.CHR> <PILOT NAME> [photo_id]\n", argv[0]);
        return 1;
    }
    const char *pic_path = argv[1];
    const char *outp = argv[2];
    const char *name = argv[3];
    int photo_id = (argc > 4) ? atoi(argv[4]) : 0;

    // Pull a real portrait + palette out of PLAYERS.PIC.
    path pp;
    path_from_c(&pp, pic_path);
    sd_pic_file pic;
    sd_pic_create(&pic);
    if(sd_pic_load(&pic, &pp) != SD_SUCCESS) {
        fprintf(stderr, "FAILED to load %s\n", pic_path);
        return 1;
    }
    if(photo_id < 0 || photo_id >= pic.photo_count) {
        photo_id = 0;
    }
    const sd_pic_photo *photo = sd_pic_get(&pic, photo_id);
    if(!photo || !photo->sprite) {
        fprintf(stderr, "PLAYERS.PIC has no usable photo %d\n", photo_id);
        sd_pic_free(&pic);
        return 1;
    }

    sd_chr_file chr;
    sd_chr_create(&chr);
    sd_pilot *p = &chr.pilot;
    sd_pilot_create(p);

    // Identity
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->pilot_id = PILOT_CRYSTAL;
    p->photo_id = (uint16_t)photo_id;
    p->sex = photo->sex;

    // Fully-upgraded Jaguar (HAR stat upgrades are 0..9 in-game).
    p->har_id = HAR_JAGUAR;
    p->arm_power = 9;
    p->leg_power = 9;
    p->arm_speed = 9;
    p->leg_speed = 9;
    p->armor = 9;
    p->stun_resistance = 9;
    p->enhancements[HAR_JAGUAR] = (char)0xFF; // every Jaguar enhancement bought

    // Veteran pilot attributes (1..25) and HAR colours (0..15 altpals).
    p->power = 20;
    p->agility = 20;
    p->endurance = 20;
    p->color_1 = 8;
    p->color_2 = 11;
    p->color_3 = 5;
    p->offense = 120;
    p->defense = 120;

    // Champion standing + a war chest that clears both entry fees with room to
    // re-arm (Nightshade 15,000 + Iron Reckoning 25,000).
    p->money = 200000;
    p->rank = 1;
    p->wins = 60;
    p->losses = 3;

    // Between tournaments: no current tournament, no enemy roster.
    p->enemies_inc_unranked = 0;
    p->enemies_ex_unranked = 0;

    // Portrait sprite + palette, copied from PLAYERS.PIC.
    chr.photo = omf_calloc(1, sizeof(sd_sprite));
    sd_sprite_create(chr.photo);
    sd_sprite_copy(chr.photo, photo->sprite);
    p->photo = chr.photo;
    memcpy(&chr.pal, &photo->pal, sizeof(vga_palette));
    chr.winnings_multiplier = 1.0f;

    path out;
    path_from_c(&out, outp);
    int rc = sd_chr_save(&chr, &out);
    if(rc != SD_SUCCESS) {
        fprintf(stderr, "FAILED to save CHR %s: %s\n", outp, sd_get_error(rc));
        sd_pic_free(&pic);
        return 1;
    }
    printf("Wrote test pilot '%s' -> %s (Jaguar maxed, %d cr, rank %d, photo %d)\n", p->name, outp, p->money, p->rank,
           photo_id);
    sd_pic_free(&pic);
    return 0;
}
