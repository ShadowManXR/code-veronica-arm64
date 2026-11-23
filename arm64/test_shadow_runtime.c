#include "room_engine.h"
#include "shadow_runtime.h"
#include "android/shadow_draw.h"
#include "android_egl.h"
#include "effect.h"
#include "main.h"
#include "ps2_NaMath.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaFog.h"
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char before[640*480*4],after[sizeof(before)];
static void check(int ok,const char *message)
{
    if (!ok) { fprintf(stderr,"FAIL shadow runtime: %s\n",message); exit(1); }
}
static void read_pixels(unsigned char *pixels)
{
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(glGetError()==GL_NO_ERROR,"GPU readback failed");
}
static void plane(float z)
{
    const float near_z=2,far_z=20000;
    float clip=((far_z+near_z)*z-2*far_z*near_z)/((far_z-near_z)*z);
    glDepthMask(GL_TRUE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0.75f,0.5f,0.25f,1); glClearDepthf(clip*0.5f+0.5f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    read_pixels(before);
}

void check_shadow_runtime(const char *path)
{
    FILE *file=fopen(path,"rb");
    check(file && !fseek(file,0,SEEK_END),"open common effect fixture");
    long bytes=ftell(file);
    check(bytes>0 && !fseek(file,0,SEEK_SET),"fixture size");
    void *data=malloc((size_t)bytes);
    check(data && fread(data,1,(size_t)bytes,file)==(size_t)bytes,"read effect fixture");
    fclose(file);
    RxEngineEffects *effects=NULL;
    const char *error=rx_engine_effects_load(data,(size_t)bytes,&effects);
    check(!error,error); free(data);
    sys->efm[1]=*rx_engine_effect_model(effects,1);
    static BH_PWORK actor;
    static O_WORK bones[4];
    static ML_WORK model;
    static ROM_WORK room;
    static ATR_WORK floor;
    static NJS_MATRIX matrices[16];
    _Make_SinTable(); njInitMatrix(matrices,16,0);
    plp=&actor; rom=&room;
    floor=(ATR_WORK){.flg=1,.type=7,.px=-100,.pz=-100,.w=200,.d=200};
    room.walp=&floor; room.wal_n=1;
    model=(ML_WORK){.obj_num=4,.owP=bones}; actor.mlwP=&model;
    actor.flg=1; actor.px=10; actor.py=1; actor.pz=20;
    njUnitMatrix(&bones[1].mtx); bones[1].mtx[13]=2;
    sys->pt_flg=3;
    sys->sp_flg|=8;
    rx_shadows_reset();
    int slot=bhSetShadow(NULL,(unsigned char *)&actor,1,4.5f,4,3.5f);
    O_WRK *shadow=&eff[0];
    check(slot==0 && shadow->lkwkp==(unsigned char *)&actor && shadow->mlwP==&sys->efm[1] &&
          shadow->mtx==(void *)shadow->mtxbuf && (uintptr_t)shadow->lkwkp>UINT32_MAX &&
          shadow->jno[0]==-1 && shadow->sx==4.5f && shadow->sy==4 && shadow->sz==3.5f,
          "original constructor lost native work pointers or parameters");
    error=rx_shadows_tick(); check(!error,error);
    check(shadow->mode0==1 && sys->ef_mdfn==0 && shadow->px==10 && shadow->py==3 && shadow->pz==20,
          "original first update or native world-space attachment differs");
    error=rx_shadows_tick(); check(!error,error);
    check(shadow->py==0 && sys->ef_mdfn==1 && sys->ef_mdf[0]==shadow,
          "original ground query or native draw queue differs");
    actor.stflg|=0x1000000;
    check(!rx_shadows_tick() && (shadow->flg&0x1000000) && !sys->ef_mdfn,"hidden actor retained a shadow");
    actor.stflg=0;
    check(!rx_shadows_tick() && !(shadow->flg&0x1000000) && sys->ef_mdfn==1,"shown actor lost shadow");
    sys->pt_flg=0;
    check(!rx_shadows_tick() && !sys->ef_mdfn,"original shadow visibility options ignored");
    sys->pt_flg=3; actor.flg=0;
    check(!rx_shadows_tick() && !shadow->flg && !sys->ef_mdfn,"inactive actor retained effect work");
    for (unsigned i=0;i<512;++i) eff[i].flg=3;
    check(bhSetShadow(NULL,(unsigned char *)&actor,1,1,1,1)==-1,"exhausted original pool accepted shadow");
    rx_shadows_reset(); actor.flg=1;
    check(bhSetShadow(NULL,(unsigned char *)&actor,1,1,1,1)==0,"pool reuse failed");
    shadow->lkwkp=(unsigned char *)(uintptr_t)1;
    check(rx_shadows_tick()!=NULL,"invalid native actor pointer was accessed");
    shadow->lkwkp=(unsigned char *)&actor;
    shadow->flg&=~0x200000u;
    check(rx_shadow_validate(shadow)!=NULL,"non-world-space modifier was treated as render-ready");
    shadow->flg|=0x200000;
    actor.flg|=0x80;
    check(rx_shadow_validate(shadow)!=NULL,"pending linked-parent transform was treated as ready");
    actor.flg=1;
    shadow->id=7;
    check(rx_shadows_tick()!=NULL,"unported effect update was silently accepted");
    puts("PASS original shadow constructor, native world attachment, ground update, visibility, pool reuse and teardown");

    RxEgl egl={0}; RxShadowDraw *draw=NULL;
    check(rx_egl_open(&egl,NULL,640,480),"create offscreen surface");
    static NJS_MATRIX camera;
    njUnitMatrix(&camera); cam.mtx=&camera; njFogDisable();
    const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
    rx_shadows_reset();
    check(bhSetShadow(NULL,(unsigned char *)&actor,1,4,4,4)==0,"GPU shadow allocation");
    shadow->px=shadow->py=0; shadow->pz=20;
    shadow->flg&=~0x1000000u;
    sys->ef_mdfn=1; sys->ef_mdf[0]=shadow;
    glViewport(0,0,640,480);
    plane(20);
    error=rx_shadow_present(effects,&draw,&view); check(!error,error);
    read_pixels(after);
    size_t changed=0;
    for (size_t i=0;i<sizeof(before);i+=4) {
        if (!memcmp(before+i,after+i,4)) continue;
        ++changed;
        /* Ps2ShadowMain0 adds 32; Main1 subtracts 32. For one closed
           volume the surviving mask is 32. Ps2ShadowDraw uses tint
           floor(255*3/16)=47, MODULATE /128 and reverse subtraction. */
        check(before[i]-after[i]==11 && before[i+1]-after[i+1]==11 &&
              before[i+2]-after[i+2]==11 && before[i+3]==after[i+3],
              "shadow pixel differs from original GS mask/composite equation");
    }
    check(changed>1000 && changed<640*480/2,"shadow volume has no bounded pixel coverage");
    for (unsigned test=0;test<3;++test) {
        plane(test==0?10:test==1?30:20);
        if (test==2) shadow->pz=1;
        error=rx_shadow_present(effects,&draw,&view); check(!error,error);
        read_pixels(after);
        check(!memcmp(before,after,sizeof(before)),"depth or near-plane rejection left shadow pixels");
    }
    shadow->pz=20;
    /* The theater uses D24; EGL's pbuffer uses packed D24/S8. Check both. */
    GLuint fbo,texture,depth;
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,640,480,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glGenRenderbuffers(1,&depth); glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,640,480);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"theater depth target incomplete");
    plane(20); error=rx_shadow_present(effects,&draw,&view); check(!error,error);
    read_pixels(after);
    GLint restored=0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&restored);
    check(restored==(GLint)fbo && memcmp(before,after,sizeof(before)),"theater depth draw or target restore failed");
    rx_shadow_close(&draw);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&texture); glDeleteRenderbuffers(1,&depth);
    rx_egl_close(&egl);
    rx_shadows_reset(); memset(&sys->efm[1],0,sizeof(sys->efm[1]));
    rx_engine_effects_destroy(effects); plp=NULL; rom=NULL;
    printf("PASS original modifier-volume GS blend equation on %zu Quest GPU pixels; D24 and D24/S8 depth\n",changed);
    puts("PASS volume depth cancellation, near-plane rejection and framebuffer restoration");
}
