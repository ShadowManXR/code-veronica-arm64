#include "opening.h"
#include "face_pose.h"
#include "player_control_runtime.h"
#include "pl_evt.h"
#include "njplus.h"
#include "ps2_NaMath.h"
#include "ps2/vu0.h"
#include "android_egl.h"
#include "camera.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "room.h"
#include "main.h"
#include "pwksub.h"
#include "MdlPut.h"
#include "ps2_NaMatrix.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
extern BH_SCEWORK bhEtask[16];extern int lNaMatMatrixStuckMax,lNaMatMatrixStuckCnt;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL script player tick: %s\n",why);exit(1);}}
typedef double Matrix[16];
static void identity(Matrix m){memset(m,0,sizeof(Matrix));m[0]=m[5]=m[10]=m[15]=1;}
static float sine(int angle){unsigned n=(unsigned)angle&65535,index=n&16383;if(n&16384)index=16383-index;float s=sinf(0.0000958738f*index);return n&32768?-s:s;}
static void product(Matrix a,const Matrix b){Matrix r;for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row){double v=0;for(unsigned k=0;k<4;++k)v+=a[k*4+row]*b[c*4+k];r[c*4+row]=v;}memcpy(a,r,sizeof(r));}
static void rotation(Matrix m,unsigned axis,int angle){Matrix r;identity(r);unsigned p=(axis+1)%3,q=(axis+2)%3;double s=sine(angle),c=sine(angle+16384);r[p*4+p]=r[q*4+q]=c;r[p*4+q]=s;r[q*4+p]=-s;product(m,r);}
static void transform(Matrix m,float x,float y,float z,int ax,int ay,int az){Matrix t;identity(t);t[12]=x;t[13]=y;t[14]=z;product(m,t);rotation(m,2,az);rotation(m,1,ay);rotation(m,0,ax);}
static unsigned channels;static double maximum;
static void matrix(const float *got,const Matrix expected){for(unsigned i=0;i<16;++i){double d=fabs(got[i]-expected[i]);if(d>maximum)maximum=d;check(isfinite(got[i])&&d<0.001,"native world/link matrix differs from independent hierarchy");++channels;}}
/* Original facial flat entries retain links into the private source tree.
   Traverse that tree for parent relationships, using preorder to select the
   separately animated flat entries. */
static void hierarchy_nodes(ML_WORK *model,const NJS_CNK_OBJECT *node,const Matrix parent,unsigned *next){while(node){unsigned index=(*next)++;check(index<model->obj_num,"reference node extent");const NJS_CNK_OBJECT *flat=model->objP+index;Matrix current;memcpy(current,parent,sizeof(current));transform(current,flat->pos[0],flat->pos[1],flat->pos[2],flat->ang[0],flat->ang[1],flat->ang[2]);check(!(model->owP[index].flg&1),"fixture has an unhandled matrix override");matrix(model->owP[index].mtx,current);if(node->child)hierarchy_nodes(model,node->child,current,next);node=node->sibling;}}
static void hierarchy(ML_WORK *model,const NJS_CNK_OBJECT *node,const Matrix parent){unsigned next=0;hierarchy_nodes(model,node,parent,&next);check(next==model->obj_num,"reference facial tree count");}



static unsigned char code[256];static RxRoomScripts script;
static void run(const unsigned char *commands,unsigned count){memset(code,0,sizeof(code));code[0]=8;code[4]=12+count;code[8]=0x65;memcpy(code+12,commands,count);check(!rx_room_scripts_open(code,14+count,&script),"scripted player fixture");rom->evtp=&script;const char *e=rx_events_start_room();check(!e,e);}
static void preview(RxOpening *o){BH_PWORK before=*plp;EXP_WORK work=o->player_work;unsigned head=o->pose_flags;NJS_CNK_OBJECT nodes[64];O_WORK matrices[64];unsigned n=plp->mlwP->obj_num;memcpy(nodes,plp->mlwP->objP,n*sizeof(*nodes));memcpy(matrices,plp->mlwP->owP,n*sizeof(*matrices));VuState vu=g_vu0;const char *e=rx_player_control_script_check();check(!e,e);check(!memcmp(&before,plp,sizeof(before))&&!memcmp(&work,&o->player_work,sizeof(work))&&head==o->pose_flags&&!memcmp(nodes,plp->mlwP->objP,n*sizeof(*nodes))&&!memcmp(matrices,plp->mlwP->owP,n*sizeof(*matrices))&&!memcmp(&vu,&g_vu0,sizeof(vu)),"preview advanced original player/controller/joints/VU");}
static void step(RxOpening *o){preview(o);const char *e=rx_player_control_script_tick();if(e)fprintf(stderr,"mode%u/%u/%u frame%d flags%x/%x: %s\n",plp->mode0,plp->mode2,plp->mode3,plp->frm_no,plp->flg,plp->stflg,e);check(!e,e);Matrix root;identity(root);transform(root,plp->px,plp->py,plp->pz,0,plp->ay,0);matrix((float*)plp->mtx,root);hierarchy(plp->mlwP,plp->mlwP->objP,root);check(rx_engine_player_world_pose(o->player),"scripted world pose not published");}
static unsigned char pixels[640*480*4],repeat[sizeof(pixels)],hidden[sizeof(pixels)];
static void capture(RxOpening *o,unsigned char *out){glViewport(0,0,640,480);const char *e=rx_opening_present(o);check(!e,e);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,out);check(!glGetError(),"scripted player GPU readback");}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);check(o.commands_ready&&!o.events_ready,"full opening remains gated");BH_PWORK startup=*plp;EXP_WORK startup_work=o.player_work;
 RxRoomScripts *original=rom->evtp;unsigned flags=sys->sp_flg;sys->sp_flg|=1;check(!memcmp(original->data+6540,(unsigned char[]){0x64,131,1,0},4)&&!memcmp(original->data+6558,(unsigned char[]){0x64,131,2,0},4),"authored original player controller requests");
 run(original->data+6508,36);check(plp->mode0==7&&plp->mode2==1&&plp->mode3==0,"original stand setup");for(unsigned i=0;i<25;++i)step(&o);check(plp->mtn_no==PlMtnAct[0][0][0]&&plp->hokan_count==0&&plp->mode3==1,"original 25-frame stand/blend did not settle");float x=plp->px,z=plp->pz;run(original->data+6558,4);for(unsigned i=0;i<45;++i)step(&o);check(plp->mode0==7&&plp->mode2==2&&plp->mtn_no==PlMtnAct[0][0][2]&&(fabsf(plp->px-x)+fabsf(plp->pz-z)>.1f),"authored 45-frame walk did not move");printf("PASS actual RM_0000 event26 stand25/walk45 source bytes, blends and published original world joints: (%.5f,%.5f) -> (%.5f,%.5f)\n",x,z,plp->px,plp->pz);
 /* Independent analytic speed/heading oracle, with original wall flag disabled
    for this fixture only; production keeps original wall collision. */
 unsigned analytical=0;for(unsigned heading=0;heading<8;++heading){plp->flg&=~0x10u;plp->flg&=~0xC0000u;plp->stflg=0x10000;plp->ax=plp->az=0;plp->ay=heading*8192;plp->px=30;plp->py=0;plp->pz=68;plp->mode0=7;plp->mode1=0;plp->mode2=2;plp->mode3=1;plp->mtn_no=PlMtnAct[0][0][2];plp->frm_no=5*65536;plp->mtn_add=65536;plp->hokan_count=0;plp->hokan_rate=32768;plp->mtn_md=0;
  for(unsigned t=0;t<100;++t){unsigned frame=(unsigned)plp->frm_no>>16,period=plp->mnwP[plp->mtn_no].frm_num;float speed=.18f+fabsf(.15f*sine(frame*(65536/period)+16384));float ex=plp->px-speed*sine(plp->ay),ez=plp->pz-speed*sine(plp->ay+16384);step(&o);check(fabsf(ex-plp->px)<.00001f&&fabsf(ez-plp->pz)<.00001f&&fabsf(speed-plp->spd)<.000001f,"original walking speed/heading differs from independent oracle");++analytical;}}
 printf("PASS %u analytic original speed/heading updates, %u independent world matrix channels (max error %.9g), exact validation rollback\n",analytical,channels,maximum);
 /* Return to standing through source state request after motion. */
 plp->flg|=0x10;run(original->data+6540,4);for(unsigned i=0;i<12;++i)step(&o);check(plp->mode2==1&&plp->mode3==1&&plp->hokan_count==0,"walk-to-stand original state transition");
 /* The original suspended predicate follows the real scripted player clock. */
 plp->flg&=~0x10u;plp->mode0=7;plp->mode1=0;plp->mode2=2;plp->mode3=1;plp->mtn_no=PlMtnAct[0][0][2];plp->frm_no=5*65536;plp->mtn_add=65536;plp->hokan_count=0;
 unsigned char waiting[38]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0,0xfc,8,0x38,0,0,0,9,0,0xfe,0xf3,5,1,30,0,0,0,0xff,0};RxRoomScripts wait_bank;check(!rx_room_scripts_open(waiting,sizeof(waiting),&wait_bank),"player clock wait fixture");rom->evtp=&wait_bank;sys->ev_flg[0]&=~2u;check(!rx_events_start_room()&&!(sys->ev_flg[0]&2),"scripted player wait did not suspend");
 for(unsigned t=1;t<=4;++t){step(&o);check(!rx_events_tick(),"player clock event update");check(((sys->ev_flg[0]&2)!=0)==(t==4),"script wait released on wrong original player frame");}rx_events_reset();rom->evtp=original;puts("PASS original suspended FrameCheck/While/EvtNext released by continuous scripted-player motion at the exact frame boundary");
 BH_PWORK saved=*plp;EXP_WORK scalar=o.player_work;unsigned pose=o.pose_flags;
 for(unsigned bad=0;bad<10;++bad){*plp=saved;o.player_work=scalar;o.pose_flags=pose;switch(bad){case 0:plp->mode2=9;break;case 1:plp->exp1=(void*)(uintptr_t)1;break;case 2:plp->mnwP=(void*)(uintptr_t)1;break;case 3:plp->mtn_no=511;break;case 4:plp->frm_no=-1;break;case 5:o.player_work.rtspd=NAN;break;case 6:o.player_work.wpntp=2;break;case 7:plp->stflg|=0x100000;break;case 8:plp->mtn_add=INT_MAX;break;case 9:plp->ax=1;break;}BH_PWORK before=*plp;EXP_WORK w=o.player_work;check(rx_player_control_script_tick()!=NULL&&!memcmp(&before,plp,sizeof(before))&&!memcmp(&w,&o.player_work,sizeof(w)),"bad script state changed player prefix");}
 *plp=saved;o.player_work=scalar;o.pose_flags=pose;sys->sp_flg&=~1u;check(!rx_player_control_script_tick()&&!memcmp(&saved,plp,sizeof(saved)),"paused scripted player changed state");sys->sp_flg|=1;
 puts("PASS ten malformed scripted-player contexts and original pause gate preserve player/scalar state");
 RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"Quest GLES pbuffer");sys->sp_flg&=~0x80u;sys->ts_flg|=0x200;
 *plp=startup;o.player_work=startup_work;check(!rx_engine_player_pose(o.player,plp,RX_PLAYER_IDLE,10*65536,0),"local skin oracle");capture(&o,pixels);capture(&o,pixels);
 NJS_CNK_MODEL *owner=NULL;for(unsigned i=0;i<plp->mlwP->obj_num;++i)if((plp->mlwP->objP[i].evalflags&0x80000000u)&&plp->mlwP->objP[i].model){check(!owner,"one original player skin owner");owner=plp->mlwP->objP[i].model;}
 check(owner!=NULL,"original player skin owner");HDR_PS header;memcpy(&header,owner->vlist,sizeof(header));size_t vertices=header.usIndexMax;check(vertices>100&&vertices<10000,"skin oracle fixture extent");float *local=malloc(vertices*32);check(local!=NULL,"skin oracle snapshot");memcpy(local,(unsigned char*)owner->vlist+64,vertices*32);
 bhCalcModel(plp);check(!rx_engine_player_publish(o.player,plp),"world skin publication");capture(&o,repeat);
 const float *world=(const float*)((unsigned char*)owner->vlist+64);Matrix rotation_root;identity(rotation_root);rotation(rotation_root,1,plp->ay);double skin_error=0;
 for(size_t v=0;v<vertices;++v)for(unsigned n=0;n<2;++n){for(unsigned k=0;k<3;++k){const float *p=local+v*8+n*4;double expected=rotation_root[k]*p[0]+rotation_root[4+k]*p[1]+rotation_root[8+k]*p[2];double d=fabs(world[v*8+n*4+k]-expected);if(d>skin_error)skin_error=d;check(d<.001,"world skin differs from independent rotated-local reference");}check(!memcmp(world+v*8+n*4+3,local+v*8+n*4+3,4),"player skin metadata changed");}
 printf("PASS %zu independent world skin position/normal channels, maximum error %.9g\n",vertices*6,skin_error);free(local);
 unsigned different=0,maximum_pixel=0;for(unsigned i=0;i<sizeof(pixels);++i){unsigned d=abs((int)pixels[i]-(int)repeat[i]);different+=d!=0;if(d>maximum_pixel)maximum_pixel=d;}
 printf("World/local GPU differences %u max %u\n",different,maximum_pixel);FILE *dbg=fopen("/data/local/tmp/v73-player-frames.rgba","wb");if(dbg){fwrite(pixels,1,sizeof(pixels),dbg);fwrite(repeat,1,sizeof(repeat),dbg);fclose(dbg);}check(different<15000&&maximum_pixel<=32,"world publication changed player/hand geometry beyond numerical tolerance");capture(&o,pixels);check(!memcmp(pixels,repeat,sizeof(pixels)),"scripted world pose rendering not stable");
 plp->stflg|=0x1000000;capture(&o,hidden);unsigned visible=0;for(unsigned i=0;i<640*480;++i)visible+=memcmp(pixels+i*4,hidden+i*4,3)!=0;check(visible>100,"scripted player GPU frame lacks visible player");printf("PASS scripted world-joint/translation-relative-skin/equipped-hand GLES equivalence: %u differing channels (max %u), %u player pixels and exact repeats\n",different,maximum_pixel,visible);
 rx_opening_close_graphics(&o);rx_egl_close(&egl);rx_events_reset();rom->evtp=original;sys->sp_flg=flags;rx_opening_end(&o);check(rx_player_control_script_check()!=NULL,"retired player tick accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
