#include "face_sampling.h"
#include "ps2_NaMath.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL face sampling: %s\n",why);exit(1);}}
static unsigned word(const void *p){unsigned n;memcpy(&n,p,4);return n;}
static float scalar(const PARAM_WORK *p,unsigned i){float f;memcpy(&f,(const unsigned char *)p+4*i,4);return f;}
static void near(float a,float b){if(!(isfinite(a)&&fabsf(a-b)<0.00002f)){fprintf(stderr,"sample got %.9g expected %.9g delta %.9g\n",a,b,a-b);check(0,"original sampler differs from scalar reference");}}
void rx_check_face_sampling(const unsigned char *rdt,size_t bytes)
{
    _Make_SinTable();
    size_t table=word(rdt+24); /* independent RM_0000 has two enemy records */
    unsigned mouth=word(rdt+table+12),lip=word(rdt+table+16),count=999;
    check(mouth==1598488&&lip==1620728&&bytes>lip,"opening facial bank reference changed");
    check(!rx_face_bank_validate(rdt+mouth,bytes-mouth,0,&count)&&count==20,"retail expression bank");
    check(!rx_face_bank_validate(rdt+lip,bytes-lip,1,&count)&&count==19,"retail lip bank");
    unsigned samples=0;
    for(unsigned index=0;index<20;++index) {
        RxFaceClip clip={0};check(!rx_face_clip(rdt+mouth,bytes-mouth,index,&clip),"retail expression select");
        check((uintptr_t)clip.keys>UINT32_MAX,"facial key pointer truncated");
        for(unsigned half=0;half<=(unsigned)(clip.keys[clip.count-1].frame+2)*2;++half) {
            float frame=half*0.5f;PARAM_WORK got={0};
            check(!rx_face_sample(&clip,98,frame,&got),"retail expression sample");
            unsigned next=0;while(next<clip.count&&(unsigned)clip.keys[next].frame<=(unsigned)frame)next++;
            for(unsigned j=0;j<40;++j) {
                float expected;
                if(!next)expected=scalar(clip.keys,j);
                else if(next==clip.count)expected=scalar(clip.keys+clip.count-1,j);
                else {const PARAM_WORK *a=clip.keys+next-1,*b=a+1;
                    float t=(frame-a->frame)/(b->frame-a->frame);expected=scalar(a,j)+t*(scalar(b,j)-scalar(a,j));}
                near(scalar(&got,j),expected);
            }
            samples++;
        }
    }
    /* All original easing modes, endpoint copies, actor binding and errors. */
    struct {unsigned size,id,count;PARAM_WORK keys[2];unsigned end;} packet={.size=336,.id=0x4e414d62,.count=2,.end=UINT32_MAX};
    packet.keys[0].frame=4;packet.keys[1].frame=20;
    for(unsigned i=0;i<40;++i){float a=1,b=9;memcpy((char *)&packet.keys[0]+i*4,&a,4);memcpy((char *)&packet.keys[1]+i*4,&b,4);}
    for(unsigned mode=0;mode<7;++mode) {
        packet.keys[0].flag=(short)mode;RxFaceClip clip;PARAM_WORK got;
        check(!rx_face_clip(&packet,sizeof(packet),0,&clip),"synthetic expression bind");
        check(!rx_face_sample(&clip,98,8,&got),"eased expression sample");
        const float rates[]={0.25f,0.0625f,0.5f,0.5f-0.5f*sinf(0.0000958738f*8191),0,0.25f,0.25f};
        for(unsigned j=0;j<40;++j)near(scalar(&got,j),1+8*rates[mode]);
        PARAM_WORK old=got;
        check(rx_face_sample(&clip,91,8,&got)&&!memcmp(&got,&old,sizeof(got)),"foreign actor changed output");
        check(rx_face_sample(&clip,98,NAN,&got)&&rx_face_sample(&clip,98,-1,&got)&&!memcmp(&got,&old,sizeof(got)),"bad frame changed output");
    }
    RxFaceClip untouched={0},previous=untouched;
    packet.keys[1].frame=4;check(rx_face_clip(&packet,sizeof(packet),0,&untouched)&&!memcmp(&untouched,&previous,sizeof(previous)),"duplicate key committed");
    packet.keys[1].frame=20;packet.keys[1].muscle[0]=INFINITY;
    check(rx_face_clip(&packet,sizeof(packet),0,&untouched)!=NULL,"nonfinite key accepted");packet.keys[1].muscle[0]=9;
    for(size_t n=0;n<sizeof(packet);++n)check(rx_face_clip(&packet,n,0,&untouched)!=NULL,"truncated facial bank accepted");
    check(rx_face_clip(&packet,sizeof(packet),1,&untouched)!=NULL,"missing expression index accepted");
    unsigned lip_ticks=0;PARAM_WORK base[6]={0};
    for(unsigned i=0;i<6;++i){
        for(unsigned j=0;j<32;++j)base[i].muscle[j]=(float)i;
        int angle=(int)i*1024;memcpy(&base[i].jawang,&angle,4);memcpy(&base[i].tangx,&angle,4);memcpy(&base[i].tangy,&angle,4);
        base[i].jawtrans=(float)i;base[i].tangz=(float)i;
    }
    for(unsigned i=0;i<19;++i) {
        RxLipClip clip;LIP_WORK state;
        check(!rx_lip_clip(rdt+lip,bytes-lip,i,&clip)&&!rx_lip_begin(&clip,&state),"retail native lip begin");
        size_t pos=0;int elapsed=0,duration=0;unsigned cur=0,next=0,active=1;
        PARAM_WORK got={0};for(unsigned j=0;j<32;++j)got.muscle[j]=-17;got.eye=(NJS_POINT3){11,22,33};
        for(unsigned tick=0;active&&tick<10000;++tick) {
            if(elapsed>=duration){elapsed-=duration;if(clip.data[pos]==255){cur=0;active=0;}
                else{cur=clip.data[pos];duration=clip.data[pos+1];pos+=2;next=clip.data[pos]==255?0:clip.data[pos];}}
            float expected=(float)cur;
            if(active&&duration-elapsed<10){float t=duration<10?(float)elapsed/duration:(10-(duration-elapsed))/10.0f;
                expected=(cur+t*((float)next-cur))*(duration<10?duration/10.0f:1);}
            check(!rx_lip_sample(&clip,base,&state,&got),"bounded original lip sample");
            for(unsigned j=0;j<32;++j)near(got.muscle[j],(0x3fff000u&(1u<<j))?expected:-17);
            near(got.jawang,expected*(1024*0.005493164f));near(got.jawtrans,expected);near(got.tangz,expected);
            check(got.eye.x==11&&got.eye.y==22&&got.eye.z==33,"lip altered eye expression");
            if(active)elapsed+=2;
            check(state.cnt==elapsed&&state.time==duration&&state.flag==active&&state.ptr==clip.data+pos,"lip timing/cursor differs");lip_ticks++;
        }
        check(!state.flag,"retail lip failed to terminate");
        PARAM_WORK saved=got;state.ptr=(unsigned char *)(uintptr_t)1;
        check(rx_lip_sample(&clip,base,&state,&got)&&!memcmp(&got,&saved,sizeof(got)),"foreign lip cursor wrote parameters");
    }
    unsigned char bad[][4]={{6,1,255,0},{0,0,255,0},{0,1,0,1},{0,255,0,0}};
    for(unsigned i=0;i<4;++i){RxLipClip clip={bad[i],4};LIP_WORK got;memset(&got,0xa5,sizeof(got));LIP_WORK old=got;
        check(rx_lip_begin(&clip,&got)&&!memcmp(&got,&old,sizeof(got)),"invalid lip stream committed");}
    printf("PASS original native face sampling: 20 retail expressions/%u samples, all easing modes, 19 lip sequences/%u ticks, full-width pointers and malformed/atomic rejection; geometry remains gated\n",samples,lip_ticks);
}

