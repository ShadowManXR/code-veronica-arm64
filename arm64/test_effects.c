#include "room_engine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok,const char *message)
{
    if (!ok) { fprintf(stderr,"FAIL common effects: %s\n",message); exit(1); }
}
static uint32_t word(const unsigned char *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned half(const unsigned char *p) { return p[0]|(unsigned)p[1]<<8; }
static void put(unsigned char *p,uint32_t value) { for (unsigned i=0;i<4;++i) p[i]=(unsigned char)(value>>(8*i)); }
static uint32_t hash(const void *data,size_t bytes)
{
    const unsigned char *p=data;
    uint32_t value=2166136261u;
    for (size_t i=0;i<bytes;++i) value=(value^p[i])*16777619u;
    return value;
}
int main(int argc,char **argv)
{
    check(argc==2,"supply original SYSTEM.AFS member 1");
    FILE *file=fopen(argv[1],"rb");
    check(file && !fseek(file,0,SEEK_END),"open fixture");
    long length=ftell(file);
    check(length>0 && !fseek(file,0,SEEK_SET),"fixture size");
    unsigned char *data=malloc((size_t)length),*bad=malloc((size_t)length);
    check(data && bad && fread(data,1,(size_t)length,file)==(size_t)length,"read fixture");
    fclose(file);
    RxEngineEffects *effects=NULL;
    const char *error=rx_engine_effects_load(data,(size_t)length,&effects);
    check(!error,error);
    const unsigned ids[]={0,1,7,13},nodes[]={2,2,3,2};
    for (unsigned i=0;i<4;++i) {
        ML_WORK *model=rx_engine_effect_model(effects,ids[i]);
        check(model && model->obj_num==nodes[i] && (uintptr_t)model->objP>UINT32_MAX,
              "native model counts or pointers differ from disc");
    }
    check(!rx_engine_effect_model(effects,2) && !rx_engine_shadow_mesh(effects,2),"absent model accepted");
    const uint32_t vertex_hash[]={0x31e4473d,0x1a759909},triangle_hash[]={0x5cb586a5,0xc66761e4};
    const unsigned vertices[]={18,33},triangles[]={32,62};
    for (unsigned i=0;i<2;++i) {
        const RxShadowMesh *mesh=rx_engine_shadow_mesh(effects,i);
        check(mesh && mesh->vertex_count==vertices[i] && mesh->triangle_count==triangles[i] &&
              hash(mesh->positions,mesh->vertex_count*sizeof(*mesh->positions))==vertex_hash[i] &&
              hash(mesh->indices,mesh->triangle_count*3*sizeof(*mesh->indices))==triangle_hash[i],
              "modifier vertices/triangles differ from independent retail reference");
    }
    ML_WORK *active=rx_engine_effect_model(effects,1);
    size_t shadow_offset=4+(size_t)word(data)+4;
    size_t top=shadow_offset+half(data+shadow_offset+4);
    size_t node=top+word(data+shadow_offset+12);
    size_t model=top+word(data+node+4);
    size_t vertex=top+word(data+model),polygon=top+word(data+model+4);
    size_t picture_offset=0;
    for (unsigned i=0;i<4;++i) picture_offset+=4+word(data+picture_offset);
    picture_offset=(picture_offset+4+31)&~(size_t)31;
    unsigned char *pictures=rx_engine_effect_pictures(effects);
    check(pictures && (uintptr_t)pictures>UINT32_MAX && !memcmp(pictures,data+picture_offset,32),
          "common texture view is not owned native package data");
    uint32_t picture_hash=hash(pictures,(size_t)length-picture_offset);
    size_t first_tim=picture_offset+32+word(data+picture_offset+4);
    for (unsigned variant=0;variant<12;++variant) {
        memcpy(bad,data,(size_t)length);
        size_t bytes=(size_t)length;
        switch (variant) {
        case 0: put(bad,UINT32_MAX); break;
        case 1: bytes=shadow_offset+16; break;
        case 2: bad[vertex]=51; break;
        case 3: put(bad+vertex+8,0x7fc00000); break;
        case 4: bad[polygon+6]=255; bad[polygon+7]=255; break;
        case 5: bad[polygon+2]=255; bad[polygon+3]=255; break;
        case 6: put(bad+first_tim+4,UINT32_MAX); break;
        case 7: bytes=first_tim+64; break;
        case 8: bad[first_tim+32+0x93]=3; break;
        case 9: put(bad+picture_offset+40,0x4000); break;
        case 10: put(bad+picture_offset+32,28); break;
        case 11: put(bad+picture_offset+40,1); break;
        }
        check(rx_engine_effects_load(bad,bytes,&effects)!=NULL && rx_engine_effect_model(effects,1)==active,
              "malformed package replaced live effect models");
    }
    memset(data,0,(size_t)length);
    check(hash(rx_engine_shadow_mesh(effects,0)->positions,18*3*sizeof(float))==vertex_hash[0],
          "common effect models borrowed expired input");
    check(hash(rx_engine_effect_pictures(effects),(size_t)length-picture_offset)==picture_hash,
          "common effect textures borrowed expired input");
    rx_engine_effects_destroy(effects);
    free(data);free(bad);
    puts("PASS four original common effect models with owned native pointers above 4 GiB");
    puts("PASS both original shadow modifier meshes: 51 vertices and 94 triangles match retail reference");
    puts("PASS malformed effect sizes, formats, vertices and indices preserve the active model bank");
    puts("PASS bounded common texture packets, palette capacity, image formats and 40-entry expansion; immutable owned source");
    return 0;
}
