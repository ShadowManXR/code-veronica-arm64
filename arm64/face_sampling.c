#include "face_sampling.h"
#include "face_bh.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(PARAM_WORK)==164 && offsetof(PARAM_WORK,frame)==160,"Face key disk layout");
static uint32_t word(const void *p){uint32_t n;memcpy(&n,p,4);return n;}
static const char *expression(const unsigned char *p,size_t n)
{
    if(n<8 || memcmp(p+1,"MAN",3) || (uintptr_t)p%4 || !word(p+4) || word(p+4)>(n-8)/164 || 8+(size_t)word(p+4)*164!=n)
        return "Invalid facial expression extent";
    const PARAM_WORK *keys=(const PARAM_WORK *)(p+8);
    for(unsigned i=0;i<word(p+4);++i) {
        if(keys[i].frame<0 || (i&&keys[i].frame<=keys[i-1].frame))return "Invalid facial key order";
        for(unsigned j=0;j<40;++j){float f;memcpy(&f,p+8+i*164+j*4,4);if(!isfinite(f))return "Nonfinite facial key";}
    }
    return NULL;
}
static const char *lip(const unsigned char *p,size_t n)
{
    for(size_t i=0;i<n;i+=2) {
        if(p[i]==255)return NULL; /* original alignment padding follows sentinel */
        if(n-i<3 || p[i]>=6 || !p[i+1])return "Invalid lip phoneme/duration";
    }
    return "Missing lip terminator";
}
static const char *bank(const void *data,size_t bytes,int is_lip,unsigned wanted,
                        const unsigned char **found,size_t *length,unsigned *count)
{
    if(!data || (uintptr_t)data%4)return "Missing or unaligned facial bank";
    const unsigned char *p=data;size_t offset=0;unsigned num=0;
    const unsigned char *selected=NULL;size_t selected_bytes=0;
    while(bytes-offset>=4) {
        uint32_t n=word(p+offset);offset+=4;
        if(n==UINT32_MAX) {
            if(found && !selected)return "Facial clip index outside bank";
            if(found){*found=selected;*length=selected_bytes;}if(count)*count=num;return NULL;
        }
        if(n>bytes-offset)return "Facial clip exceeds bank";
        const char *error=is_lip?lip(p+offset,n):expression(p+offset,n);if(error)return error;
        if(num==wanted){selected=p+offset;selected_bytes=n;}num++;offset+=n;
    }
    return "Missing facial bank terminator";
}
const char *rx_face_bank_validate(const void *data,size_t bytes,int is_lip,unsigned *count)
{return bank(data,bytes,is_lip,0,NULL,NULL,count);}
const char *rx_face_clip(const void *data,size_t bytes,unsigned index,RxFaceClip *out)
{
    const unsigned char *p;size_t n;if(!out)return "Missing facial clip output";
    const char *error=bank(data,bytes,0,index,&p,&n,NULL);if(error)return error;
    *out=(RxFaceClip){(const PARAM_WORK *)(p+8),word(p+4),p[0]};return NULL;
}
const char *rx_lip_clip(const void *data,size_t bytes,unsigned index,RxLipClip *out)
{
    const unsigned char *p;size_t n;if(!out)return "Missing lip clip output";
    const char *error=bank(data,bytes,1,index,&p,&n,NULL);if(error)return error;
    *out=(RxLipClip){p,n};return NULL;
}
const char *rx_face_sample(const RxFaceClip *clip,unsigned actor,float frame,PARAM_WORK *out)
{
    if(!clip || !clip->keys || !clip->count || !out || (clip->actor&&clip->actor!=actor) ||
       !isfinite(frame) || frame<0 || frame>32767)return "Invalid facial sample request";
    MASK_WORK fm={0};fm.face=(PARAM_WORK *)clip->keys;fm.fnum=(int)clip->count;
    /* Initialize fields not overwritten by the original interpolation routine. */
    fm.param=clip->keys[0];fmCnkSetCurrentFrame(&fm,frame);*out=fm.param;return NULL;
}
const char *rx_lip_begin(const RxLipClip *clip,LIP_WORK *out)
{
    if(!clip || !clip->data || !out)return "Missing lip sample request";
    const char *error=lip(clip->data,clip->bytes);if(error)return error;
    *out=(LIP_WORK){.top=(unsigned char *)clip->data,.ptr=(unsigned char *)clip->data,.flag=1};return NULL;
}
const char *rx_lip_sample(const RxLipClip *clip,const PARAM_WORK base[6],LIP_WORK *state,PARAM_WORK *inout)
{
    if(!clip || !clip->data || !base || !state || !inout || state->top!=clip->data ||
       (uintptr_t)state->ptr<(uintptr_t)clip->data || (uintptr_t)state->ptr-(uintptr_t)clip->data>=clip->bytes ||
       ((uintptr_t)state->ptr-(uintptr_t)clip->data)%2 || state->cur>=6 || state->next>=6 ||
       state->cnt<0 || state->time<0 || state->time>255 || state->cnt>32765 || state->flag>1)
        return "Invalid native lip cursor";
    if(!state->flag)return NULL;
    const char *error=lip(state->ptr,clip->bytes-((uintptr_t)state->ptr-(uintptr_t)clip->data));if(error)return error;
    MASK_WORK fm={0};fm.param=*inout;LIP_WORK next=*state;
    fmSetLipSyncParam(&fm,(PARAM_WORK *)base,&next,0x3fff000);*state=next;*inout=fm.param;return NULL;
}
