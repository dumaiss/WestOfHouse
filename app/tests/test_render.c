#include "kos.h"
#include "kos/gfx.h"
#include "kos/timer.h"
#include "render.h"
#include <assert.h>
static uint8_t scene[36864], vram[512][256], saved[36864];
static unsigned int upload_at, upload_calls, uploaded, closed, clock_closed, pointer_calls;
static struct { unsigned int x,y; char text[64]; uint8_t fg,bg; } calls[32];
static unsigned int texts;
int gfx_open(void) { return 0; }
void gfx_close(void) { ++closed; }
int gfx_set_mode(enum gfx_mode m) { assert(m==GFX_MODE_256x212_256); return 0; }
int gfx_clear(gfx_color_t c) { memset(vram,c,sizeof(vram)); return 0; }
int gfx_font_set_glyph(enum gfx_font f,uint8_t code,const uint8_t *rows)
{ (void)code; assert(f==GFX_FONT_6X8 && rows[7]<=63); return 0; }
int gfx_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,gfx_color_t c,enum gfx_op op)
{
 unsigned int row; assert(x+w<=256 && y+h<=512 && op==GFX_OP_COPY);
 for(row=y;row<y+h;++row) memset(&vram[row][x],c,w);
 return 0;
}
int gfx_copy_rect(uint16_t x,uint16_t y,uint16_t dx,uint16_t dy,uint16_t w,uint16_t h)
{
 unsigned int row; assert(x+w<=256 && dx+w<=256 && y+h<=512 && dy+h<=512);
 for(row=0;row<h;++row) memmove(&vram[dy+row][dx],&vram[y+row][x],w);
 return 0;
}
int gfx_upload_begin(uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{ assert(!x && !y && w==256 && h==144); upload_at=0; ++upload_calls; return 0; }
int gfx_upload_write(const void *data,uint16_t length)
{ memcpy((uint8_t *)vram+upload_at,data,length); upload_at+=length; uploaded+=length; return length; }
int gfx_upload_end(void) { assert(upload_at==36864); return 0; }
int gfx_text_n(uint16_t x,uint16_t y,const char *s,uint16_t n,const struct gfx_text_style *style)
{
 unsigned int row,col; assert(x+n*6<=256 && y+8<=512);
 assert(texts<32); calls[texts].x=x; calls[texts].y=y;
 memcpy(calls[texts].text,s,n); calls[texts].text[n]=0;
 calls[texts].fg=style->fg; calls[texts].bg=style->bg; ++texts;
 for(row=y;row<y+8u;++row) for(col=x;col<x+n*6u;++col) vram[row][col]=(col&1)?style->fg:style->bg;
 return 0;
}
int gfx_text(uint16_t x,uint16_t y,const char *s,const struct gfx_text_style *style)
{ return gfx_text_n(x,y,s,(uint16_t)strlen(s),style); }
int gfx_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,gfx_color_t c,enum gfx_op op)
{ return gfx_fill_rect(x,y,w,h,c,op); }
int gfx_line(uint16_t x,uint16_t y,uint16_t dx,uint16_t dy,gfx_color_t c,enum gfx_op op)
{ (void)x;(void)y;(void)dx;(void)dy;(void)c;(void)op; return 0; }
int gfx_display_enable(uint8_t enabled) { assert(enabled==1); return 0; }
int gfx_wait(void) { return 0; }
int gfx_pointer_bordered(int x,int y) { assert(x>=0 && x<256 && y>=0 && y<212); ++pointer_calls; return 0; }
int gfx_pointer_hide(void) { return 0; }
int timer_open(void) { return 7; }
int timer_now(int handle,uint32_t *now) { static uint32_t clock; assert(handle==7); *now=clock++; return 0; }
uint32_t timer_us(uint32_t n) { return n; }
int close(unsigned int handle) { assert(handle==7); ++clock_closed; return 0; }
int blob(unsigned int n) { assert(n==1 || n==2); return (int)n+10; }
int xread(unsigned int handle,unsigned int offset,void *buffer,unsigned int n)
{
 assert((handle==11 || handle==12) && n<=256);
 if(handle==12) offset+=32768;
 assert(offset+n<=sizeof(scene)); memcpy(buffer,scene+offset,n); return (int)n;
}
static int saw(const char *s,unsigned int x,unsigned int y)
{
 unsigned int i;
 for(i=0;i<texts;++i) if(!strcmp(calls[i].text,s) && calls[i].x==x && calls[i].y==y) return 1;
 return 0;
}
int main(int argc,char **argv)
{
 FILE *file;
 panel_t p;
 menu_t menu;
 unsigned int a,x,y;
 assert(argc==2); file=fopen(argv[1],"rb"); assert(file);
 assert(fread(scene,1,sizeof(scene),file)==sizeof(scene)); fclose(file);
 assert(render_open()==0 && render_scene(180,1)==0);
 assert(upload_calls==1 && uploaded==36864);
 assert(saw("HERE:      180",2,3) && saw("WOH-STATE: 1",2,11));
 for(a=0;a<36864;++a) { x=a%256; y=a/256;
  if(!(x>=2 && x<140 && y>=2 && y<20)) assert(((uint8_t *)vram)[a]==scene[a]);
 }
 texts=0; assert(render_scene(79,0)==0);
 assert(saw("WOH-STATE: 0",2,11) && upload_calls==1);
 memcpy(saved,vram,sizeof(saved));
 assert(render_pointer(70,140,1)==0 && pointer_calls==1 && !memcmp(saved,vram,sizeof(saved)));
 fixture_menu(&menu); texts=0; assert(render_menu(&menu)==0);
 assert(memcmp(saved,vram,sizeof(saved)));
 assert(render_menu_close(&menu)==0 && !memcmp(saved,vram,sizeof(saved)));
 panel_init(&p); panel_begin(&p,0);
 for(a=0;a<PANEL_ROWS+1u;++a) { panel_byte(&p,'a'); panel_byte(&p,'\n'); }
 panel_finish(&p); texts=0;
 assert(render_panel(&p,0)==0 && saw("[...]",14,194));
 texts=0; assert(render_prompt(&p,1)==0);
 assert(saw("> ",2,203) && saw("(PgUp/PgDn)",158,203) && saw("\037",242,203));
 for(a=0;a<40;++a) panel_insert(&p,'x');
 texts=0; assert(render_prompt(&p,1)==0);
 assert(!saw("(PgUp/PgDn)",158,203) && !saw("\037",242,203));
 render_close(); assert(closed==1 && clock_closed==1);
 puts("render contract tests passed: fixed scene, zero state, geometry, history, prompt, menu restoration");
 return 0;
}
