#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zvm.h"
#include "zvm_internal.h"
zvm_status_t zvm_reference_run(zvm_t *, uint16_t);
typedef struct { unsigned long hash, bytes; } output_t;
static zvm_error_t output(void *u, uint8_t channel, uint8_t byte)
{
    output_t *o=u;
    o->hash=(o->hash*33u+byte+channel*257u)&0xfffffffful;
    ++o->bytes;
    return ZVM_OK;
}
static void put16(unsigned char *p, unsigned int n)
{ p[0]=(unsigned char)(n>>8); p[1]=(unsigned char)n; }
static void arithmetic(void)
{
    static const unsigned int values[]={0,1,2,0x7fff,0x8000,0x8001,0xfffe,0xffff};
    unsigned int count=0;
    for(unsigned int op=20;op<=24;++op)
        for(unsigned int i=0;i<8;++i) for(unsigned int j=0;j<8;++j) {
            unsigned char sa[1024]={0},sb[1024];
            zvm_t a,b;
            sa[0]=3;put16(sa+6,64);put16(sa+12,128);put16(sa+14,768);put16(sa+26,512);
            sa[64]=(unsigned char)(0xc0|op);sa[65]=15;
            put16(sa+66,values[i]);put16(sa+68,values[j]);sa[70]=16;sa[71]=0xba;
            memcpy(sb,sa,sizeof(sa));
            assert(zvm_init(&a,sa,sizeof(sa))==ZVM_OK && zvm_init(&b,sb,sizeof(sb))==ZVM_OK);
            assert(zvm_reference_run(&a,50)==zvm_run(&b,50));
            assert(zvm_error(&a)==zvm_error(&b) && !memcmp(sa,sb,sizeof(sa)));
            ++count;
        }
    printf("compact arithmetic differential: PASS (%u signed edge cases)\n",count);
}
int main(int argc,char **argv)
{
    static const char *commands[]={"look","open mailbox","read leaflet","north","east","open window","enter window","west","take lamp","turn on lamp","look","inventory","south","east","west","score","diagnose","wait","xyzzy","quit","yes"};
    zvm_t a,b;
    output_t oa={5381,0},ob={5381,0};
    FILE *f;
    long n;
    unsigned char *sa,*sb;
    unsigned int turn=0,slices=0;
    arithmetic();
    assert(argc==2 && (f=fopen(argv[1],"rb")));
    fseek(f,0,SEEK_END);n=ftell(f);rewind(f);
    sa=malloc(n);sb=malloc(n);assert(sa&&sb);
    assert(fread(sa,1,n,f)==(size_t)n);fclose(f);memcpy(sb,sa,n);
    assert(zvm_init(&a,sa,n)==ZVM_OK && zvm_init(&b,sb,n)==ZVM_OK);
    assert(zvm_set_output_callback(&a,output,&oa)==ZVM_OK);
    assert(zvm_set_output_callback(&b,output,&ob)==ZVM_OK);
    while (++slices<200000) {
        zvm_status_t x=zvm_reference_run(&a,50),y=zvm_run(&b,50);
        if(x!=y || zvm_program_counter(&a)!=zvm_program_counter(&b) || oa.hash!=ob.hash || oa.bytes!=ob.bytes || memcmp(sa,sb,n)) {
            fprintf(stderr,"divergence turn=%u slice=%u status=%d/%d error=%d/%d PC=%lu/%lu output=%lu/%lu\n",turn,slices,x,y,zvm_error(&a),zvm_error(&b),(unsigned long)zvm_program_counter(&a),(unsigned long)zvm_program_counter(&b),oa.bytes,ob.bytes);
            return 1;
        }
        assert(ZVM_STATE(&a)->sp==ZVM_STATE(&b)->sp);
        assert(ZVM_STATE(&a)->frame_count==ZVM_STATE(&b)->frame_count);
        if(x==ZVM_FINISHED)break;
        assert(x!=ZVM_FAULT);
        if(x==ZVM_NEED_INPUT) {
            if(turn==sizeof(commands)/sizeof(commands[0]))break;
            assert(zvm_submit_input(&a,commands[turn])==ZVM_OK);
            assert(zvm_submit_input(&b,commands[turn])==ZVM_OK);
            ++turn;
        }
    }
    assert(slices<200000 && turn>=10);
    printf("compact differential: PASS story=%s turns=%u slices=%u bytes=%lu hash=%08lx\n",argv[1],turn,slices,oa.bytes,oa.hash);
    free(sa);free(sb);
    return 0;
}
