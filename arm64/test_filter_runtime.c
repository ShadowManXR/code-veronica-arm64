#include "effect_runtime.h"
#include "android/particle_draw.h"
#include "android_egl.h"
#include "effect.h"
#include "main.h"
#include "ps2_NaTextureFunction.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok,const char *why)
{
    if (!ok) { fprintf(stderr,"FAIL original filter: %s\n",why);exit(1); }
}
static void tick(void) { const char *error=rx_effects_tick();check(!error,error); }
static O_WRK *allocate(void)
{
    EF_WORK record={.flg=1,.id=92,.sx=4,.sy=4,.sz=4};
    int slot=bhSetEffectTb(&record,NULL,NULL,0);check(slot>=0,"native filter allocation");return &eff[slot];
}
static void uv(unsigned rotation,float x,float y,float *u,float *v)
{
    switch (rotation) {
    case 0: *u=x;*v=y;break;
    case 1: *u=y;*v=1-x;break;
    case 2: *u=1-x;*v=1-y;break;
    default: *u=1-y;*v=x;break;
    }
}
static void lifecycle(void)
{
    for (unsigned rotation=0;rotation<4;++rotation) for (unsigned opacity=0;opacity<16;++opacity) {
        rx_effects_reset();O_WRK *op=allocate();cam.flg&=~8u;sys->fil_no=2;sys->fil_rt=(opacity<<4)|12|rotation;
        tick();check(op->mode0==1 && (op->flg&0x1000000) && op->tex_id==92 && op->gidx==bhGetGidx(op) &&
                     op->tv[3].x==640 && op->tv[3].y==480 && op->tv[0].z==1.0869f && !sys->ef_trs2dn,
                     "original filter initialization differs");
        tick();check(op->mode0==1 && !sys->ef_trs2dn,"disabled camera filter was queued");
        cam.flg|=8;sys->fil_no=1;tick();check(op->mode0==1 && !sys->ef_trs2dn,"wrong camera filter selector was queued");
        sys->fil_no=2;tick();
        check(op->mode0==2 && !(op->flg&0x1000000) && sys->ef_trs2dn==1 && sys->ef_trs2d[0]==op &&
              op->tv[0].col==(((opacity<<4|15)<<24)|0xffffffu),"original filter activation/opacity differs");
        for (unsigned i=0;i<4;++i) {
            float u,v;uv(rotation,(float)(i&1),(float)(i>>1),&u,&v);
            check(op->tv[i].u==u && op->tv[i].v==v,"original filter orientation differs");
        }
        NJS_TEXTURE_VTX saved[4];memcpy(saved,op->tv,sizeof(saved));sys->fil_rt^=0xf3;tick();
        check(!memcmp(saved,op->tv,sizeof(saved)),"active filter failed to latch original rotation/opacity");
        sys->sp_flg&=~8u;cam.flg&=~8u;tick();
        check(op->mode0==2 && sys->ef_trs2dn==1,"paused filter did not retain its original queue");
        sys->sp_flg|=8;tick();check(op->mode0==1 && (op->flg&0x1000000) && !sys->ef_trs2dn,"filter did not deactivate");
        cam.flg|=8;tick();check(op->mode0==2 && sys->ef_trs2dn==1 && memcmp(saved,op->tv,sizeof(saved)),
                               "filter did not refresh after reactivation");
        op->stflg|=0x1000000;tick();check(!sys->ef_trs2dn && op->mode0==2,"hidden filter task updated");
        op->stflg=0;op->gidx^=1;check(rx_effects_tick()!=NULL,"stale filter texture identity was admitted");
    }
    rx_effects_reset();O_WRK *op=allocate();op->ani_ct=1;check(rx_effects_tick()!=NULL,"unported filter page was read");
    rx_effects_reset();op=allocate();op->type=1;check(rx_effects_tick()!=NULL,"unported filter type was admitted");
    rx_effects_reset();op=allocate();tick();op->tv[0].u=NAN;check(rx_effects_tick()!=NULL,"nonfinite filter UV was admitted");
    rx_effects_reset();for (unsigned i=0;i<129;++i) allocate();tick();cam.flg|=8;sys->fil_no=2;
    check(rx_effects_tick()!=NULL && sys->ef_trs2dn==128,"filter queue overflow was not stopped");
    rx_effects_reset();
    puts("PASS original camera filter: four orientations, 16 opacities, latched settings, activation/selector/pause/hide transitions");
    puts("PASS bounded filter page/type/UV/texture identity and full native screen queue");
}
static float sample(const uint32_t *atlas,unsigned w,unsigned h,float u,float v,unsigned channel)
{
    float x=u*w-.5f,y=v*h-.5f;int ix=(int)floorf(x),iy=(int)floorf(y);
    float fx=x-ix,fy=y-iy,result=0;
    for (int j=0;j<2;++j) for (int i=0;i<2;++i) {
        int px=(ix+i+(int)w)%(int)w,py=(iy+j+(int)h)%(int)h;
        if (gs.ctx[0].clamp&3) px=ix+i<0?0:ix+i>=(int)w?(int)w-1:ix+i;
        if ((gs.ctx[0].clamp>>2)&3) py=iy+j<0?0:iy+j>=(int)h?(int)h-1:iy+j;
        result+=((atlas[py*w+px]>>(channel*8))&255)*(i?fx:1-fx)*(j?fy:1-fy);
    }
    return result;
}
static void clear(unsigned w,unsigned h)
{
    glViewport(0,0,w,h);glDepthMask(GL_TRUE);glClearDepthf(1);glClearColor(80/255.0f,144/255.0f,208/255.0f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
}
static void gpu(void)
{
    RxEgl egl={0};RxParticleDraw *draw=NULL;check(rx_egl_open(&egl,NULL,1280,960),"filter EGL surface");gs_reset();
    const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
    check(njSetTexture(&sys->ef_tlist)&&njSetTextureNum(sys->ef_tn[92])>=0,"original filter texture selection");
    uint64_t tex0=gs.ctx[0].tex0;unsigned w=1u<<((tex0>>26)&15),h=1u<<((tex0>>30)&15);
    uint32_t *atlas=malloc((size_t)w*h*4);unsigned char *pixels=malloc(1280*960*4);
    check(atlas && pixels,"filter GPU reference allocation");gs_decode_texture(tex0,gs.texa,w,h,atlas);
    const unsigned background[]={80,144,208},levels[]={0,5,10,15};
    size_t compared=0,changed=0,precision_channels=0;unsigned maximum_error=0;
    for (unsigned rotation=0;rotation<4;++rotation) for (unsigned level=0;level<4;++level) {
        unsigned scale=rotation==3 && level==3?2:1,sw=640*scale,sh=480*scale;
        rx_effects_reset();allocate();cam.flg|=8;sys->fil_no=2;sys->fil_rt=(levels[level]<<4)|rotation;
        tick();tick();clear(sw,sh);const char *error=rx_overlay_present(&draw,&view);check(!error,error);
        glReadPixels(0,0,sw,sh,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(!glGetError(),"filter readback");
        for (unsigned y=0;y<sh;++y) for (unsigned x=0;x<sw;++x) {
            float u,v;uv(rotation,(x+.5f)/sw,(y+.5f)/sh,&u,&v);
            unsigned alpha=(unsigned)floorf(((tex0>>34)&1?sample(atlas,w,h,u,v,3):128)*((levels[level]+1)*8)/128+.0001f);
            size_t pixel=((sh-1-y)*sw+x)*4;
            for (unsigned c=0;c<3;++c) {
                int source=(int)floorf(sample(atlas,w,h,u,v,c)+.0001f);
                int expected=(int)background[c]+(int)floorf((source-(int)background[c])*(float)alpha/128);
                if (expected<0) expected=0;
                if (expected>255) expected=255;
                unsigned difference=(unsigned)abs((int)pixels[pixel+c]-expected);
                if (difference>maximum_error) maximum_error=difference;
                if (difference>2) {
                    /* A hardware bilinear sample can differ by one color/alpha
                       unit from ideal interpolation. Bound both terms before
                       modulation and include final UNORM blend rounding. */
                    int low=255,high=0;
                    for (int da=-1;da<=1;++da) for (int dc=-1;dc<=1;++dc) {
                        int bounded_alpha=(int)alpha+da,bounded_source=source+dc;
                        if (bounded_alpha<0) bounded_alpha=0;
                        if (bounded_alpha>128) bounded_alpha=128;
                        if (bounded_source<0) bounded_source=0;
                        if (bounded_source>255) bounded_source=255;
                        int bound=(int)background[c]+(int)floorf((bounded_source-(int)background[c])*(float)bounded_alpha/128);
                        if (bound<low) low=bound;
                        if (bound>high) high=bound;
                    }
                    check((int)pixels[pixel+c]>=low-1 && (int)pixels[pixel+c]<=high+1,
                          "filter exceeds one-unit texture interpolation and framebuffer blend-rounding bounds");
                    ++precision_channels;
                }
                changed+=pixels[pixel+c]!=background[c];++compared;
            }
            check(pixels[pixel+3]==255,"filter changed framebuffer alpha");
        }
    }
    if (maximum_error>3 || changed<100) fprintf(stderr,"filter GPU error=%u changed=%zu\n",maximum_error,changed);
    check(maximum_error<=3 && changed>100,"original filter orientations/opacity/GLES pixels differ from scalar GS reference");
    free(atlas);free(pixels);rx_particle_close(&draw);rx_egl_close(&egl);
    printf("PASS original filter texture/orientations/alpha/scaled viewport: %zu Quest GPU channel samples, maximum GS error %u/255\n",compared,maximum_error);
    printf("PASS %zu filter channels beyond 2/255 remain inside one-unit texture interpolation and blend-rounding bounds\n",precision_channels);
}
void check_filter_runtime(void)
{
    unsigned flags=cam.flg,selector=sys->fil_no,rate=sys->fil_rt;
    lifecycle();gpu();rx_effects_reset();cam.flg=flags;sys->fil_no=selector;sys->fil_rt=rate;
}

