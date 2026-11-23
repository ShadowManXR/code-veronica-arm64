#include "effect_runtime.h"
#include "android/particle_draw.h"
#include "android_egl.h"
#include "effect.h"
#include "main.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_NaDraw2D.h"
#include "ps2_NaMatrix.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok,const char *why)
{
    if (!ok) { fprintf(stderr,"FAIL original overlay: %s\n",why);exit(1); }
}
static void tick(void) { const char *error=rx_effects_tick();check(!error,error); }
static O_WRK *allocate(unsigned page)
{
    EF_WORK record={.flg=1,.id=20,.type=45,.px=30,.py=20,.pz=10,.sx=4,.sy=4,.sz=4};
    int slot=bhSetEffectTb(&record,NULL,NULL,(int)page);check(slot>=0,"native overlay allocation");return &eff[slot];
}
static void uv_rect(O_WRK *op,float u,float v,float w,float h)
{
    for (unsigned i=0;i<4;++i) { op->tv[i].u=u+(i&1?w:0);op->tv[i].v=v+(i&2?h:0); }
}
static void lifecycle(void)
{
    for (unsigned page=0;page<3;++page) {
        rx_effects_reset();O_WRK *op=allocate(page);tick();
        check(op->mode0==1 && op->tex_id==45 && op->ani_ct==(int)page && op->gidx==bhGetGidx(op) &&
              op->px==0 && op->py==0 && op->pz==0 && (op->flg&0x1000000) && !sys->ef_trs2dn &&
              op->tv[3].u==1 && op->tv[3].v==1 && op->tv[0].col==0xffe0e0e0 && op->bl_src==8 && op->bl_dst==6,
              "original initialization/page/global texture identity differs");
        tick();check(!sys->ef_trs2dn,"inactive overlay was queued");
        op->mode1=1;op->px=12.25f;op->py=34.5f;op->sx=5;op->sy=3.75f;uv_rect(op,.25f,.125f,.5f,.75f);tick();
        check(sys->ef_trs2dn==1 && sys->ef_trs2d[0]==op && !(op->flg&0x1000000) &&
              op->tv[0].x==12.25f && op->tv[0].y==34.5f && op->tv[3].x==332.25f &&
              op->tv[3].y==394.5f && op->tv[3].z==.25f,"original cropped screen rectangle/depth differs");
        sys->sp_flg&=~8u;op->px=20;tick();check(op->tv[0].x==12.25f,"paused overlay advanced");
        sys->sp_flg|=8;tick();check(op->tv[0].x==20,"overlay did not resume");
        op->stflg|=0x1000000;tick();check(!sys->ef_trs2dn,"hidden overlay was queued");
        op->stflg=0;op->mode1=0;tick();check(!sys->ef_trs2dn && (op->flg&0x1000000),"overlay visibility cue differs");
        op->mode1=1;op->sx=-5;tick();check(op->tv[3].x==-300,"mirrored screen rectangle differs");
        op->gidx^=1;check(rx_effects_tick()!=NULL,"stale cached global texture identity was accepted");
    }
    rx_effects_reset();allocate(3);check(rx_effects_tick()!=NULL,"absent overlay page was read");
    rx_effects_reset();O_WRK *op=allocate(0);op->type=46;check(rx_effects_tick()!=NULL,"unported overlay family was admitted");
    rx_effects_reset();op=allocate(0);tick();op->sz=0;tick();op->mode1=1;
    check(rx_effects_tick()!=NULL,"active overlay divided by zero");
    rx_effects_reset();op=allocate(0);tick();op->tv[0].u=NAN;
    check(rx_effects_tick()!=NULL,"nonfinite overlay UV entered original handler");
    rx_effects_reset();for (unsigned i=0;i<130;++i) allocate(i%3);tick();
    for (unsigned i=0;i<128;++i) eff[i].mode1=1;
    tick();check(sys->ef_trs2dn==128 && sys->ef_trs2d[127]==&eff[127],"full original overlay queue differs");
    eff[128].mode1=1;check(rx_effects_tick()!=NULL && sys->ef_trs2dn==128,"overlay queue overflow was not stopped");
    rx_effects_reset();
    puts("PASS original overlay init/cached texture pages, cropped/mirrored coordinates, visibility, pause and reciprocal depth");
    puts("PASS bounded overlay cue/UV/texture state and full 128-entry queue with inactive trailing records");
}
static float sample(const uint32_t *atlas,unsigned w,unsigned h,float u,float v,unsigned channel,int nearest)
{
    float x=u*w-.5f,y=v*h-.5f;int ix=(int)floorf(x),iy=(int)floorf(y);
    float fx=x-ix,fy=y-iy,result=0;
    if (nearest) { ix=(int)floorf(u*w);iy=(int)floorf(v*h);fx=fy=0; }
    for (int j=0;j<2;++j) for (int i=0;i<2;++i) {
        int px=(ix+i+(int)w)%(int)w,py=(iy+j+(int)h)%(int)h;
        if (gs.ctx[0].clamp&3) px=ix+i<0?0:ix+i>=(int)w?(int)w-1:ix+i;
        if ((gs.ctx[0].clamp>>2)&3) py=iy+j<0?0:iy+j>=(int)h?(int)h-1:iy+j;
        result+=((atlas[py*w+px]>>(channel*8))&255)*(i?fx:1-fx)*(j?fy:1-fy);
    }
    return result;
}
static void clear(float depth)
{
    glDepthMask(GL_TRUE);glClearDepthf(depth);glClearColor(32/255.0f,32/255.0f,32/255.0f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
}
static void present(RxParticleDraw **draw,const NJS_CNK_VIEW *view)
{
    const char *error=rx_overlay_present(draw,view);check(!error,error);check(!glGetError(),"overlay GLES error");
}
static int blend(int destination,float texture,unsigned shade,unsigned alpha)
{
    int source=(int)floorf(texture*shade/128+.0001f);
    int color=destination+(int)floorf((source-destination)*(float)alpha/128);
    return color<0?0:color>255?255:color;
}
static void composition(RxParticleDraw **draw)
{
    const NJS_CNK_VIEW view={.1f,20000,320,320,240,1,1};
    check(njSetTexture(&sys->ef_tlist)&&njSetTextureNum(sys->ef_tn[45])>=0,"composition source texture");
    uint64_t tex0=gs.ctx[0].tex0;unsigned w=1u<<((tex0>>26)&15),h=1u<<((tex0>>30)&15);
    uint32_t *atlas=malloc((size_t)w*h*4);check(atlas!=NULL,"composition source allocation");
    gs_decode_texture(tex0,gs.texa,w,h,atlas);unsigned best=0,score=0;
    for (unsigned i=0;i<w*h;++i) {
        unsigned r=atlas[i]&255,b=(atlas[i]>>16)&255,a=(tex0>>34)&1?atlas[i]>>24:128;
        unsigned candidate=(r<b?r:b)*a;
        if (candidate>score) { score=candidate;best=i; }
    }
    check(score>0,"composition requires a nonzero original red/blue texel");
    uint32_t texel=atlas[best];unsigned alpha=((tex0>>34)&1?texel>>24:128)/2;
    float u=(best%w+.5f)/w,v=(best/w+.5f)/h;free(atlas);
    rx_effects_reset();O_WRK *a=allocate(0),*b=allocate(0);tick();
    a->mode1=b->mode1=1;a->px=b->px=a->py=b->py=40;a->sx=b->sx=a->sy=b->sy=2;tick();
    a->flg|=0x20000000;b->flg|=0x20000000;
    for (unsigned i=0;i<4;++i) {
        a->tv[i].u=b->tv[i].u=u;a->tv[i].v=b->tv[i].v=v;
        a->tv[i].col=0x80ff0000;b->tv[i].col=0x800000ff;
        a->tv[i].z=4;b->tv[i].z=1;
    }
    glViewport(0,0,640,480);unsigned char pixel[4],first[4];
    clear(1);present(draw,&view);glReadPixels(100,379,1,1,GL_RGBA,GL_UNSIGNED_BYTE,first);
    for (unsigned c=0;c<3;++c) {
        float texture=(texel>>(c*8))&255;
        int expected=blend(blend(32,texture,c==2?128:0,alpha),texture,c==0?128:0,alpha);
        check(abs((int)first[c]-expected)<=2,"screen overlays were not ordered by original GS depth bucket");
    }
    sys->ef_trs2d[0]=b;sys->ef_trs2d[1]=a;
    clear(1);present(draw,&view);glReadPixels(100,379,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    for (unsigned c=0;c<4;++c) check(pixel[c]==first[c],"reversed submission changed sorted screen composition");
    for (unsigned i=0;i<4;++i) a->tv[i].z=b->tv[i].z=2;
    unsigned difference=0;
    for (unsigned order=0;order<2;++order) {
        sys->ef_trs2d[0]=order?a:b;sys->ef_trs2d[1]=order?b:a;
        clear(1);present(draw,&view);glReadPixels(100,379,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        for (unsigned c=0;c<3;++c) {
            float texture=(texel>>(c*8))&255;
            int expected=blend(blend(32,texture,c==(order?0u:2u)?128:0,alpha),texture,c==(order?2u:0u)?128:0,alpha);
            check(abs((int)pixel[c]-expected)<=2,"equal-depth overlay insertion order differs");
            if (order) difference+=(unsigned)abs((int)pixel[c]-first[c]);else first[c]=pixel[c];
        }
    }
    check(difference>2,"composition fixture did not distinguish equal-depth order");
    clear(.1f);present(draw,&view);glReadPixels(100,379,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    check(pixel[0]==32 && pixel[1]==32 && pixel[2]==32,"overlay bypassed GS depth rejection");
    for (unsigned i=0;i<4;++i) { a->tv[i].z=4;b->tv[i].z=1; }
    sys->ef_trs2dn=1;sys->ef_trs2d[0]=a;clear(1);present(draw,&view);
    sys->ef_trs2d[0]=b;present(draw,&view);glReadPixels(100,379,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    for (unsigned c=0;c<3;++c) {
        float texture=(texel>>(c*8))&255;
        int expected=blend(blend(32,texture,c==0?128:0,alpha),texture,c==2?128:0,alpha);
        check(abs((int)pixel[c]-expected)<=2,"overlay wrote depth and blocked a later overlay");
    }
    b->stflg|=0x1000000;clear(1);present(draw,&view);glReadPixels(100,379,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    check(pixel[0]==32 && pixel[1]==32 && pixel[2]==32,"hidden screen overlay drew");b->stflg=0;
    b->tv[0].x=1e30f;check(rx_overlay_present(draw,&view)!=NULL,"unsafe GS fixed-point screen coordinate was accepted");
    sys->ef_trs2d[0]=(O_WRK *)(uintptr_t)1;check(rx_overlay_present(draw,&view)!=NULL,"invalid screen queue pointer was read");
    sys->ef_trs2dn=129;check(rx_overlay_present(draw,&view)!=NULL,"invalid screen queue count was read");
    rx_effects_reset();
    puts("PASS original overlay GS bucket order, equal-depth insertion, depth rejection/no writes, hidden state and bounded native queue");
}
static void gpu(void)
{
    RxEgl egl={0};RxParticleDraw *draw=NULL;
    check(rx_egl_open(&egl,NULL,1280,960),"overlay EGL surface");gs_reset();
    const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
    unsigned char *pixels=malloc(1280*960*4);check(pixels!=NULL,"overlay GPU readback allocation");
    size_t compared=0,changed=0;unsigned maximum_error=0;
    for (unsigned page=0;page<3;++page) for (unsigned nearest=0;nearest<2;++nearest) for (unsigned crop=0;crop<2;++crop) {
        unsigned scale=page==2 && nearest && crop?2:1,wout=640*scale,hout=480*scale;
        rx_effects_reset();O_WRK *op=allocate(page);tick();op->mode1=1;
        op->px=97.28125f;op->py=60.78125f;op->sx=2;op->sy=1.5f;
        if (crop) uv_rect(op,.125f,.25f,.75f,.5f);
        for (unsigned i=0;i<4;++i) op->tv[i].col=0x9050c0e0;
        if (nearest) op->flg|=0x20000000;
        tick();
        check(njSetTexture(&sys->ef_tlist)&&njSetTextureNum(sys->ef_tn[45]+page)>=0,"overlay source texture selection");
        uint64_t tex0=gs.ctx[0].tex0;unsigned w=1u<<((tex0>>26)&15),h=1u<<((tex0>>30)&15);
        uint32_t *atlas=malloc((size_t)w*h*4);check(atlas!=NULL,"overlay scalar atlas allocation");
        gs_decode_texture(tex0,gs.texa,w,h,atlas);glViewport(0,0,wout,hout);clear(1);present(&draw,&view);
        glReadPixels(0,0,wout,hout,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(!glGetError(),"overlay readback");
        check(op->tv[0].z==1 && op->tv[3].z==1,"original Ninja depth clamp was not written back");
        float left=truncf((1728+op->tv[0].x)*16)/16-1728,top=truncf((1808+op->tv[0].y)*16)/16-1808;
        float right=truncf((1728+op->tv[3].x)*16)/16-1728,bottom=truncf((1808+op->tv[3].y)*16)/16-1808;
        for (unsigned y=(unsigned)(top*scale)+1;y<(unsigned)(bottom*scale)-1;++y)
            for (unsigned x=(unsigned)(left*scale)+1;x<(unsigned)(right*scale)-1;++x) {
                float u=op->tv[0].u+(op->tv[1].u-op->tv[0].u)*((x+.5f)/scale-left)/(right-left);
                float v=op->tv[0].v+(op->tv[2].v-op->tv[0].v)*((y+.5f)/scale-top)/(bottom-top);
                /* Exact nearest-texel boundaries have an interpolation tie on
                   the GPU. Compare texel interiors; bilinear covers every sample. */
                if (nearest && (fabsf(u*w-roundf(u*w))<.0001f || fabsf(v*h-roundf(v*h))<.0001f)) continue;
                unsigned alpha=(unsigned)floorf(((tex0>>34)&1?sample(atlas,w,h,u,v,3,nearest):128)*72/128+.0001f);
                unsigned shades[]={40,96,112};size_t pixel=((hout-1-y)*wout+x)*4;
                for (unsigned c=0;c<3;++c) {
                    int expected=blend(32,sample(atlas,w,h,u,v,c,nearest),shades[c],alpha);
                    unsigned error=(unsigned)abs((int)pixels[pixel+c]-expected);
                    if (error>maximum_error) maximum_error=error;
                    changed+=pixels[pixel+c]!=32;++compared;
                }
                check(pixels[pixel+3]==255,"overlay changed framebuffer alpha");
            }
        check(pixels[0]==32 && pixels[1]==32 && pixels[2]==32,"overlay changed pixels outside its rectangle");
        free(atlas);
    }
    if (maximum_error>2 || changed<100) fprintf(stderr,"overlay GPU error=%u changed=%zu\n",maximum_error,changed);
    check(maximum_error<=2 && changed>100,"overlay coordinates/texture pages/filter/color/alpha differ from scalar GS reference");
    composition(&draw);
    static NJS_MATRIX camera;njUnitMatrix(&camera);cam.mtx=&camera;
    EF_WORK flame={.flg=1,.id=9,.type=1,.pz=20,.sx=2,.sy=2,.sz=2};
    check(bhSetEffectTb(&flame,NULL,NULL,0)>=0,"post-overlay flame allocation");tick();
    RxParticleDraw *fresh=NULL;glViewport(0,0,640,480);clear(1);
    const char *error=rx_particle_present(&fresh,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    unsigned char *reference=malloc(640*480*4);check(reference!=NULL,"post-overlay 3D reference allocation");
    memcpy(reference,pixels,640*480*4);clear(1);error=rx_particle_present(&draw,&view);check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(!memcmp(reference,pixels,640*480*4),"screen shader state leaked into the next 3D particle draw");
    size_t flame_pixels=0;for (size_t i=0;i<640*480*4;i+=4) flame_pixels+=pixels[i]!=32||pixels[i+1]!=32||pixels[i+2]!=32;
    check(flame_pixels>100,"post-overlay 3D reference was empty");free(reference);rx_particle_close(&fresh);
    puts("PASS shared renderer switches from screen overlays back to identical 3D particle pixels");
    free(pixels);rx_particle_close(&draw);rx_egl_close(&egl);
    printf("PASS original screen overlays: %zu Quest GPU channels, maximum GS reference error %u/255; three pages, crop, filters and scaled viewport\n",compared,maximum_error);
}
void check_overlay_runtime(void) { lifecycle();gpu();rx_effects_reset(); }
