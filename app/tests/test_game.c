/* Actual story and compact VM over mapped RAM. Models the aperture and
 * allocation/read failures, not OS timing, interrupts, or physical decoding. */
#include "kos.h"
#include "game.h"
#include "probe.h"
#include "zvm_internal.h"
#include <assert.h>
#include <stdint.h>
static uint8_t source[131070], original[131070];
static uint8_t pages[17][8192], aperture[8192], allocated[17];
static uint32_t source_size;
static unsigned int active, live, maps, source_calls;
static int file_open, missing, allocation_failure, map_failure, source_failure, staging_failure;
woh_probe_t woh_probe;
static panel_t panel;
extern game_t game;
static unsigned int byte_calls, view_calls;
static zvm_error_t counted_read(void *user, uint32_t at, uint8_t *out)
{ ++byte_calls; return mapped_story_read(user,at,out); }
static zvm_error_t counted_write(void *user, uint32_t at, uint8_t v)
{ ++byte_calls; return mapped_story_write(user,at,v); }
static uint8_t *counted_page(void *user, uint32_t at, uint8_t write)
{ ++view_calls; return mapped_story_page(user,at,write); }
static void pointer_hits(void)
{
 zvm_state_t *s=ZVM_STATE(&game.vm);
 mapped_story_t *m=&game.mapped;
 uint8_t value;
 unsigned int before=maps;
 s->read_byte=counted_read; s->write_byte=counted_write; s->page=counted_page;
 m->active=1; m->held=0; m->held_slot=255;
 byte_calls=view_calls=0;
 for (unsigned int i=0;i<100;++i) {
  assert(zvm_mem_fetch(s,64,&value)==ZVM_OK && value==original[64]);
  assert(zvm_mem_read(s,65,&value)==ZVM_OK && value==original[65]);
 }
 assert(byte_calls==0 && view_calls==2 && maps==before+1);
 /* Different instruction/data regions, including more than 256 switches. */
 for (unsigned int i=0;i<300;++i) {
  assert(zvm_mem_read(s,8192,&value)==ZVM_OK && value==original[8192]);
  assert(zvm_mem_fetch(s,64,&value)==ZVM_OK && value==original[64]);
 }
 assert(byte_calls==0 && maps==before+601);
 assert(zvm_mem_write(s,8192,0x5a)==ZVM_OK);
 assert(zvm_mem_read(s,8192,&value)==ZVM_OK && value==0x5a);
 assert(zvm_mem_write(s,8192,original[8192])==ZVM_OK);
 assert(zvm_mem_write(s,s->static_base,1)==ZVM_EREADONLY);
 assert(byte_calls==0);
 m->held_slot=255;
 aperture_close(m->previous);
 m->held=m->active=0;
 assert(!active && !mapped_story_page(m,64,0));
 /* Old mapped pointers must be rejected after restoring caller memory. */
 assert(zvm_mem_fetch(s,64,&value)==ZVM_OK && value==original[64]);
 assert(byte_calls==1 && !active);
 puts("mapped pointer hits: PASS (zero byte callbacks, reuse, 600 region switches, write-through, scope invalidation)");
}
int open(const char *path, unsigned char flags)
{ (void)path; (void)flags; if (missing) return -KOS_ENOENT; assert(!file_open); file_open=1; return 1; }
int close(unsigned int handle) { assert(handle==1 && file_open); file_open=0; return 0; }
int xalloc(unsigned int size, unsigned int flags)
{
 assert(size && size<=8192 && flags==KOS_XMEM_FLAG_MAPPABLE);
 if (allocation_failure) return -KOS_ENOSPC;
 for (unsigned int h=1;h<17;++h) if (!allocated[h]) { allocated[h]=1; ++live; return (int)h; }
 return -KOS_ENOSPC;
}
int xfree(unsigned int h)
{ assert(h && h<17 && allocated[h] && !active); allocated[h]=0; --live; return 0; }
int xwrite(unsigned int h, unsigned int offset, const void *data, unsigned int n)
{ assert(h && h<17 && allocated[h] && offset+n<=8192 && !active); memcpy(pages[h]+offset,data,n); return (int)n; }
int xmap(unsigned int h, unsigned int *address)
{
 assert(h && h<17 && allocated[h]);
 if (map_failure) return -KOS_EINVAL;
 *address=(unsigned int)(uintptr_t)aperture;
 return (int)h;
}
unsigned char aperture_open(unsigned char token)
{
 unsigned char previous=(unsigned char)active;
 assert(token<17 && (!token || allocated[token]));
 memcpy(pages[active],aperture,8192); memcpy(aperture,pages[token],8192);
 active=token; ++maps; return previous;
}
void aperture_close(unsigned char previous) { (void)aperture_open(previous); }
static int read_at(void *context, uint32_t offset, void *out, uint16_t n)
{
 (void)context; ++source_calls;
 if (source_failure || (staging_failure && source_calls>1)) return -KOS_EIO;
 assert(offset+n<=source_size); memcpy(out,source+offset,n); return n;
}
int kos_reader_from_handle(kos_reader_t *reader, kos_handle_source_t *handle)
{ reader->context=handle; reader->read_at=read_at; reader->size=source_size; return 0; }
static void reset(void)
{
 assert(!file_open && !live && !active);
 source_calls=maps=0;
 missing=allocation_failure=map_failure=source_failure=staging_failure=0;
 memcpy(source,original,source_size);
 panel_init(&panel); panel_begin(&panel,0);
}
static zvm_status_t advance(void)
{
 for (unsigned int n=0;n<10000;++n) {
  zvm_status_t status=game_step(&game);
  assert(!active && !game.mapped.active);
  if (status!=ZVM_YIELDED) return status;
 }
 assert(!"instruction budget exceeded"); return ZVM_FAULT;
}
int main(int argc, char **argv)
{
 FILE *file;
 unsigned int preload_reads;
 assert(argc==2 && (uintptr_t)aperture<=UINT32_MAX);
 file=fopen(argv[1],"rb"); assert(file);
 source_size=(uint32_t)fread(original,1,sizeof(original),file); fclose(file); assert(source_size>64);
 reset(); missing=1; assert(game_open(&game,"story",&panel)==-1); game_close(&game);
 reset(); source_failure=1; assert(game_open(&game,"story",&panel)==-2); game_close(&game);
 reset(); source[2]^=1; assert(game_open(&game,"story",&panel)==-3); game_close(&game);
 reset(); allocation_failure=1; assert(game_open(&game,"story",&panel)==-7); game_close(&game);
 reset(); staging_failure=1; assert(game_open(&game,"story",&panel)==-7); game_close(&game);
 reset(); map_failure=1; assert(game_open(&game,"story",&panel)==-7); game_close(&game);
 reset(); assert(game_open(&game,"story",&panel)==0);
 assert(live==(source_size+8191)/8192);
 preload_reads=source_calls;
 pointer_hits();
 assert(advance()==ZVM_NEED_INPUT && game_observe(&game)==0 && game.here==180 && game.state==1);
 game_output_finish(&panel); panel_finish(&panel); panel_begin(&panel,"north");
 assert(zvm_submit_input(&game.vm,"north")==ZVM_OK);
 assert(advance()==ZVM_NEED_INPUT && game_observe(&game)==0 && game.here==81 && game.state==1);
 assert(source_calls==preload_reads && !memcmp(source,original,source_size));
 game_output_finish(&panel); game_close(&game);
 reset(); assert(game_open(&game,"story",&panel)==0);
 ZVM_STATE(&game.vm)->pc=source_size;
 assert(advance()==ZVM_FAULT && zvm_error(&game.vm)==ZVM_EBOUNDS); game_close(&game);
 reset(); assert(!strcmp(game_load_error(-7),"story page preload failed"));
 puts("game integration: PASS (real story/movement, no runtime source reads, mapped scope restoration, preload failures, VM fault, cleanup)");
 return 0;
}
