#include "opening.h"
#include "face_control_runtime.h"
#include "boot_disc.h"
#include "pad_input.h"
#include "room_scripts.h"
#include "android_egl.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned char visible[640*480*4], other[sizeof(visible)];
static void check(int ok,const char *message) {
    if(!ok) { fprintf(stderr,"FAIL opening actor events: %s\n",message); exit(1); }
}
static void execute(const unsigned char *code,size_t bytes) {
    unsigned char data[128]={8,0,0,0};
    data[4]=(unsigned char)(8+bytes);
    memcpy(data+8,code,bytes);
    RxRoomScripts scripts;
    check(!rx_room_scripts_open(data,10+bytes,&scripts),"command fixture");
    void *saved=rom->evtp;
    rom->evtp=&scripts;
    const char *error=rx_events_start_room();
    check(!error,error);
    rx_events_reset();
    rom->evtp=saved;
}
static void capture(RxOpening *opening,unsigned char *pixels) {
    /* Hold flame UV frames fixed for isolated actor visibility comparisons. */
    for (unsigned i=0;i<512;++i) if ((eff[i].flg&1) && eff[i].id==9 && eff[i].mode0==1) eff[i].ct0=0;
    glViewport(0,0,640,480);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const char *error=rx_opening_present(opening);
    check(!error,error);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(!glGetError(),"GPU readback");
}
static size_t different(const unsigned char *a,const unsigned char *b) {
    size_t count=0;
    for(size_t i=0;i<sizeof(visible);i+=4) count+=memcmp(a+i,b+i,4)!=0;
    return count;
}
static void step(RxOpening *opening,RxPadInput *pad,unsigned buttons) {
    pad->keys=buttons;
    rx_pad_poll_game(pad);
    rx_opening_read_controls();
    const char *error=rx_opening_move_player(opening);
    check(!error,error);
}
int main(int argc,char **argv) {
    check(argc==2 || argc==3,"supply ISO directory and optional PPM capture path");
    RxAndroidDisc disc={0}; RxBootDisc boot; RxOpening opening={0}; RxEgl egl={0}; RxPadInput pad;
    const char *error=rx_android_mount_disc(&disc,argv[1]);
    if(!error) error=rx_android_start_disc(&disc,&boot);
    if(!error) error=rx_opening_start(&opening,0);
    check(!error,error);
    RxRoomEffectFault effect_fault;const char *effect_error=rx_room_effects_check_live(opening.room_effects,&effect_fault);
    check(rx_room_effects_count(opening.room_effects)==14 && !effect_error && sys->efid[0]>=0,
          "all opening effect records must now be published");
    RxEventFault fault;const char *scenario_error=rx_opening_check_events(&fault);
    fprintf(stderr,"Room readiness marker %02x offset %zu: %s\n",fault.opcode,fault.offset,scenario_error);
    check(!opening.events_ready && scenario_error && fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected full-room scenario readiness");
    check(!sys->ev_flg[0],"readiness check changed original opening progression");
    check(rx_egl_open(&egl,NULL,640,480),"EGL initialization");
    rx_pad_init(&pad); GLint depth_bits,stencil_bits; glGetIntegerv(GL_DEPTH_BITS,&depth_bits); glGetIntegerv(GL_STENCIL_BITS,&stencil_bits); fprintf(stderr,"EGL depth %d stencil %d error %x\n",depth_bits,stencil_bits,glGetError());
    sys->sp_flg&=~0x80u; // Hold original animated lights fixed for image comparisons.
    capture(&opening,other);
    capture(&opening,visible);
    if (argc==3) {
        FILE *capture_file=fopen(argv[2],"wb");check(capture_file!=NULL,"open frame capture");
        fprintf(capture_file,"P6\n640 480\n255\n");
        for (int y=479;y>=0;--y) for (int x=0;x<640;++x)
            check(fwrite(visible+(y*640+x)*4,1,3,capture_file)==3,"write frame capture");
        check(!fclose(capture_file),"close frame capture");
    }
    BH_PWORK *face_actor=&ene[rom->enep[1].wrk_no];
    check(!rx_face_control_check(face_actor)&&(uintptr_t)face_actor->exp0>UINT32_MAX&&(uintptr_t)face_actor->exp1>UINT32_MAX&&(uintptr_t)face_actor->exp2>UINT32_MAX&&!rx_face_control_ready(face_actor),"native facial contexts must retain owned full-width bindings");
    int face_before=*(int *)face_actor->exp0;
    unsigned char face_repeat[6]={0};memcpy(face_repeat,((RxRoomScripts *)rom->evtp)->data+1742,4);
    execute(face_repeat,sizeof(face_repeat));check(*(int *)face_actor->exp0==(face_before|2),"original FaceRep did not set owned control flag");
    capture(&opening,other);check(!different(visible,other),"facial request flags changed the inactive live render path");
    const unsigned char face_once[]={0x9a,1,1,0,0,0};execute(face_once,sizeof(face_once));
    check(*(int *)face_actor->exp0==face_before,"original FaceRep did not restore control flag");
    puts("PASS original FaceRep flags on owned high-address RM_0000 actor storage; native context bound; live facial frame publication remains gated");
    int point_count=sys->lg_pnt;check(point_count==2,"original starting-room point light count changed");
    const unsigned char light_off[]={0x35,1,11,0,0,0},light_on[]={0x35,0,11,0,0,0};
    execute(light_off,sizeof(light_off));capture(&opening,other);
    size_t light_pixels=different(visible,other);
    check(sys->lg_pnt==point_count-1 && light_pixels>50,"original LightSet did not remove the selected room light from the GPU frame");
    execute(light_on,sizeof(light_on));capture(&opening,other);
    check(sys->lg_pnt==point_count && !different(visible,other),"original LightSet did not restore the exact room frame");
    printf("PASS original LightSet selection changed %zu Quest GPU pixels; exact restore\n",light_pixels);
    const unsigned char hide[]={0x24,0,0,0,0,0},show[]={0x24,0,0,1,0,0};
    execute(hide,sizeof(hide)); capture(&opening,other);
    size_t hidden_pixels=different(visible,other);
    check(hidden_pixels>100,"hide did not remove Claire and her hands");
    execute(show,sizeof(show)); capture(&opening,other);
    check(!different(visible,other),"show failed to restore the exact frame");
    const unsigned char display_off[]={0x20,0,0,0,0,0},display_on[]={0x20,0,0,1,0,0};
    execute(display_off,sizeof(display_off)); capture(&opening,other);
    check(different(visible,other)==hidden_pixels,"display mask left visible player geometry");
    execute(display_on,sizeof(display_on)); capture(&opening,other);
    check(!different(visible,other),"display mask did not restore the frame");
    unsigned mesh=0;
    while(mesh<plp->mlwP->obj_num&&!plp->mlwP->objP[mesh].model) ++mesh;
    check(mesh<plp->mlwP->obj_num&&mesh<256,"missing original body mesh");
    unsigned char node[]={0x3b,0,0,(unsigned char)mesh,0,0,0,0};
    execute(node,sizeof(node)); capture(&opening,other);
    size_t mesh_pixels=different(visible,other);
    check(mesh_pixels>100,"original node visibility ignored by skinned renderer");
    node[4]=1;
    execute(node,sizeof(node)); capture(&opening,other);
    check(!different(visible,other),"node visibility failed to restore original mesh");
    const unsigned char pause[]={0x2e,0,0,0},resume[]={0x2e,1,0,0};
    execute(pause,sizeof(pause));
    float x=plp->px,z=plp->pz; int frame=plp->frm_no;
    for(int i=0;i<12;++i) step(&opening,&pad,RX_PAD_UP);
    check(plp->px==x&&plp->pz==z&&plp->frm_no==frame,"player moved during original motion pause");
    execute(resume,sizeof(resume));
    for(int i=0;i<12;++i) step(&opening,&pad,RX_PAD_UP);
    check(hypotf(plp->px-x,plp->pz-z)>0.2f,"motion did not resume");
    execute(hide,sizeof(hide)); x=plp->px; z=plp->pz; frame=plp->frm_no;
    for(int i=0;i<12;++i) step(&opening,&pad,RX_PAD_UP);
    check(plp->px==x&&plp->pz==z&&plp->frm_no==frame,"hidden actor continued its ordinary player task");
    execute(show,sizeof(show));
    printf("PASS original actor visibility GPU: %zu hidden pixels, %zu hidden body-mesh pixels; exact restore\n",hidden_pixels,mesh_pixels);
    puts("PASS original player motion pause/resume and hidden-actor control gating in real RM_0000");
    unsigned char cine_enter[4]={0};memcpy(cine_enter,((RxRoomScripts *)rom->evtp)->data+1726,2);
    const unsigned char cine_exit[]={0x12,2,0,0};
    execute(cine_enter,sizeof(cine_enter));x=plp->px;z=plp->pz;frame=plp->frm_no;
    for(int i=0;i<12;++i)step(&opening,&pad,RX_PAD_DOWN);
    check((sys->cb_flg&4)&&(sys->st_flg&4)&&plp->px==x&&plp->pz==z&&plp->frm_no==frame,"original CineSet did not block native movement");
    execute(cine_exit,sizeof(cine_exit));
    for(int i=0;i<12;++i)step(&opening,&pad,RX_PAD_DOWN);
    check(!(sys->cb_flg&4)&&!(sys->st_flg&4)&&hypotf(plp->px-x,plp->pz-z)>0.2f,"original cinematic exit did not restore native movement");
    puts("PASS original RM_0000 CineSet blocks and restores player movement in the real room");
    puts("PASS all 14 original room effects published; scenario stops at its next unported command without changing progression flags");
    rx_opening_end(&opening);
    check(!face_actor->exp0 && rx_face_control_check(face_actor)!=NULL,"room teardown retained native facial control ownership");
    rx_egl_close(&egl);
    return 0;
}









