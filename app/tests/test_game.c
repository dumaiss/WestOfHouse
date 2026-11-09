/* Real VM and production cache, with bounded substitutes for OS storage. */
#include "kos.h"
#include "game.h"
#include "probe.h"
#include <assert.h>
#include <stdlib.h>
static uint8_t source[131070], original[131070], backing[32768];
static uint32_t source_size;
static int file_open, allocated, source_calls;
static int missing, allocation_failure, source_failure, staging_failure, dynamic_failure;
woh_probe_t woh_probe;
static panel_t panel;
extern game_t game;
int open(const char *path,unsigned char flags)
{ (void)path;(void)flags; if(missing) return -KOS_ENOENT; assert(!file_open); file_open=1; return 1; }
int close(unsigned int handle) { assert(handle==1 && file_open); file_open=0; return 0; }
int xalloc(unsigned int size,unsigned int flags)
{ (void)flags; assert(size<=sizeof(backing)); if(allocation_failure) return -KOS_ENOSPC; assert(!allocated); allocated=1; return 17; }
int xfree(unsigned int handle) { assert(handle==17 && allocated); allocated=0; return 0; }
int xread(unsigned int h,unsigned int offset,void *out,unsigned int n)
{ assert(h==17 && allocated && offset+n<=sizeof(backing)); if(dynamic_failure) return -KOS_EIO; memcpy(out,backing+offset,n); return (int)n; }
int xwrite(unsigned int h,unsigned int offset,const void *data,unsigned int n)
{ assert(h==17 && allocated && offset+n<=sizeof(backing)); memcpy(backing+offset,data,n); return (int)n; }
static int read_at(void *context,uint32_t offset,void *out,uint16_t n)
{
 (void)context; ++source_calls;
 if(source_failure || (staging_failure && source_calls>1)) return -KOS_EIO;
 assert(offset+n<=source_size); memcpy(out,source+offset,n); return n;
}
int kos_reader_from_handle(kos_reader_t *reader,kos_handle_source_t *handle)
{ reader->context=handle; reader->read_at=read_at; reader->size=source_size; return 0; }
static void reset(void)
{
 assert(!file_open && !allocated);
 source_calls=missing=allocation_failure=source_failure=staging_failure=dynamic_failure=0;
 memcpy(source,original,source_size);
 panel_init(&panel); panel_begin(&panel,0);
}
static zvm_status_t advance(void)
{
 unsigned int n;
 zvm_status_t status;
 for(n=0;n<10000;++n) { status=game_step(&game); if(status!=ZVM_YIELDED) return status; }
 assert(!"instruction budget exceeded"); return ZVM_FAULT;
}
int main(int argc,char **argv)
{
 FILE *file;
 assert(argc==2); file=fopen(argv[1],"rb"); assert(file);
 source_size=(uint32_t)fread(original,1,sizeof(original),file); fclose(file);
 assert(source_size>64);
 reset(); missing=1; assert(game_open(&game,"story",&panel)==-1); game_close(&game);
 reset(); source_failure=1; assert(game_open(&game,"story",&panel)==-2); game_close(&game);
 reset(); source[2]^=1; assert(game_open(&game,"story",&panel)==-3); game_close(&game);
 reset(); allocation_failure=1; assert(game_open(&game,"story",&panel)==-5); game_close(&game);
 reset(); staging_failure=1; assert(game_open(&game,"story",&panel)==-7); game_close(&game);
 reset(); assert(game_open(&game,"story",&panel)==0);
 assert(advance()==ZVM_NEED_INPUT && game_observe(&game)==0 && game.here==180 && game.state==1);
 game_output_finish(&panel); panel_finish(&panel);
 panel_begin(&panel,"north"); assert(zvm_submit_input(&game.vm,"north")==ZVM_OK);
 assert(advance()==ZVM_NEED_INPUT && game_observe(&game)==0 && game.here==81 && game.state==1);
 game_output_finish(&panel); game_close(&game);
 assert(!memcmp(source,original,source_size));
 reset(); assert(game_open(&game,"story",&panel)==0);
 dynamic_failure=1; assert(advance()==ZVM_FAULT); game_close(&game);
 reset(); assert(!strcmp(game_load_error(-5),"no XMEM available for story memory"));
 puts("game integration tests passed: actual story, movement, file/read/binding/allocation/staging/VM faults and cleanup");
 return 0;
}
