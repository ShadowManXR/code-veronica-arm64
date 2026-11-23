#include "effect_runtime.h"
#include "random_runtime.h"
#include "android/particle_draw.h"
#include "android_egl.h"
#include "effect.h"
#include "room_effects.h"
#include "room.h"
#include "MdlPut.h"
#include "main.h"
#include "ps2_NaMath.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaFog.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_texture.h"
#include "ps2_dummy.h"
#include "ps2_loadtim2.h"
#include "ps2_pxlconv.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char pixels[640*480*4];
static void check(int ok,const char *why)
{
    if (!ok) { fprintf(stderr,"FAIL original particles: %s\n",why);exit(1); }
}
static void tick(void) { const char *error=rx_effects_tick();check(!error,error); }
static int allocate(unsigned id,unsigned type,float y)
{
    EF_WORK record={.flg=id==124?0x4100001u:1u,.id=(unsigned short)id,.type=(unsigned short)type,
                    .px=0,.py=y,.pz=20,.sx=5,.sy=7,.sz=5};
    int slot=bhSetEffectTb(&record,NULL,NULL,0);
    check(slot>=0,"effect pool allocation");return slot;
}

static void lifecycle(void)
{
    rx_effects_reset();sys->sp_flg|=8;sys->ef_flg=0;sys->gm_flg&=~1u;
    int manager=allocate(115,2,5);
    tick();check(!eff[1].flg&&(eff[manager].flg&0x1000000),"idle manager emitted a particle");
    eff[manager].mode1=1;sys->sp_flg&=~8u;
    tick();check(eff[manager].mode1==1&&!eff[1].flg,"paused effects consumed a trigger");
    sys->sp_flg|=8;sys->gm_flg|=1;tick();
    check(eff[manager].mode1==1&&!eff[1].flg,"loading gate consumed a trigger");
    sys->gm_flg&=~1u;tick();
    O_WRK *child=&eff[1];
    check(!eff[manager].mode1&&child->id==124&&child->mode0==1&&child->ct0==0&&
          child->sx==0.5f&&child->sy==0.7f&&child->sz==0.5f&&child->py==5&&child->gpy==0&&
          child->tex_id==33&&child->bl_src==8&&child->bl_dst==10&&!sys->ef_trsn&&
          (uintptr_t)child->exp0>UINT32_MAX,"original manager spawn/initialization frame differs");
    for (unsigned frame=0;frame<5;++frame) {
        tick();
        check(sys->ef_trsn==1&&sys->ef_trs[0]==child&&child->tv[0].u==frame/16.0f&&
              child->tv[0].v==15/16.0f&&child->tv[3].u==(frame+1)/16.0f&&child->tv[3].v==1,
              "original startup UV sequence differs");
    }
    const float heights[]={4.9f,4.7f,4.4f,4,3.5f,2.9f,2.2f,1.4f,0.5f,0};
    for (unsigned frame=0;frame<10;++frame) {
        tick();check(fabsf(child->py-heights[frame])<0.00001f&&
                     child->tv[0].u==(5+frame%4)/16.0f,"original falling motion/loop differs");
    }
    check(child->mode0==3&&child->ct0==9,"ground contact did not enter finishing phase");
    for (unsigned frame=9;frame<14;++frame) {
        tick();check(child->tv[0].u==frame/16.0f&&sys->ef_trsn==1,"original finishing sequence differs");
    }
    tick();check(!child->flg&&!sys->ef_trsn&&!eff[manager].mode1,"particle expiry/retrigger differs");
    rx_effects_reset();
    for (unsigned i=0;i<512;++i) eff[i].flg=2;
    eff[511]=(O_WRK){.flg=1,.id=115,.type=2,.mode1=1,.sx=5,.sy=7,.sz=5,.py=5,.pz=20};
    tick();check(!eff[511].mode1&&eff[0].flg==2&&!sys->ef_trsn,"original full-pool cue/drop behavior differs");
    rx_effects_reset();
    allocate(124,0,5);tick();
    eff[0].exp0=(unsigned char *)(uintptr_t)1;
    check(rx_effects_tick()!=NULL,"untracked native UV pointer was dereferenced");
    rx_effects_reset();allocate(124,0,5);tick();eff[0].ct0=99;
    check(rx_effects_tick()!=NULL,"invalid UV frame was read");
    rx_effects_reset();allocate(115,0,5);
    check(rx_effects_tick()!=NULL,"unported manager variant was silently accepted");
    rx_effects_reset();
    for (unsigned i=0;i<3;++i) eff[i]=(O_WRK){.flg=1,.id=(unsigned short)(i==0?33:i==1?35:45)};
    tick();check(!eff[0].flg&&!eff[1].flg&&!eff[2].flg,"original texture-only record handlers differ");
    puts("PASS original 115/type-2 trigger, same-frame child initialization, UV phases, gravity, ground contact and expiry");
    puts("PASS original pause/load/full-pool behavior, texture-only records and bounded native animation pointers");
}

static void flame_lifecycle(void)
{
    rx_effects_reset();allocate(9,1,0);O_WRK *flame=&eff[0];
    for (unsigned frame=0;frame<40;++frame) {
        tick();
        check(flame->mode0==1 && flame->ct0==(int)(frame%8+1) &&
              flame->tv[0].u==(frame%8)/16.0f && flame->tv[0].v==13/16.0f &&
              flame->tv[3].u==(frame%8+1)/16.0f && flame->tv[3].v==14/16.0f &&
              flame->tv[0].col==0xffe0e0e0 && flame->bl_src==8 && flame->bl_dst==6 &&
              flame->tex_id==8 && sys->ef_trsn==1 && sys->ef_trs[0]==flame &&
              (uintptr_t)flame->exp0>UINT32_MAX,"original eight-frame flame loop differs");
    }
    int frame=flame->ct0;sys->sp_flg&=~8u;tick();
    check(flame->ct0==frame,"paused flame advanced");sys->sp_flg|=8;
    flame->stflg|=0x1000000;tick();check(!sys->ef_trsn && flame->ct0==frame,"hidden flame advanced");
    flame->stflg=0;
    flame->ct0=9;check(rx_effects_tick()!=NULL,"out-of-bounds flame UV read was accepted");
    flame->ct0=0;flame->exp0=(unsigned char *)(uintptr_t)1;
    check(rx_effects_tick()!=NULL,"untracked flame UV pointer was read");
    rx_effects_reset();allocate(9,2,0);check(rx_effects_tick()!=NULL,"untested flame variant was admitted");
    rx_effects_reset();allocate(9,1,0);flame=&eff[0];flame->flg|=0x80;
    flame->lkwkp=(unsigned char *)(uintptr_t)1;
    check(rx_effects_tick()!=NULL,"invalid flame parent pointer was read");
    BH_PWORK *parent=&ene[127];BH_PWORK previous=*parent;
    static O_WORK joints[2];ML_WORK model={.obj_num=2,.owP=joints};
    *parent=(BH_PWORK){.flg=1,.mlwP=&model,.px=10,.py=20,.pz=30,.ay=16384};
    njUnitMatrix(&joints[1].mtx);njTranslate(&joints[1].mtx,2,3,4);
    flame->lkwkp=(unsigned char *)parent;flame->lkono=1;flame->lox=1;flame->loy=2;flame->loz=3;
    tick();check(fabsf(flame->px-17)<0.00001f && fabsf(flame->py-25)<0.00001f &&
                 fabsf(flame->pz-27)<0.00001f,"flame attachment did not compose joint and actor transforms");
    parent->px+=5;tick();check(fabsf(flame->px-22)<0.00001f,"flame did not follow its moving parent");
    parent->flg=0;parent->mlwP=NULL;tick();check(fabsf(flame->px-22)<0.00001f,"inactive parent changed flame position");
    parent->flg=1;parent->mlwP=&model;flame->lkono=2;
    check(rx_effects_tick()!=NULL,"out-of-range flame joint was read");
    flame->lkono=1;parent->flg|=0x80;check(rx_effects_tick()!=NULL,"unported nested flame link was accepted");
    *parent=previous;
    rx_effects_reset();
    for (unsigned i=0;i<512;++i) allocate(9,1,0);
    tick();check(sys->ef_trsn==512 && sys->ef_trs[511]==&eff[511],"full flame draw queue differs");
    puts("PASS original flame loop, pause/visibility, full queue and bounded native UV/parent/joint state");
    puts("PASS flame joint+actor rotation/translation, moving parent and inactive-parent behavior");
}

static unsigned emitter_uv(unsigned type,unsigned frame,float uv[4])
{
    unsigned count=type<4?18:type==4?19:15;
    if (frame>=count) return count;
    if (type<4) {
        unsigned cell=frame<6?frame:frame<14?4+(frame&1):frame-14;
        uv[0]=cell*5/32.0f;uv[1]=(type<2?0:10)/32.0f+(frame>=14?5/32.0f:0);
        uv[2]=uv[3]=5/32.0f;
    } else {
        static const unsigned u2[]={0,2,5,8,12,18,24,18,24,18,24,0,8,16,24,0,8,16,24};
        static const unsigned u3[]={0,2,5,8,12,18,24,0,8,16,24,0,8,16,24};
        static const unsigned sizes[]={2,3,3,4,6,6,8};
        uv[0]=(type==4?u2[frame]:u3[frame])/32.0f;
        uv[1]=(type==4?(frame<11?0:frame<15?8:16):(frame<7?0:frame<11?8:16))/32.0f;
        uv[2]=uv[3]=(frame<7?sizes[frame]:type==4 && frame<11?sizes[frame&1?5:6]:8)/32.0f;
    }
    return count;
}

static void emitter_lifecycle(void)
{
    for (unsigned type=0;type<7;++type) for (unsigned version=0;version<4;++version) {
        rx_effects_reset();int slot=allocate(125,version+4,500);O_WRK *manager=&eff[slot];
        tick();check(!eff[1].flg,"idle original emitter created a child");
        manager->mode1=(unsigned char)(type+1);sys->sp_flg&=~8u;tick();
        check(manager->mode1==type+1 && !eff[1].flg,"paused emitter consumed a cue");
        sys->sp_flg|=8;sys->gm_flg|=1;tick();
        check(manager->mode1==type+1 && !eff[1].flg,"loading emitter consumed a cue");sys->gm_flg&=~1u;
        rx_ps2_srand(137+type*4+version);float random_x=(float)rx_ps2_rand()/2147483600.0f-0.5f;
        float random_z=(float)rx_ps2_rand()/2147483600.0f-0.5f;rx_ps2_srand(137+type*4+version);
        float y=500,velocity=type==6?1.25f:-0.1f;
        float speed=(type<2?0.015f:type<4?0.02f:0.005f)*5;
        unsigned count=type<4?18:type==4?19:15;
        O_WRK *child=&eff[1];
        for (unsigned frame=0;frame<count;++frame) {
            if (type<5) { if (velocity>-3) velocity-=speed;y+=velocity; }
            else if (type==6) { y+=velocity;velocity*=0.8f; }
            tick();float uv[4];emitter_uv(type,frame,uv);
            unsigned alpha=type<4?255:type==4?64-version*16:128-version*32;
            check(!manager->mode1 && child->id==126 && child->type==type && child->mdlver==version &&
                  child->mode0==1 && child->ct0==(int)(frame+1) && child->tex_id==35 &&
                  child->ani_ct==(int)(type<2?0:type<4?1:2) && child->bl_src==8 && child->bl_dst==6 &&
                  sys->ef_trsn==1 && sys->ef_trs[0]==child && (uintptr_t)child->exp0>UINT32_MAX,
                  "original emitter spawn/page selection/frame dispatch differs");
            check(fabsf(child->px-random_x)<0.000001f && fabsf(child->pz-(20+random_z))<0.00001f &&
                  fabsf(child->py-y)<0.0001f && fabsf(child->yn-velocity)<0.00001f &&
                  fabsf(child->sx-(type<2||type>4?1.25f:type<4?1.0f:1.5f))<0.00001f &&
                  fabsf(child->sy-(type<2||type>4?1.75f:type<4?1.4f:2.1f))<0.00001f && child->sz==5,
                  "original emitter scale/random offset/particle physics differs");
            check(child->tv[0].u==uv[0] && child->tv[0].v==uv[1] &&
                  child->tv[3].u==uv[0]+uv[2] && child->tv[3].v==uv[1]+uv[3] &&
                  child->tv[0].col==(alpha<<24|0xffffffu),"original particle UV table/opacity differs");
        }
        tick();check(!child->flg && !sys->ef_trsn && !manager->mode1,"original emitter particle did not expire at sentinel");
    }
    rx_effects_reset();allocate(126,0,5);tick();eff[0].py=0.1f;tick();
    check(!eff[0].flg && !sys->ef_trsn,"original falling particle did not expire below ground");
    rx_effects_reset();allocate(125,0,500);eff[0].mode1=8;
    check(rx_effects_tick()!=NULL && !eff[1].flg,"invalid child cue created unsupported work");
    for (unsigned type=0;type<7;++type) {
        rx_effects_reset();allocate(126,type,500);tick();eff[0].ct0=99;
        check(rx_effects_tick()!=NULL,"particle UV bounds were not checked");
        eff[0].ct0=0;eff[0].exp0=(unsigned char *)(uintptr_t)1;
        check(rx_effects_tick()!=NULL,"untracked particle UV pointer was read");
    }
    rx_effects_reset();allocate(126,5,500);tick();eff[0].type=0;eff[0].ct0=18;
    check(rx_effects_tick()!=NULL,"changed particle type escaped its initialized UV table bounds");
    rx_effects_reset();allocate(126,7,500);check(rx_effects_tick()!=NULL,"invalid particle type was accepted");
    rx_effects_reset();allocate(126,4,500);eff[0].mdlver=4;
    check(rx_effects_tick()!=NULL,"invalid particle opacity version was accepted");
    rx_effects_reset();
    for (unsigned i=0;i<512;++i) eff[i].flg=2;
    eff[511]=(O_WRK){.flg=1,.id=125,.mode1=1,.sx=5,.sy=7,.sz=5,.py=500,.pz=20};
    tick();check(!eff[511].mode1 && eff[0].flg==2 && !sys->ef_trsn,"full emitter pool did not consume/drop original cue");
    eff[0].flg=0;eff[511].mode1=1;tick();
    check(eff[0].id==126 && !eff[0].mode0 && !sys->ef_trsn,"child in earlier pool slot advanced in its birth frame");
    tick();check(eff[0].mode0==1 && sys->ef_trsn==1,"deferred child did not update next frame");
    rx_effects_reset();
    puts("PASS original 125/126: all seven child types and four opacity versions, UV tables, scales, random offsets, physics and expiry");
    puts("PASS original emitter pause/load/full-pool/slot-order behavior and guarded cue/type/opacity/native UV state");
}

static void retail_flames(void)
{
    ML_WORK *models[]={sys->obwp[6].mlwP,sys->itwp[5].mlwP};
    for (unsigned i=0;i<2;++i) {
        check(models[i] && models[i]->obj_num && !models[i]->owP,"retail effect joint setup");
        models[i]->owP=calloc(models[i]->obj_num,sizeof(O_WORK));check(models[i]->owP!=NULL,"retail joints allocation");
        bhCalcTree(NULL,models[i]);
    }
    rx_effects_reset();RxRoomEffects *table=NULL;const char *error=rx_room_effects_prepare(&table);check(!error,error);
    RxRoomEffectFault fault;error=rx_room_effects_check_live(table,&fault);
    check(!error,"all original opening effect families must now be ready");
    for (unsigned record=2;record<=3;++record) {
        rx_effects_reset();eff[0]=*rx_room_effects_work(table,record);
        eff[0].mtx=(NJS_MATRIX *)eff[0].mtxbuf;eff[0].tvp=eff[0].tv;eff[0].pvp=eff[0].pv;
        tick();check(eff[0].id==9 && eff[0].mode0==1 && sys->ef_trsn==1,"retail flame record did not enter original handler");
        if (record==3) check((eff[0].flg&0x80) && eff[0].lkwkp==(unsigned char *)&sys->obwp[6] &&
                            eff[0].lkono==2,"retail flame lost its authored prop/joint attachment");
    }
    for (unsigned record=5;record<=7;++record) {
        rx_effects_reset();eff[0]=*rx_room_effects_work(table,record);
        eff[0].mtx=(NJS_MATRIX *)eff[0].mtxbuf;eff[0].tvp=eff[0].tv;eff[0].pvp=eff[0].pv;
        eff[0].mode1=1;tick();
        check(eff[1].id==126 && eff[1].mdlver==eff[0].type%4 && !eff[0].mode1,
              "actual opening emitter did not create its original child");
        if (record==5) {
            check((eff[0].flg&0x80) && eff[0].lkwkp==(unsigned char *)&sys->itwp[5],"opening emitter lost item attachment");
            float x=eff[0].px;sys->itwp[5].px+=2;eff[0].mode1=1;tick();
            check(fabsf(eff[0].px-x-2)<0.00001f,"actual linked emitter did not follow item");
            sys->itwp[5].px-=2;
        }
    }
    for (unsigned record=9;record<=11;++record) {
        rx_effects_reset();eff[0]=*rx_room_effects_work(table,record);
        eff[0].mtx=(NJS_MATRIX *)eff[0].mtxbuf;eff[0].tvp=eff[0].tv;eff[0].pvp=eff[0].pv;
        tick();check(eff[0].id==20 && eff[0].ani_ct==(int)record-9 && !sys->ef_trs2dn && eff[0].px==0,
                     "actual opening overlay initialization differs");
        eff[0].mode1=1;tick();check(sys->ef_trs2dn==1 && eff[0].tv[3].x==512 && eff[0].tv[3].y==512,
                                  "actual opening overlay did not submit its original quad");
    }
    rx_effects_reset();eff[0]=*rx_room_effects_work(table,12);tick();
    check(eff[0].id==21 && !sys->ef_fncn && !(sys->cb_flg&0x40),"actual dormant cinema effect changed callback state");
    eff[0].mode1=1;tick();check(sys->ef_fncn==1 && sys->ef_fnc[0]==&eff[0] && (sys->cb_flg&0x40),
                              "actual opening cinema effect did not queue its original callback");
    rx_effects_reset();sys->cb_flg&=~0x40u;
    error=rx_room_effects_publish(table);check(!error,error);
    unsigned camera_flags=cam.flg;unsigned filter=sys->fil_no,rate=sys->fil_rt;
    cam.flg|=8;sys->fil_no=2;sys->fil_rt=0xb1;
    tick();tick();
    check(sys->ef_trsn==2 && sys->ef_trs2dn==1 && !sys->ef_fncn,
          "published opening effects did not queue both flames and the camera filter");
    O_WRK *filter_work=&eff[sys->efid[13]];
    check(filter_work->id==92 && filter_work->mode0==2 && filter_work->tv[0].col==0xbfffffff,
          "published original filter state differs");
    for (unsigned i=0;i<14;++i) check(sys->efid[i]==rx_room_effects_slot(table,i),"published effect mapping changed");
    cam.flg=camera_flags;sys->fil_no=filter;sys->fil_rt=rate;
    rx_effects_reset();rx_room_effects_destroy(table);
    for (unsigned i=0;i<2;++i) { free(models[i]->owP);models[i]->owP=NULL; }
    puts("PASS actual RM_0000 flame records 2/3 and prop 6 joint 2; all three original emitters; all three original overlays; original cinema callback; all 14 effect families published and stepped");
}

static float sample(const uint32_t *atlas,unsigned w,unsigned h,float u,float v,unsigned channel)
{
    float x=u*w-0.5f,y=v*h-0.5f;int ix=(int)floorf(x),iy=(int)floorf(y);
    float fx=x-ix,fy=y-iy,result=0;
    for (int j=0;j<2;++j) for (int i=0;i<2;++i) {
        int px=(ix+i+(int)w)%(int)w,py=(iy+j+(int)h)%(int)h;
        if (gs.ctx[0].clamp&3) px=ix+i<0?0:ix+i>=(int)w?(int)w-1:ix+i;
        if ((gs.ctx[0].clamp>>2)&3) py=iy+j<0?0:iy+j>=(int)h?(int)h-1:iy+j;
        result+=((atlas[py*w+px]>>(channel*8))&255)*(i?fx:1-fx)*(j?fy:1-fy);
    }
    return result;
}

static void gpu(void)
{
    RxEgl egl={0};RxParticleDraw *draw=NULL;
    check(rx_egl_open(&egl,NULL,640,480),"particle EGL surface");
    static NJS_MATRIX camera;njUnitMatrix(&camera);cam.mtx=&camera;njFogDisable();
    gs_reset();
    const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
    rx_effects_reset();int slot=allocate(124,0,0);
    O_WRK *particle=&eff[slot];particle->sx=particle->sy=2;
    tick();tick();
    check(njSetTexture(&sys->ef_tlist)&&njSetTextureNum(sys->ef_tn[33])>=0,"atlas selection");
    uint64_t tex0=gs.ctx[0].tex0;
    unsigned w=1u<<((tex0>>26)&15),h=1u<<((tex0>>30)&15);
    uint32_t *atlas=malloc((size_t)w*h*sizeof(*atlas));check(atlas!=NULL,"atlas readback allocation");
    gs_decode_texture(tex0,gs.texa,w,h,atlas);
    size_t compared=0,lit=0;unsigned maximum_error=0;
    for (unsigned frame=0;frame<3;++frame) {
        if (frame) tick();
        glViewport(0,0,640,480);glDepthMask(GL_TRUE);glClearDepthf(1);
        glClearColor(32/255.0f,32/255.0f,32/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        const char *error=rx_particle_present(&draw,&view);check(!error,error);
        glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(!glGetError(),"particle pixels");
        for (unsigned y=241;y<303;++y) for (unsigned x=289;x<351;++x) {
            float u=particle->tv[0].u+(particle->tv[1].u-particle->tv[0].u)*(x+0.5f-288)/64;
            float v=particle->tv[2].v+(particle->tv[0].v-particle->tv[2].v)*(y+0.5f-240)/64;
            unsigned a=(tex0>>34)&1?(unsigned)floorf(sample(atlas,w,h,u,v,3)+0.0001f):128;
            for (unsigned c=0;c<3;++c) {
                unsigned source=(unsigned)floorf(sample(atlas,w,h,u,v,c)+0.0001f);
                unsigned expected=32+source*a/128;if (expected>255) expected=255;
                unsigned actual=pixels[(y*640+x)*4+c];
                unsigned difference=actual>expected?actual-expected:expected-actual;
                if (difference>maximum_error) maximum_error=difference;
                lit+=actual>32;++compared;
            }
            check(pixels[(y*640+x)*4+3]==255,"particle blend changed framebuffer alpha");
        }
    }
    check(maximum_error<=2&&lit>100,"real atlas billboard differs from scalar GS modulation/additive blend");
    uint32_t saved_palette[1024];memcpy(saved_palette,palbuf,sizeof(saved_palette));
    memset(palbuf,0,sizeof(saved_palette));sys->gm_flg|=4;
    glDepthMask(GL_TRUE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const char *palette_error=rx_particle_present(&draw,&view);check(!palette_error,palette_error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for (size_t i=0;i<sizeof(pixels);i+=4)
        check(pixels[i]==32&&pixels[i+1]==32&&pixels[i+2]==32,"palette change left stale particle pixels");
    memcpy(palbuf,saved_palette,sizeof(saved_palette));sys->gm_flg|=4;
    palette_error=rx_particle_present(&draw,&view);check(!palette_error,palette_error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    size_t restored=0;for (size_t i=0;i<sizeof(pixels);i+=4) restored+=pixels[i]>32||pixels[i+1]>32||pixels[i+2]>32;
    check(restored>100,"restored shared palette did not invalidate the particle atlas cache");
    puts("PASS original palette dirty flag and GPU atlas cache invalidation/restoration");
    glDepthMask(GL_TRUE);glClearDepthf(0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const char *error=rx_particle_present(&draw,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for (size_t i=0;i<sizeof(pixels);i+=4)
        check(pixels[i]==32&&pixels[i+1]==32&&pixels[i+2]==32&&pixels[i+3]==255,"occluded particle drew through depth");
    particle->pz=-20;glClearDepthf(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    error=rx_particle_present(&draw,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    for (size_t i=0;i<sizeof(pixels);i+=4) check(pixels[i]==32&&pixels[i+1]==32&&pixels[i+2]==32,"behind-camera particle drew");
    free(atlas);rx_particle_close(&draw);rx_egl_close(&egl);
    printf("PASS original atlas UV/modulation/additive blend: %zu Quest GPU channel samples, maximum error %u/255\n",compared,maximum_error);
    puts("PASS original particle depth rejection, behind-camera rejection and preserved framebuffer alpha");
}

static void flame_gpu(void)
{
    RxEgl egl={0};RxParticleDraw *draw=NULL;
    check(rx_egl_open(&egl,NULL,640,480),"flame EGL surface");
    static NJS_MATRIX camera;njUnitMatrix(&camera);cam.mtx=&camera;njFogDisable();gs_reset();
    const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
    rx_effects_reset();allocate(9,1,0);O_WRK *flame=&eff[0];flame->sx=flame->sy=2;
    tick();check(njSetTexture(&sys->ef_tlist)&&njSetTextureNum(sys->ef_tn[8])>=0,"flame atlas selection");
    uint64_t tex0=gs.ctx[0].tex0;unsigned w=1u<<((tex0>>26)&15),h=1u<<((tex0>>30)&15);
    uint32_t *atlas=malloc((size_t)w*h*sizeof(*atlas));check(atlas!=NULL,"flame atlas allocation");
    gs_decode_texture(tex0,gs.texa,w,h,atlas);
    unsigned maximum_error=0;size_t compared=0,changed=0;
    for (unsigned frame=0;frame<8;++frame) {
        if (frame) tick();
        glViewport(0,0,640,480);glDepthMask(GL_TRUE);glClearDepthf(1);
        glClearColor(32/255.0f,32/255.0f,32/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        const char *error=rx_particle_present(&draw,&view);check(!error,error);
        glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(!glGetError(),"flame pixels");
        for (unsigned y=241;y<303;++y) for (unsigned x=289;x<351;++x) {
            float u=flame->tv[0].u+(flame->tv[1].u-flame->tv[0].u)*(x+0.5f-288)/64;
            float v=flame->tv[2].v+(flame->tv[0].v-flame->tv[2].v)*(y+0.5f-240)/64;
            unsigned alpha=(tex0>>34)&1?(unsigned)floorf(sample(atlas,w,h,u,v,3)+0.0001f):128;
            for (unsigned c=0;c<3;++c) {
                int source=(int)floorf(sample(atlas,w,h,u,v,c)*112/128+0.0001f);
                int expected=32+(int)floorf((source-32)*(float)alpha/128);
                if (expected<0) expected=0;if (expected>255) expected=255;
                unsigned actual=pixels[(y*640+x)*4+c],difference=(unsigned)abs((int)actual-expected);
                if (difference>maximum_error) maximum_error=difference;
                changed+=actual!=32;++compared;
            }
            check(pixels[(y*640+x)*4+3]==255,"flame blend changed framebuffer alpha");
        }
    }
    if (maximum_error>2 || changed<100) fprintf(stderr,"flame GPU max error=%u changed=%zu tex0=%llx\n",maximum_error,changed,(unsigned long long)tex0);
    check(maximum_error<=2 && changed>100,"original flame atlas/modulation/alpha blend differs from scalar GS reference");
    unsigned char *reference=malloc(sizeof(pixels));check(reference!=NULL,"flame attachment reference allocation");
    memcpy(reference,pixels,sizeof(pixels));
    BH_PWORK *parent=&ene[127],previous=*parent;O_WORK joint={0};ML_WORK model={.obj_num=1,.owP=&joint};
    njUnitMatrix(&joint.mtx);njTranslate(&joint.mtx,2,0,0);
    *parent=(BH_PWORK){.flg=1,.mlwP=&model,.px=-2,.pz=20};
    flame->flg|=0x80;flame->lkwkp=(unsigned char *)parent;flame->lkono=0;flame->ct0=7;tick();
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const char *error=rx_particle_present(&draw,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(!memcmp(reference,pixels,sizeof(pixels)),"linked and unlinked flame at identical world position differ");
    parent->px+=1;flame->ct0=7;tick();glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    error=rx_particle_present(&draw,&view);check(!error,error);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(memcmp(reference,pixels,sizeof(pixels))!=0,"moving flame parent did not move GPU billboard");
    *parent=previous;
    free(reference);free(atlas);rx_particle_close(&draw);rx_egl_close(&egl);
    printf("PASS original flame atlas/color/GS alpha blend: %zu Quest GPU channel samples, maximum error %u/255\n",compared,maximum_error);
    puts("PASS identical linked/unlinked flame pixels and moving-parent GPU attachment");
}

static void emitter_gpu(void)
{
    RxEgl egl={0};RxParticleDraw *draw=NULL;
    check(rx_egl_open(&egl,NULL,640,480),"emitter EGL surface");
    static NJS_MATRIX camera;njUnitMatrix(&camera);cam.mtx=&camera;njFogDisable();gs_reset();
    const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
    unsigned maximum_error=0;size_t compared=0,changed=0;
    unsigned page_draws[3]={0};
    for (unsigned type=0;type<7;++type) for (unsigned version=0;version<4;++version) {
        rx_effects_reset();allocate(126,type,500);O_WRK *child=&eff[0];child->mdlver=(unsigned char)version;
        unsigned count=type<4?18:type==4?19:15;
        const unsigned frames[]={0,5,count-1};
        for (unsigned index=0;index<3;++index) {
            child->py=500;child->ct0=(int)frames[index];tick();
            child->py=0;child->sx=child->sy=2;
            check(sys->ef_trsn==1,"original particle was not queued for GPU reference check");
            unsigned page=(unsigned)child->ani_ct;++page_draws[page];
            check(njSetTexture(&sys->ef_tlist)&&njSetTextureNum(sys->ef_tn[35]+page)>=0,"emitter texture page selection");
            uint64_t tex0=gs.ctx[0].tex0;unsigned w=1u<<((tex0>>26)&15),h=1u<<((tex0>>30)&15);
            uint32_t *atlas=malloc((size_t)w*h*sizeof(*atlas));check(atlas!=NULL,"emitter atlas allocation");
            gs_decode_texture(tex0,gs.texa,w,h,atlas);
            glViewport(0,0,640,480);glDepthMask(GL_TRUE);glClearDepthf(1);
            glClearColor(32/255.0f,32/255.0f,32/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            const char *error=rx_particle_present(&draw,&view);check(!error,error);
            glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(!glGetError(),"emitter GPU pixels");
            for (unsigned y=209;y<271;++y) for (unsigned x=289;x<351;++x) {
                float u=child->tv[0].u+(child->tv[1].u-child->tv[0].u)*(x+0.5f-288)/64;
                float ty=((240-(y+0.5f))/32-child->tv[0].y)/(child->tv[2].y-child->tv[0].y);
                float v=child->tv[0].v+(child->tv[2].v-child->tv[0].v)*ty;
                unsigned vertex_alpha=((child->tv[0].col>>24)+1)>>1;
                float texture_alpha=(tex0>>34)&1?sample(atlas,w,h,u,v,3):128;
                unsigned alpha=(unsigned)floorf(texture_alpha*vertex_alpha/128+0.0001f);
                for (unsigned c=0;c<3;++c) {
                    int source=(int)floorf(sample(atlas,w,h,u,v,c)+0.0001f);
                    int expected=32+(int)floorf((source-32)*(float)alpha/128);
                    if (expected<0) expected=0;
                    if (expected>255) expected=255;
                    unsigned actual=pixels[(y*640+x)*4+c],difference=(unsigned)abs((int)actual-expected);
                    if (difference>maximum_error) maximum_error=difference;
                    changed+=actual!=32;++compared;
                }
                check(pixels[(y*640+x)*4+3]==255,"emitter draw changed framebuffer alpha");
            }
            free(atlas);
        }
    }
    if (maximum_error>2 || changed<100) fprintf(stderr,"emitter GPU error=%u changed=%zu\n",maximum_error,changed);
    check(maximum_error<=2 && changed>100 && page_draws[0] && page_draws[1] && page_draws[2],
          "original emitter page/UV orientation/opacity/GS blend differs from scalar reference");
    rx_particle_close(&draw);rx_egl_close(&egl);
    printf("PASS original 126 pages, both UV orientations and four opacity versions: %zu Quest GPU channel samples, maximum error %u/255\n",compared,maximum_error);
}

void check_overlay_runtime(void);
void check_cinema_runtime(void);
void check_filter_runtime(void);

void check_particle_runtime(const char *path,const char *common_path)
{
    static ROM_WORK room;static O_WRK props[32],items[32];static NJS_MATRIX stack[128];
    _Make_SinTable();njInitMatrix(stack,128,0);
    FILE *file=fopen(path,"rb");check(file&&!fseek(file,0,SEEK_END),"open room fixture");
    long bytes=ftell(file);check(bytes>0&&!fseek(file,0,SEEK_SET),"room fixture size");
    void *rdt=malloc((size_t)bytes);check(rdt&&fread(rdt,1,(size_t)bytes,file)==(size_t)bytes,"read room fixture");fclose(file);
    NJS_TEXMEMLIST *pool=calloc(128,sizeof(*pool));
    Ps2_tex_buff=aligned_alloc(64,10*1024*1024);Ps2_PXLCONV=aligned_alloc(64,1024*1024);
    check(pool&&Ps2_tex_buff&&Ps2_PXLCONV,"particle texture work allocation");njInitTexture(pool,128);
    rom=&room;sys->obwp=props;sys->itwp=items;sys->rdtp=rdt;sys->rdtsz=(unsigned)bytes;
    sys->ef_tlist=(NJS_TEXLIST){sys->ef_tex,0};sys->ef_ct=sys->ef_extn=0;sys->ef_pbnk=32;
    FILE *common=fopen(common_path,"rb");check(common && !fseek(common,0,SEEK_END),"open common effect fixture");
    long common_bytes=ftell(common);check(common_bytes>0 && !fseek(common,0,SEEK_SET),"common effect fixture size");
    unsigned char *common_data=malloc((size_t)common_bytes);
    check(common_data && fread(common_data,1,(size_t)common_bytes,common)==(size_t)common_bytes,"read common effect fixture");fclose(common);
    RxEngineEffects *effects=NULL;const char *common_error=rx_engine_effects_load(common_data,(size_t)common_bytes,&effects);
    free(common_data);check(!common_error,common_error);
    common_error=rx_effects_load_common(effects);check(!common_error,common_error);
    check(sys->ef_ctb==40 && sys->ef_tn[8]==25,"original common effect texture mapping differs");
    puts("PASS all 40 original common effect textures, animation offsets and palette banks");
    bhSetRDT();check(!sys->error&&rom->mdl.objP,"particle room load");
    static ATR_WORK floor={.flg=1,.type=7,.px=-100,.pz=-100,.w=200,.d=200};
    ATR_WORK *walls=rom->walp;int count=rom->wal_n;rom->walp=&floor;rom->wal_n=1;
    check(sys->gm_flg&4,"original room loader did not dirty the shared palette");
    check(Send_1024_Clut_data(NULL,0x3ff0)<0 && Send_1024_Clut_data(palbuf,0x3ff1)<0,
          "invalid shared-palette transport accepted");
    const char *palette_error=rx_effects_sync_palette();check(!palette_error,palette_error);
    check(!(sys->gm_flg&4),"shared-palette dirty flag was not consumed");
    for (unsigned group=0;group<4;++group) for (unsigned bank=0;bank<16;++bank)
        for (unsigned color=0;color<16;++color)
            check(gs_read_pixel(0,0x3ff0+group*4,1,(bank&1)*8+color%8,(bank/2)*2+color/8)==
                  palbuf[group*256+bank*16+color],"original shared palette bank layout differs");
    puts("PASS original shared-palette transport: all 1024 colors and 64 bank tiles");
    lifecycle();gpu();flame_lifecycle();emitter_lifecycle();retail_flames();flame_gpu();emitter_gpu();check_overlay_runtime();check_cinema_runtime();check_filter_runtime();
    rx_effects_reset();rom->walp=walls;rom->wal_n=count;
    njReleaseTexture(&sys->ef_tlist);bhInitRoom();
    rx_engine_effects_destroy(effects);
    free(pool);free(Ps2_tex_buff);free(Ps2_PXLCONV);free(rdt);
    Ps2_tex_buff=Ps2_PXLCONV=NULL;rom=NULL;sys->rdtp=NULL;
}

