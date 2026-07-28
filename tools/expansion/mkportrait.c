/** @file mkportrait.c
 * @brief Inject the original Nightshade Concord antagonist faces into a PIC.
 *
 * OMF tournament enemies do not carry an embedded portrait on disk -- each enemy
 * references a face by photo_id into the tournament's shared PIC file. So adding
 * a NEW, non-reused cast of faces means appending new photos to a copy of
 * WORLD.PIC and pointing each enemy's photo_id at the right slot.
 *
 * This tool:
 *   1. Loads WORLD.PIC (all the real classic-pilot faces).
 *   2. For each cast member (enum concord_face in vance.h), loads the
 *      engine-ready indexed portrait produced by quantize_portrait.py
 *      (raw ".vph": <u16 w><u16 h><48*3 palette><w*h indices>, index 0 = clear).
 *   3. Encodes each into an OMF sprite (sd_sprite_vga_encode) and stores it at
 *      its enum face index, padding any gap with clones of a real face so
 *      intermediate indices stay valid.
 *   4. Saves the result as NIGHTSHD.PIC using the engine's own sd_pic_save().
 *
 * Output is byte-correct because it uses OpenOMF's own writers.
 *
 * Usage: mkportrait <WORLD.PIC path> <portraits_dir> <output.PIC path>
 *   (a legacy 3-arg form with a single .vph file is still accepted and injected
 *    at FACE_CARDINAL for backward compatibility.)
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
    return 0;
}

// Build a single sd_pic_photo from a .vph file. Returns NULL on failure.
static sd_pic_photo *build_photo(const char *vph_path, const sd_pic_photo *filler) {
    sd_vga_image portrait;
    vga_palette portrait_pal;
    if(load_vph(vph_path, &portrait, &portrait_pal)) {
        return NULL;
    }
    sd_pic_photo *photo = omf_calloc(1, sizeof(sd_pic_photo));
    photo->is_player = 0; // enemy portrait
    photo->sex = PILOT_SEX_MALE;
    photo->unk_flag = filler->unk_flag; // match the "has image data" flag of real faces
    memcpy(&photo->pal, &portrait_pal, sizeof(vga_palette));
    photo->sprite = omf_calloc(1, sizeof(sd_sprite));
    sd_sprite_create(photo->sprite);
    if(sd_sprite_vga_encode(photo->sprite, &portrait) != SD_SUCCESS) {
        fprintf(stderr, "FAILED to encode portrait sprite from %s\n", vph_path);
        sd_sprite_free(photo->sprite);
        omf_free(photo->sprite);
        omf_free(photo);
        sd_vga_image_free(&portrait);
        return NULL;
    }
    photo->sprite->pos_x = 0;
    photo->sprite->pos_y = 0;
    sd_vga_image_free(&portrait);
    return photo;
}

// Clone a filler face into slot i so intermediate indices stay valid/drawable.
static void set_filler(sd_pic_file *pic, int i, const sd_pic_photo *filler) {
    pic->photos[i] = omf_calloc(1, sizeof(sd_pic_photo));
    pic->photos[i]->is_player = filler->is_player;
    pic->photos[i]->sex = filler->sex;
    pic->photos[i]->unk_flag = filler->unk_flag;
    memcpy(&pic->photos[i]->pal, &filler->pal, sizeof(vga_palette));
    pic->photos[i]->sprite = omf_calloc(1, sizeof(sd_sprite));
    sd_sprite_create(pic->photos[i]->sprite);
    sd_sprite_copy(pic->photos[i]->sprite, filler->sprite);
}

int main(int argc, char *argv[]) {
    if(argc < 4) {
        fprintf(stderr, "Usage: %s <WORLD.PIC path> <portraits_dir> <output.PIC path>\n", argv[0]);
        return 1;
    }
    const char *world_path = argv[1];
    const char *portraits_arg = argv[2];
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

    if(pic.photo_count <= 0 || pic.photo_count > CONCORD_FACE_BASE) {
        fprintf(stderr, "unexpected photo count %d in %s (need <= %d)\n", pic.photo_count, world_path,
                CONCORD_FACE_BASE);
        sd_pic_free(&pic);
        return 1;
    }
    if(CONCORD_FACE_LAST >= MAX_PIC_PHOTOS) {
        fprintf(stderr, "cast last face %d exceeds PIC capacity %d\n", CONCORD_FACE_LAST, MAX_PIC_PHOTOS);
        sd_pic_free(&pic);
        return 1;
    }

    // 2. Pad the gap between the real faces and the cast base with a valid face.
    const sd_pic_photo *filler = sd_pic_get(&pic, 0);
    for(int i = pic.photo_count; i < CONCORD_FACE_BASE; i++) {
        set_filler(&pic, i, filler);
    }

    // 3. Inject each cast face at its enum index. Determine whether the caller
    //    gave us a portraits directory (batch) or a single legacy .vph file.
    const char *stems[] = CONCORD_FACE_STEMS;
    size_t len = strlen(portraits_arg);
    int single_vph = (len >= 4 && strcmp(portraits_arg + len - 4, ".vph") == 0);

    if(single_vph) {
        // Legacy: inject one portrait at FACE_CARDINAL, filler for the rest.
        sd_pic_photo *photo = build_photo(portraits_arg, filler);
        if(!photo) {
            sd_pic_free(&pic);
            return 1;
        }
        pic.photos[FACE_CARDINAL] = photo;
        for(int f = FACE_CARDINAL + 1; f <= CONCORD_FACE_LAST; f++) {
            set_filler(&pic, f, filler);
        }
        printf("injected single portrait at index %d\n", FACE_CARDINAL);
    } else {
        for(int idx = 0; idx < CONCORD_FACE_TOTAL; idx++) {
            char vph_path[1024];
            snprintf(vph_path, sizeof(vph_path), "%s/%s.vph", portraits_arg, stems[idx]);
            int face_index = CONCORD_FACE_BASE + idx;
            sd_pic_photo *photo = build_photo(vph_path, filler);
            if(!photo) {
                // Missing/failed face: fall back to a valid filler so indices stay sane.
                fprintf(stderr, "  (using filler for face %d '%s')\n", face_index, stems[idx]);
                set_filler(&pic, face_index, filler);
                continue;
            }
            pic.photos[face_index] = photo;
            printf("  injected '%s' -> index %d\n", stems[idx], face_index);
        }
    }

    pic.photo_count = CONCORD_FACE_LAST + 1;

    // 4. Save the extended PIC.
    path out;
    path_from_c(&out, out_path);
    int rc = sd_pic_save(&pic, &out);
    if(rc != SD_SUCCESS) {
        fprintf(stderr, "FAILED to save %s: %s\n", out_path, sd_get_error(rc));
        sd_pic_free(&pic);
        return 1;
    }
    printf("wrote %s (%d photos; Concord cast at %d..%d)\n", out_path, pic.photo_count, CONCORD_FACE_BASE,
           CONCORD_FACE_LAST);
    sd_pic_free(&pic);
    return 0;
}
