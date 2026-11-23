#include "face_mesh.h"
#include "face_sampling.h"
#include "room_engine.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL face mesh: %s\n",why);exit(1);}}
static unsigned word(const void *p){unsigned n;memcpy(&n,p,4);return n;}
static void put(void *p,unsigned n){memcpy(p,&n,4);}
static uint32_t hash(const void *p,size_t n){const unsigned char *v=p;uint32_t h=2166136261u;while(n--)h=(h^*v++)*16777619u;return h;}
static double max_error;
static unsigned changed,total;
static void compare(RxFaceMesh *mesh,const PARAM_WORK *param)
{
    const MASK_WORK *fm=rx_face_mesh_work(mesh);const float *src=(const float *)fm->src->vlist+16;
    size_t bytes=rx_face_mesh_bytes(mesh);unsigned char *expected=malloc(bytes);check(expected!=NULL,"reference allocation");
    memcpy(expected,fm->src->vlist,bytes);float *dst=(float *)expected+16;
    const LIST_WORK *con=fm->list;
    for(int i=0;i<fm->lnum;++i){const VLIST_WORK *v=fm->vlist+i;double sum=0,numerator[3]={0};
        for(int j=0;j<v->mnum;++j,++con){double p=param->muscle[con->id],weight=fabs(p)*con->scal;
            sum+=weight;numerator[0]+=con->vec.x*p*weight;numerator[1]+=con->vec.y*p*weight;numerator[2]+=con->vec.z*p*weight;}
        for(unsigned k=0;k<3;++k)dst[v->id*8+k]=(float)(src[v->id*8+k]+(sum?numerator[k]/sum:0));
    }
    PARAM_WORK saved=*param;check(!rx_face_mesh_muscles(mesh,param)&&!memcmp(param,&saved,sizeof(saved)),"muscle stage failed or changed input");
    const unsigned char *actual=(const unsigned char *)fm->dst->vlist;
    check(!memcmp(actual,expected,64)&&!memcmp(actual+bytes-4,expected+bytes-4,4),"vertex packet framing changed");
    for(unsigned i=0;i<fm->head->nAttr;++i){const float *got=(const float *)actual+16+i*8;
        for(unsigned k=0;k<3;++k){double error=fabs((double)got[k]-dst[i*8+k]);
            if(error>max_error)max_error=error;
            check(isfinite(got[k])&&error<=0.0001,"EE-order muscle position differs from independent double reference");
            changed+=got[k]!=src[i*8+k];total++;}
        check(!memcmp(got+3,src+i*8+3,20),"muscles changed vertex flags/normal/weight lanes");
    }
    free(expected);
}
void rx_check_face_jaw(RxFaceMesh *mesh,unsigned actor,const void *mouth,size_t bytes);
void rx_check_face_mesh(RxEngineRoom *room,unsigned index,const unsigned char *rdt,size_t rdt_bytes)
{
    size_t mask_bytes;const unsigned char *mask=rx_engine_enemy_mask(room,index,&mask_bytes);
    RxFaceMesh *mesh=rx_engine_enemy_face_mesh(room,index);
    if(!mask){check(!mesh,"absent MASK produced a face context");return;}
    check(mesh!=NULL,"room failed to stage native facial mesh");
    const MASK_WORK *fm=rx_face_mesh_work(mesh);size_t bytes=rx_face_mesh_bytes(mesh);
    check(fm&&fm->vtype==51&&fm->vtop==16&&fm->vofs==8&&fm->mode==1&&!fm->nvpt&&!fm->obj,
          "detached muscle work claims a complete published geometry context");
    check(fm->src->vlist!=fm->dst->vlist&&(uintptr_t)fm->src>UINT32_MAX&&(uintptr_t)fm->head>UINT32_MAX,
          "facial source/destination ownership or pointer width");
    unsigned mask_hash=hash(mask,mask_bytes),source_hash=hash(fm->src->vlist,bytes);
    check(!memcmp(fm->head,mask,mask_bytes),"owned MASK copy changed encoded index words");
    const NJS_POINT3 *const *neighbors;size_t count=rx_face_mesh_neighbors(mesh,&neighbors),at=0;
    const unsigned char *indices=mask+64+fm->head->nList*4;
    for(int i=0;i<fm->lnum;++i)for(int j=0;j<fm->vlist[i].nvnum;++j,++at)
        check((uintptr_t)neighbors[at]>UINT32_MAX&&neighbors[at]==(const NJS_POINT3 *)((const float *)fm->dst->vlist+16+word(indices+at*4)*8),
              "native neighbor mapping differs from original index");
    check(at==count&&count==(index?8218u:8208u),"opening facial neighbor reference count");
    PARAM_WORK param={0};compare(mesh,&param);
    for(unsigned muscle=0;muscle<32;++muscle){memset(&param,0,sizeof(param));param.muscle[muscle]=muscle%2?-0.75f:0.625f;compare(mesh,&param);}
    for(unsigned i=0;i<32;++i)param.muscle[i]=((int)(i%7)-3)*0.125f;compare(mesh,&param);
    for(int i=0;i<fm->fnum;++i)compare(mesh,fm->face+i);
    size_t table=word(rdt+24);unsigned mouth=word(rdt+table+12);
    for(unsigned i=0;i<20;++i){RxFaceClip clip;check(!rx_face_clip(rdt+mouth,rdt_bytes-mouth,i,&clip),"opening expression view");
        float end=clip.keys[clip.count-1].frame;
        for(unsigned j=0;j<3;++j){check(!rx_face_sample(&clip,rx_engine_room_rom(room)->enep[index].id,end*j*0.5f,&param),"expression-to-muscle sampling");compare(mesh,&param);}}
    memset(&param,0,sizeof(param));compare(mesh,&param);
    check(!memcmp(fm->src->vlist,fm->dst->vlist,bytes),"zero-expression round trip left muscle drift");
    size_t output_bytes=bytes;unsigned char *snapshot=malloc(output_bytes);check(snapshot!=NULL,"snapshot allocation");memcpy(snapshot,fm->dst->vlist,output_bytes);
    PARAM_WORK previous=fm->param;param.muscle[0]=NAN;
    check(rx_face_mesh_muscles(mesh,&param)&&!memcmp(snapshot,fm->dst->vlist,bytes)&&!memcmp(&previous,&fm->param,sizeof(previous)),"nonfinite sample committed");
    for(unsigned i=0;i<32;++i)param.muscle[i]=FLT_MAX;
    check(rx_face_mesh_muscles(mesh,&param)&&!memcmp(snapshot,fm->dst->vlist,bytes),"overflowing weights committed");
    unsigned char *scratch=malloc(mask_bytes);check(scratch!=NULL,"corrupt MASK allocation");
    RxChunk chunk={.vertices=(const unsigned char *)fm->src->vlist,.vertex_extent=bytes};
    for(unsigned which=0;which<9;++which){memcpy(scratch,mask,mask_bytes);
        switch(which){case 0:put(scratch+12,UINT32_MAX);break;case 1:put(scratch+16,UINT32_MAX);break;
        case 2:scratch[64+2]=33;break;case 3:scratch[64+3]=255;break;case 4:put(scratch+64+fm->head->nList*4,UINT32_MAX);break;
        case 5:put(scratch+64+fm->head->nList*4+count*4,32);break;case 6:put(scratch+20,0);break;
        case 7:scratch[0]='X';break;case 8:put(scratch+32,UINT32_MAX);break;}
        RxFaceMesh *out=NULL;check(rx_face_mesh_create(scratch,mask_bytes,&chunk,&out)&&!out,"malformed mesh committed");}
    for(size_t n=0;n<mask_bytes;n+=127){RxFaceMesh *out=NULL;check(rx_face_mesh_create(mask,n,&chunk,&out)&&!out,"truncated MASK committed");}
    RxFaceMesh *out=NULL;chunk.vertex_extent=bytes-1;check(rx_face_mesh_create(mask,mask_bytes,&chunk,&out)&&!out,"truncated vertex packet committed");
    check(hash(mask,mask_bytes)==mask_hash&&hash(fm->src->vlist,bytes)==source_hash&&!memcmp(fm->head,mask,mask_bytes),"facial evaluation rewrote source/MASK/encoded index words");
    rx_check_face_jaw(mesh,rx_engine_room_rom(room)->enep[index].id,rdt+mouth,rdt_bytes-mouth);
    free(scratch);free(snapshot);
    printf("PASS detached native face mesh actor %u: %u vertices, %zu full-width neighbors; original muscle body, all 32 muscles/base keys/20 expressions, %u position channels (%u changed), maximum reference error %.9g; malformed/atomic rejection\n",index,fm->head->nAttr,count,total,changed,max_error);
}
