#include "kos.h"
#include "kos/kres.h"
#include "kos/kres_konaos.h"
#include "kos/kimg.h"
#include "kos/gfx.h"
#include "resources.h"
#include "resource_names.h"
#include "consumer.h"

static kres_container archive;
static kres_resource story, image;
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
    return kres_open(&archive, path, KRES_OPEN_READ);
}

void resources_close(void)
{
    (void)kres_rclose(&image);
    (void)kres_rclose(&story);
    if (archive.opened) (void)kres_close(&archive);
    memset(&hotspots, 0, sizeof(hotspots));
}

int resources_story(kos_reader_t *reader)
{
    int result = kres_get(&archive, WOH_STORY_RESOURCE, &story, KRES_OPEN_READ);
    return result < 0 ? result : kos_reader_from_kres(reader, &story);
}

int resources_scene(uint16_t room, uint16_t state)
{
    uint8_t i;
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
        if (kres_get(&archive, scene_keys[i].hotspots, &table, KRES_OPEN_READ) == 0) {
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
