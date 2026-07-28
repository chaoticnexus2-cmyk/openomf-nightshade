/** @file mkportrait.c
 * @brief Inject a brand-new original antagonist portrait (Vance) into a PIC.
 *
 * OMF tournament enemies do not carry an embedded portrait on disk -- each enemy
 * references a face by photo_id into the tournament's shared PIC file. So adding
 * a NEW, non-reused face for the masked boss "Vance" means appending a new photo
 * to a copy of WORLD.PIC and pointing the enemy's photo_id at it.
 *
 * This tool:
 *   1. Loads WORLD.PIC (all the real classic-pilot faces).
 *   2. Loads the engine-ready indexed portrait produced by
 *      expansion-art/quantize_portrait.py (a raw ".vph": <u16 w><u16 h><48*3
 *      palette><w*h indices>, index 0 = transparent).
 *   3. Encodes it into an OMF sprite (sd_sprite_vga_encode) and appends it as a
 *      new sd_pic_photo at index VANCE_PHOTO_ID, padding any gap with clones of a
 *      real face so intermediate indices stay valid.
 *   4. Saves the result as NIGHTSHD.PIC using the engine's own sd_pic_save().
 *
 * Output is byte-correct because it uses OpenOMF's own writers.
 *
 * Usage: mkportrait <WORLD.PIC path> <vance.vph path> <output.PIC path>
 * @license MIT
 */

#include "formats/error.h"
#include "formats/pic.h"
#include "formats/sprite.h"
#include "formats/vga_image.h"
#include "game/common_defines.h"
#include "utils/allocator.h"
#include "utils/path.h"
#include "vance.h"
#include "video/vga_palette.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Load the raw indexed portrait (.vph) written by quantize_portrait.py.
// Format: <u16 w><u16 h><48*3 palette RGB><w*h index bytes>. Index 0 is the
// transparent background; opaque pixels use indices 1..47.
static int load_vph(const char *path, sd_vga_image *img, vga_palette *pal) {
    FILE *file = fopen(path, "rb");
    if(!file) {
        fprintf(stderr, "FAILED to open portrait %s\n", path);
        return 1;
    }
    uint8_t header[4];
    uint8_t palette_bytes[48 * 3];
    if(fread(header, 1, 4, file) != 4 || fread(palette_bytes, 1, sizeof(palette_bytes), file) != sizeof(palette_bytes)) {
        fprintf(stderr, "FAILED to read portrait header/palette in %s\n", path);
        fclose(file);
        return 1;
    }
    int width = header[0] | (header[1] << 8);
    int height = header[2] | (header[3] << 8);
    size_t pixel_count = (size_t)width * (size_t)height;

    if(sd_vga_image_create(img, width, height) != SD_SUCCESS) {
        fprintf(stderr, "FAILED to allocate portrait image %dx%d\n", width, height);
        fclose(file);
        return 1;
    }
    if(fread(img->data, 1, pixel_count, file) != pixel_count) {
        fprintf(stderr, "FAILED to read portrait pixels (%zu) in %s\n", pixel_count, path);
        fclose(file);
        sd_vga_image_free(img);
        return 1;
    }
    fclose(file);

    // Portrait palette occupies indices 0..47 (PIC photos store a 48-entry range).
    vga_palette_init(pal);
    for(int i = 0; i < 48; i++) {
        pal->colors[i].r = palette_bytes[i * 3 + 0];
        pal->colors[i].g = palette_bytes[i * 3 + 1];
        pal->colors[i].b = palette_bytes[i * 3 + 2];
    }
    printf("loaded portrait %s (%dx%d)\n", path, width, height);
    return 0;
}

int main(int argc, char *argv[]) {
    if(argc < 4) {
        fprintf(stderr, "Usage: %s <WORLD.PIC path> <vance.vph path> <output.PIC path>\n", argv[0]);
        return 1;
    }
    const char *world_path = argv[1];
    const char *vph_path = argv[2];
    const char *out_path = argv[3];

    // 1. Load WORLD.PIC (all the real faces).
    path world_in;
    path_from_c(&world_in, world_path);
    sd_pic_file pic;
    sd_pic_create(&pic);
    if(sd_pic_load(&pic, &world_in) != SD_SUCCESS) {
        fprintf(stderr, "FAILED to load %s\n", world_path);
        return 1;
    }
    printf("loaded %s: %d photos\n", world_path, pic.photo_count);

    if(pic.photo_count <= 0 || pic.photo_count >= MAX_PIC_PHOTOS) {
        fprintf(stderr, "unexpected photo count %d in %s\n", pic.photo_count, world_path);
        sd_pic_free(&pic);
        return 1;
    }
    if(VANCE_PHOTO_ID >= MAX_PIC_PHOTOS) {
        fprintf(stderr, "VANCE_PHOTO_ID %d exceeds PIC capacity\n", VANCE_PHOTO_ID);
        sd_pic_free(&pic);
        return 1;
    }
    if(pic.photo_count > VANCE_PHOTO_ID) {
        fprintf(stderr, "WORLD.PIC already has %d photos (>= VANCE_PHOTO_ID %d); pick a higher slot\n", pic.photo_count,
                VANCE_PHOTO_ID);
        sd_pic_free(&pic);
        return 1;
    }

    // 2. Load the engine-ready Vance portrait.
    sd_vga_image portrait;
    vga_palette portrait_pal;
    if(load_vph(vph_path, &portrait, &portrait_pal)) {
        sd_pic_free(&pic);
        return 1;
    }

    // 3. Pad any gap between the existing faces and VANCE_PHOTO_ID with clones of
    //    photo 0, so every intermediate index remains a valid (drawable) face.
    const sd_pic_photo *filler = sd_pic_get(&pic, 0);
    for(int i = pic.photo_count; i < VANCE_PHOTO_ID; i++) {
        pic.photos[i] = omf_calloc(1, sizeof(sd_pic_photo));
        pic.photos[i]->is_player = filler->is_player;
        pic.photos[i]->sex = filler->sex;
        pic.photos[i]->unk_flag = filler->unk_flag;
        memcpy(&pic.photos[i]->pal, &filler->pal, sizeof(vga_palette));
        pic.photos[i]->sprite = omf_calloc(1, sizeof(sd_sprite));
        sd_sprite_create(pic.photos[i]->sprite);
        sd_sprite_copy(pic.photos[i]->sprite, filler->sprite);
    }

    // 4. Build the new Vance photo at VANCE_PHOTO_ID.
    sd_pic_photo *vance = omf_calloc(1, sizeof(sd_pic_photo));
    vance->is_player = 0; // enemy portrait
    vance->sex = PILOT_SEX_MALE;
    vance->unk_flag = filler->unk_flag; // match the "has image data" flag of real faces
    memcpy(&vance->pal, &portrait_pal, sizeof(vga_palette));
    vance->sprite = omf_calloc(1, sizeof(sd_sprite));
    sd_sprite_create(vance->sprite);
    if(sd_sprite_vga_encode(vance->sprite, &portrait) != SD_SUCCESS) {
        fprintf(stderr, "FAILED to encode Vance portrait sprite\n");
        sd_sprite_free(vance->sprite);
        omf_free(vance->sprite);
        omf_free(vance);
        sd_vga_image_free(&portrait);
        sd_pic_free(&pic);
        return 1;
    }
    // Centre the portrait roughly where the classic faces sit.
    vance->sprite->pos_x = 0;
    vance->sprite->pos_y = 0;

    pic.photos[VANCE_PHOTO_ID] = vance;
    pic.photo_count = VANCE_PHOTO_ID + 1;
    sd_vga_image_free(&portrait);

    // 5. Save the extended PIC.
    path out;
    path_from_c(&out, out_path);
    int rc = sd_pic_save(&pic, &out);
    if(rc != SD_SUCCESS) {
        fprintf(stderr, "FAILED to save %s: %s\n", out_path, sd_get_error(rc));
        sd_pic_free(&pic);
        return 1;
    }
    printf("wrote %s (%d photos; Vance face at index %d)\n", out_path, pic.photo_count, VANCE_PHOTO_ID);
    sd_pic_free(&pic);
    return 0;
}
