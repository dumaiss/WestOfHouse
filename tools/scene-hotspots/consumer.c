#include "consumer.h"

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int text(const uint8_t *p, uint8_t size, uint8_t command)
{
    uint8_t i, previous = ' ';
    for (i = 0; i < size; ++i) {
        if (p[i] < 32u || p[i] > 126u) return 0;
        if (command && !((p[i] >= 'a' && p[i] <= 'z') || p[i] == ' ')) return 0;
        if (command && p[i] == ' ' && previous == ' ') return 0;
        previous = p[i];
    }
    return !command || previous != ' ';
}

/* Parse and validate a whole variable-length entry; no packed C structures. */
static uint16_t entry(const whs_table_t *t, uint16_t at, whs_region_t *out)
{
    uint8_t i, j, k, a, b;
    const uint8_t *prior[3];
    uint8_t lengths[3];
    const uint8_t *p = t->bytes;
    if (at > t->size || t->size - at < 8) return 0;
    out->id = p[at]; out->x = p[at+1u]; out->y = p[at+2u];
    out->width = word(p+at+3u); out->height = p[at+5u];
    out->action_count = p[at+6u]; out->label_length = p[at+7u];
    at += 8u;
    if (!out->id || !out->width || out->width > 256u || !out->height ||
        (uint16_t)out->x + out->width > 256u ||
        (uint16_t)out->y + out->height > 159u ||
        !out->action_count || out->action_count > 3u ||
        !out->label_length || out->label_length > 31u ||
        out->label_length > t->size-at) return 0;
    out->label = p+at;
    if (!text(p+at,out->label_length,0)) return 0;
    at += out->label_length; out->action_offset = at;
    for (i = 0; i < out->action_count; ++i) {
        if (t->size-at < 2) return 0;
        a=p[at++]; b=p[at++];
        if (!a || a>8u || !b || b>80u || (uint16_t)a+b>t->size-at ||
            !text(p+at,a,0) || !text(p+at+a,b,1)) return 0;
        prior[i]=p+at+a; lengths[i]=b;
        for (j=0; j<i; ++j) if (lengths[j]==b) {
            for (k=0; k<b && prior[j][k]==prior[i][k]; ++k) {}
            if (k==b) return 0;
        }
        at += (uint16_t)a+b;
    }
    return at;
}

int whs_open(whs_table_t *out, const uint8_t *bytes, uint16_t size)
{
    whs_table_t table;
    whs_region_t region;
    uint8_t i;
    uint16_t at=48u;
    if (!out) return 0;
    out->bytes=0; out->size=out->room=out->state=0; out->count=out->unbound=0;
    if (!bytes || size<48u || size>WHS_MAX_BYTES || bytes[0]!='W' ||
        bytes[1]!='H' || bytes[2]!='T' || bytes[3]!='1' || bytes[4]>1u ||
        bytes[5]>12u || !word(bytes+6) || word(bytes+10)!=256u ||
        word(bytes+12)!=159u || word(bytes+14)!=size-16u) return 0;
    table.bytes=bytes; table.size=size; table.room=word(bytes+6);
    table.state=word(bytes+8); table.count=bytes[5]; table.unbound=bytes[4];
    if (table.unbound ? table.state!=65535u : (!table.state || table.state==65535u)) return 0;
    for (i=0; i<table.count; ++i) {
        at=entry(&table,at,&region);
        if (!at || region.id!=i+1u) return 0;
    }
    if (at!=size) return 0;
    *out=table; return 1;
}

int whs_matches(const whs_table_t *table, uint16_t room, uint16_t state)
{
    return table && table->bytes && !table->unbound && table->room==room && table->state==state;
}

int whs_region(const whs_table_t *table, uint8_t index, whs_region_t *out)
{
    uint16_t at=48u;
    uint8_t i;
    if (!table || !table->bytes || !out || index>=table->count) return 0;
    for (i=0; i<=index; ++i) {
        at=entry(table,at,out);
        if (!at) return 0;
    }
    return 1;
}

int whs_hit(const whs_table_t *table, uint16_t x, uint16_t y, whs_region_t *out)
{
    whs_region_t region;
    uint8_t i, found=0;
    uint16_t area, best=65535u;
    if (!table || table->unbound || !out || x>=256u || y>=159u) return 0;
    for (i=0; i<table->count; ++i) {
        if (!whs_region(table,i,&region)) return 0;
        if (x>=region.x && x<(uint16_t)region.x+region.width &&
            y>=region.y && y<(uint16_t)region.y+region.height) {
            /* 256*159 fits uint16_t on both host and SDCC. */
            area=region.width*(uint16_t)region.height;
            if (area<best) { *out=region; best=area; found=1; }
        }
    }
    return found;
}

int whs_action(const whs_table_t *table, const whs_region_t *region, uint8_t index,
               char *label, uint8_t label_capacity, char *command, uint8_t command_capacity)
{
    uint16_t at;
    uint8_t i,j,a,b;
    const uint8_t *p;
    if (!table || !table->bytes || !region || !label || !command || index>=region->action_count) return 0;
    p=table->bytes; at=region->action_offset;
    for (i=0; i<=index; ++i) {
        if (at>table->size || table->size-at<2) return 0;
        a=p[at++]; b=p[at++];
        if (!a || a>8u || !b || b>80u || (uint16_t)a+b>table->size-at) return 0;
        if (i==index) {
            if (a>=label_capacity || b>=command_capacity) return 0;
            for (j=0; j<a; ++j) label[j]=(char)p[at+j];
            label[a]=0; at+=a;
            for (j=0; j<b; ++j) command[j]=(char)p[at+j];
            command[b]=0; return 1;
        }
        at+=(uint16_t)a+b;
    }
    return 0;
}
