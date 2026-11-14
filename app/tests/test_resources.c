/* Exercise the actual SDK KRES reader and production WOH resource adapter. */
#include "kos.h"
#include "kos/kres.h"
#include "resources.h"
#include <assert.h>
#include <stdlib.h>
static FILE *archive_file;
static unsigned int opens, closes;
static int io_open(void *ctx,const char *path,unsigned int flags)
{ (void)ctx; assert(flags==KRES_OPEN_READ && !archive_file); archive_file=fopen(path,"rb"); if(!archive_file)return -KOS_ENOENT; ++opens; return 0; }
static int io_close(void *ctx)
{ (void)ctx; assert(archive_file); fclose(archive_file); archive_file=0; ++closes; return 0; }
static int io_size(void *ctx,uint32_t *size)
{ (void)ctx; assert(!fseek(archive_file,0,SEEK_END)); *size=(uint32_t)ftell(archive_file); return 0; }
static int io_read(void *ctx,void *dst,unsigned int n)
{ (void)ctx; return (int)fread(dst,1,n,archive_file); }
static int io_seek(void *ctx,uint32_t at)
{ (void)ctx; return fseek(archive_file,(long)at,SEEK_SET) ? -KOS_EIO : 0; }
const struct kres_io kres_io_konaos_read={io_open,io_close,io_size,io_read,0,io_seek,0,0,0,0};
static uint8_t pixels[54272];
static void match_pixels(const char *path,unsigned int count)
{
 uint8_t expected[256]; unsigned int at,chunk; FILE *file=fopen(path,"rb");assert(file);
 assert(!fseek(file,16,SEEK_SET));
 for(at=0;at<count;at+=chunk) {
  chunk=count-at; if(chunk>256)chunk=256;
  assert(resources_pixels(pixels+at,(uint16_t)chunk)==(int)chunk);
  assert(fread(expected,1,chunk,file)==chunk && !memcmp(expected,pixels+at,chunk));
 }
 fclose(file);
}
int main(int argc,char **argv)
{
 static const uint16_t pairs[8][2]={{180,1},{180,2},{81,1},{80,1},{79,1},{79,2},{203,1},{203,2}};
 unsigned int i; char path[512],*base;
 kos_reader_t reader; menu_t menu; uint8_t header[64],again[64];char command[81];
 assert(argc==4);
 assert(resources_open(argv[1])==0 && resources_story(&reader)==0);
 assert(kos_reader_read_full(&reader,0,header,64)==64 && header[0]==3);
 assert(resources_title()==0);match_pixels(argv[3],54272);
 assert(resources_scene(180,1)==1);match_pixels(argv[2],40704);
 assert(resources_menu(&menu,165,100)==1 && menu.count==2); /* door inside house */
 menu_command(&menu,1,command);assert(!strcmp(command,"open door"));
 assert(resources_menu(&menu,20,20)==0);
 assert(resources_scene(79,2)==1 && resources_menu(&menu,0,158)==0);
 assert(resources_scene(201,0)==0 && resources_menu(&menu,100,100)==0);
 assert(resources_scene(180,65535)==0);
 /* Another logical resource's reads never corrupt the absolute story view. */
 assert(kos_reader_read_full(&reader,0,again,64)==64 && !memcmp(header,again,64));
 for(i=0;i<8;++i) {
  assert(resources_scene(pairs[i][0],pairs[i][1])==1);
  snprintf(path,sizeof(path),"%s",argv[2]);base=strrchr(path,'/');assert(base);
  snprintf(base+1,sizeof(path)-(unsigned int)(base+1-path),"room-%u-state-%u.img",pairs[i][0],pairs[i][1]);
  match_pixels(path,40704);
 }
 resources_close();assert(opens==1 && closes==1);
 assert(resources_open("/no/such/archive")==-KOS_ENOENT);resources_close();
 puts("resources passed: real SDK KRES, title/scene bytes, pair lookup, nested hotspot/actions, unbound exclusion, shared story cursor, single file, cleanup");
 return 0;
}
