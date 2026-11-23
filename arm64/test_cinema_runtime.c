#include "effect_callbacks.h"
#include "effect_runtime.h"
#include "android/particle_draw.h"
#include "android_egl.h"
#include "effect.h"
#include "effsub1.h"
#include "main.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaSystem.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok,const char *why)
{
    if (!ok) { fprintf(stderr,"FAIL original cinema: %s\n",why);exit(1); }
}
static void tick(void) { const char *error=rx_effects_tick();check(!error,error); }
static O_WRK *allocate(void)
{
    EF_WORK record={.flg=1,.id=21,.sx=4,.sy=4,.sz=4};
    int slot=bhSetEffectTb(&record,NULL,NULL,0);check(slot>=0,"original effect allocation");return &eff[slot];
}
static void collect(RxEffectCallbacks *batch)
{
    const char *error=rx_effect_callbacks_collect(batch);check(!error,error);
}
static void check_polygons(const RxEffectCallbacks *batch)
{
    const float top[]={-40,40,440,376},bottom[]={40,104,520,440};
    const unsigned alpha[4][4]={{128,128,128,128},{128,128,0,0},{128,128,128,128},{0,0,128,128}};
    check(batch->count==4 && (uintptr_t)batch->polygons>UINT32_MAX,"native cinema polygon count/address differs");
    for (unsigned i=0;i<4;++i) for (unsigned j=0;j<4;++j) {
        const RxScreenPolygon *polygon=&batch->polygons[i];
        check(polygon->position[j][0]==1728+(j==1||j==2?640:0) &&
              polygon->position[j][1]==1808+(j<2?top[i]:bottom[i]) && polygon->position[j][2]==-1,
              "original Ninja polygon packing changed cinema coordinates/priority");
        check(!polygon->color[j][0] && !polygon->color[j][1] && !polygon->color[j][2] &&
              polygon->color[j][3]==alpha[i][j],"original cinema gradient alpha conversion differs");
    }
    check(gs.ctx[0].alpha==0x44 && !gs.pabe,"original cinema blend state did not reach native GS registers");
}
static void lifecycle(void)
{
    rx_effects_reset();sys->cb_flg|=0x1040;O_WRK *op=allocate();tick();
    check(!sys->ef_fncn && !(sys->cb_flg&0x40) && (sys->cb_flg&0x1000) &&
          (op->flg&0x1000000) && !op->func,"dormant original cinema state differs");
    op->mode1=1;tick();check(sys->ef_fncn==1 && sys->ef_fnc[0]==op && op->func==bhDraw021 &&
                           (sys->cb_flg&0x40) && (op->flg&0x1000000),"original callback activation differs");
    RxEffectCallbacks batch={0};collect(&batch);check_polygons(&batch);
    RxScreenPolygon *saved=batch.polygons;
    sys->ef_fnc[0]=(O_WRK *)(uintptr_t)1;
    check(rx_effect_callbacks_collect(&batch)!=NULL && batch.polygons==saved && batch.count==4,
          "invalid native callback work replaced the completed polygon batch");
    sys->ef_fnc[0]=op;op->func=(void (*)(void *))(uintptr_t)1;
    check(rx_effects_tick()!=NULL && batch.polygons==saved,"untrusted callback pointer was admitted");
    op->func=bhDraw021;tick();sys->sp_flg&=~8u;op->mode1=0;tick();
    check(sys->ef_fncn==1 && (sys->cb_flg&0x40),"paused cinema did not retain its original draw queue");
    collect(&batch);check_polygons(&batch);sys->sp_flg|=8;tick();
    check(!sys->ef_fncn && !(sys->cb_flg&0x40) && (sys->cb_flg&0x1000),"cinema cue did not disable its original flag/queue");
    collect(&batch);check(!batch.count && !batch.polygons,"empty callback collection retained polygons");
    op->mode1=1;tick();op->stflg|=0x1000000;tick();
    check(!sys->ef_fncn && (sys->cb_flg&0x40),"hidden callback task changed original skip semantics");
    rx_effects_reset();
    for (unsigned i=0;i<512;++i) allocate()->mode1=1;
    tick();check(sys->ef_fncn==512 && sys->ef_fnc[511]==&eff[511],"full original callback queue differs");
    collect(&batch);check(batch.count==2048 && batch.polygons[2047].position[3][1]==2248,
                          "full native callback polygon transport differs");
    saved=batch.polygons;sys->ef_fncn=513;
    check(rx_effect_callbacks_collect(&batch)!=NULL && batch.polygons==saved,"invalid callback count was read");
    rx_effect_callbacks_destroy(&batch);rx_effects_reset();
    unsigned error=sys->error;check(rx_primitive_2d(0,NULL,0)<0 && (sys->error&0x80000000u),
                                   "unbound original 2D transport silently succeeded");sys->error=error;
    error=sys->error;uint64_t alpha=gs.ctx[0].alpha;njColorBlendingModeSys(99,6);
    check((sys->error&0x80000000u) && gs.ctx[0].alpha==alpha,"native blend lookup bounds did not preserve valid registers");sys->error=error;
    puts("PASS original cinema cue/flags, paused/hidden queues, bounded native callback identities and atomic polygon collection");
    puts("PASS original bhDraw021/Ninja 2D geometry, alpha, priority and GS blend; 512 callbacks/2048 owned polygons");
}
static void clear(unsigned width,unsigned height,float depth)
{
    glViewport(0,0,width,height);glDepthMask(GL_TRUE);glClearDepthf(depth);
    glClearColor(96/255.0f,160/255.0f,224/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
}
static void present(RxParticleDraw **draw,const NJS_CNK_VIEW *view)
{
    const char *error=rx_callback_present(draw,view);check(!error,error);check(!glGetError(),"cinema GLES error");
}
static void gpu(void)
{
    RxEgl egl={0};RxParticleDraw *draw=NULL;
    check(rx_egl_open(&egl,NULL,1280,960),"cinema EGL surface");gs_reset();
    rx_effects_reset();allocate()->mode1=1;tick();
    const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
    unsigned char *pixels=malloc(1280*960*4);check(pixels!=NULL,"cinema GPU readback allocation");
    const unsigned background[]={96,160,224};size_t compared=0;unsigned maximum_error=0;
    for (unsigned scale=1;scale<=2;++scale) {
        unsigned w=640*scale,h=480*scale;clear(w,h,1);present(&draw,&view);
        glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(!glGetError(),"cinema readback");
        for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
            float screen_y=(y+.5f)/scale,alpha=0;
            if (screen_y<40 || screen_y>=440) alpha=128;
            else if (screen_y<104) alpha=floorf(2*(104-screen_y));
            else if (screen_y>=376) alpha=floorf(2*(screen_y-376));
            size_t pixel=((h-1-y)*w+x)*4;
            for (unsigned c=0;c<3;++c) {
                int expected=(int)floorf(background[c]*(1-alpha/128));
                unsigned error=(unsigned)abs((int)pixels[pixel+c]-expected);
                if (error>maximum_error) maximum_error=error;++compared;
            }
            check(pixels[pixel+3]==255,"cinema changed framebuffer alpha");
        }
    }
    if (maximum_error>2) fprintf(stderr,"cinema maximum GS error=%u\n",maximum_error);
    check(maximum_error<=2,"original cinema bars/gradient/clipping differ from independent GS equation");
    NJS_CNK_VIEW distant=view;distant.near_z=.1f;clear(640,480,.1f);present(&draw,&distant);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for (size_t i=0;i<640*480*4;i+=4)
        check(pixels[i]==96 && pixels[i+1]==160 && pixels[i+2]==224,"cinema bypassed original GS depth rejection");
    static NJS_MATRIX camera;njUnitMatrix(&camera);cam.mtx=&camera;
    EF_WORK flame={.flg=1,.id=9,.type=1,.pz=20,.sx=2,.sy=2,.sz=2};
    check(bhSetEffectTb(&flame,NULL,NULL,0)>=0,"cinema-to-3D flame allocation");tick();
    RxParticleDraw *fresh=NULL;clear(640,480,1);
    const char *error=rx_particle_present(&fresh,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    unsigned char *reference=malloc(640*480*4);check(reference!=NULL,"cinema-to-3D reference allocation");
    memcpy(reference,pixels,640*480*4);present(&draw,&view);clear(640,480,1);
    error=rx_particle_present(&draw,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(!memcmp(reference,pixels,640*480*4),"solid cinema shader state leaked into textured 3D drawing");
    /* A second reference clears only depth after the cinema draw. Equal
       pixels prove the cinema did not leave depth that blocks later geometry. */
    eff[1].py=11;clear(640,480,1);present(&draw,&distant);
    glDepthMask(GL_TRUE);glClearDepthf(1);glClear(GL_DEPTH_BUFFER_BIT);
    error=rx_particle_present(&draw,&distant);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,reference);
    clear(640,480,1);present(&draw,&distant);error=rx_particle_present(&draw,&distant);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(!memcmp(reference,pixels,640*480*4),"cinema wrote depth that occluded later textured geometry");
    EF_WORK overlay={.flg=1,.id=20,.type=45,.sx=2,.sy=2,.sz=4};
    int slot=bhSetEffectTb(&overlay,NULL,NULL,0);check(slot>=0,"cinema-to-overlay allocation");tick();
    eff[slot].mode1=1;eff[slot].px=96;eff[slot].py=64;tick();
    clear(640,480,1);error=rx_overlay_present(&fresh,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,reference);
    present(&draw,&view);clear(640,480,1);error=rx_overlay_present(&draw,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(!memcmp(reference,pixels,640*480*4),"solid cinema shader state leaked into textured screen overlays");
    free(reference);free(pixels);rx_particle_close(&fresh);rx_particle_close(&draw);rx_egl_close(&egl);
    printf("PASS original cinema clipping/gradient/alpha: %zu Quest GPU channel samples, maximum GS error %u/255\n",compared,maximum_error);
    puts("PASS cinema depth rejection/no writes and exact restoration of textured 3D/screen renderer state");
}
void check_cinema_runtime(void)
{
    unsigned cb=sys->cb_flg;lifecycle();gpu();rx_effects_reset();sys->cb_flg=cb;
}
