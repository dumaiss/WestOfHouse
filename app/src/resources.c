#include "kos.h"
#include "kos/kres.h"
#include "kos/kres_konaos.h"
#include "kos/kimg.h"
#include "kos/gfx.h"
#include "resources.h"
#include "resource_names.h"
#include "consumer.h"

static kres_container archive;
static kres_resource story, image, music;
/* The music streams through a container of its own: its own file handle,
 * so its reads never move the story's and scenes' file position. */
static kres_container music_archive;
static const char *archive_path;
static whs_table_t hotspots;
/* Ordinary resident storage remains visible while SDK far calls change the
 * aperture. The generated bound is the largest declared WHT1 payload. */
static uint8_t hotspot_bytes[WOH_HOTSPOT_BYTES];

/* KRES permits short logical reads. Keep every borrowed buffer resident and
 * complete the requested span before handing it to the decoder or renderer. */
static int read_full(kres_resource *resource, void *buffer, uint16_t size)
{
    uint16_t done = 0;
    int result;
    while (done < size) {
        result = kres_read(resource, (uint8_t *)buffer + done, size - done);
        if (result < 0) return result;
        if (!result) return -KOS_EIO;
        done += (uint16_t)result;
    }
    return (int)done;
}

int resources_open(const char *path)
{
    kres_install(&kres_io_konaos_read, 0);
    archive_path = path;
    return kres_open(&archive, path, KRES_OPEN_READ);
}

void resources_close(void)
{
    (void)kres_rclose(&image);
    (void)kres_rclose(&story);
    (void)kres_rclose(&music);
    if (music_archive.opened) (void)kres_close(&music_archive);
    if (archive.opened) (void)kres_close(&archive);
    memset(&hotspots, 0, sizeof(hotspots));
}

int resources_story(kos_reader_t *reader)
{
    int result = kres_get(&archive, WOH_STORY_RESOURCE, &story, KRES_OPEN_READ);
    return result < 0 ? result : kos_reader_from_kres(reader, &story);
}

int resources_music(kos_reader_t *reader)
{
#ifdef WOH_MUSIC_RESOURCE
    int result = kres_open(&music_archive, archive_path, KRES_OPEN_READ);
    if (result < 0) return result;
    result = kres_get(&music_archive, WOH_MUSIC_RESOURCE, &music, KRES_OPEN_READ);
    /* One file read per staging refill, straight into the player's
     * resident buffer. */
    return result < 0 ? result : kos_reader_from_kres_direct(reader, &music);
#else
    (void)reader;
    return -1;
#endif
}

int resources_scene(uint16_t room, uint16_t state)
{
    uint8_t i;
    uint8_t name_length;
    char hotspot_name[32];
    struct kimg_header header;
    kres_resource table;
    memset(&hotspots, 0, sizeof(hotspots));
    (void)kres_rclose(&image);
    for (i = 0; i < WOH_SCENE_COUNT; ++i) {
        if (scene_keys[i].room != room || scene_keys[i].state != state) continue;
        if (kres_get(&archive, scene_keys[i].image, &image, KRES_OPEN_READ) < 0 ||
            read_full(&image, &header, KIMG_HEADER_SIZE) != KIMG_HEADER_SIZE ||
            memcmp(header.magic, "KIMG", 4u) || header.version != 1u ||
            header.mode != GFX_MODE_256x212_YJK_YAE || header.flags || header.palette_count ||
            header.width != 256u || header.height != 159u || header.payload_size != 40704ul ||
            image.used_bytes != 40720ul) return -1;
        /* A missing or malformed optional interaction table disables hotspots;
         * the image and normal keyboard/parser path remain available. */
        /* The manifest enforces matching .img/.wht base names. Keep only the
         * image name in resident constants and form its paired table on stack. */
        name_length = (uint8_t)strlen(scene_keys[i].image);
        if (name_length < 4u || name_length >= sizeof(hotspot_name)) return -1;
        memcpy(hotspot_name, scene_keys[i].image, name_length + 1u);
        memcpy(hotspot_name + name_length - 3u, "wht", 3u);
        if (kres_get(&archive, hotspot_name, &table, KRES_OPEN_READ) == 0) {
            if (table.used_bytes <= sizeof(hotspot_bytes) &&
                read_full(&table, hotspot_bytes, (uint16_t)table.used_bytes) == (int)table.used_bytes &&
                whs_open(&hotspots, hotspot_bytes, (uint16_t)table.used_bytes) &&
                !whs_matches(&hotspots, room, state))
                memset(&hotspots, 0, sizeof(hotspots));
            (void)kres_rclose(&table);
        }
        return 1;
    }
    return 0;
}

int resources_title(void)
{
    struct kimg_header header;
    if (kres_get(&archive, WOH_TITLE_RESOURCE, &image, KRES_OPEN_READ) < 0 ||
        read_full(&image, &header, KIMG_HEADER_SIZE) != KIMG_HEADER_SIZE ||
        memcmp(header.magic, "KIMG", 4u) || header.version != 1u ||
        header.mode != GFX_MODE_256x212_YJK || header.flags || header.palette_count ||
        header.width != 256u || header.height != 212u || header.payload_size != 54272ul ||
        image.used_bytes != 54288ul) return -1;
    return 0;
}

int resources_pixels(void *buffer, uint16_t size)
{
    return read_full(&image, buffer, size);
}

int resources_menu(menu_t *menu, uint16_t x, uint16_t y)
{
    whs_region_t region;
    uint8_t i;
    if (!whs_hit(&hotspots, x, y, &region)) return 0;
    menu_open(menu, x, y, 0, menu->action_labels[0], menu->action_labels[1], menu->action_labels[2]);
    menu->count = region.action_count;
    for (i = 0; i < region.action_count; ++i) {
        if (!whs_action(&hotspots, &region, i, menu->action_labels[i], 9u, menu->commands[i], 81u)) {
            menu->open = 0; return 0;
        }
    }
    return 1;
}
