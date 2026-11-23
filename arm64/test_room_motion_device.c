#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "room.h"
#include "main.h"
#include "pwksub.h"
#include "ps2_NaMatrix.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
extern BH_SCEWORK bhEtask[16];extern int lNaMatMatrixStuckMax,lNaMatMatrixStuckCnt;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL room motion: %s\n",why);exit(1);}}
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

static unsigned char data[64];static RxRoomScripts script;
static const char *run(unsigned record,const unsigned char command[8]){memset(data,0,sizeof(data));data[0]=12;data[4]=18;data[8]=20;data[12]=20;data[20]=0x65;data[21]=1;data[22]=record;memcpy(data+24,command,8);data[32]=0xff;check(!rx_room_scripts_open(data,34,&script),"native room motion fixture");rom->evtp=&script;return rx_events_start_room();}
static void reject(const unsigned char command[8]){unsigned char bad[26]={8,0,0,0,24,0,0,0,5,1,30,0,0,0};memcpy(bad+14,command,8);RxRoomScripts bank;check(!rx_room_scripts_open(bad,26,&bank),"bad room-motion fixture");rom->evtp=&bank;unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"invalid room motion executed prefix");sys->ev_flg[0]=flags;}
static void reject_actor(BH_PWORK *actor,const unsigned char command[8]){BH_PWORK before=*actor;check(run(0,command)!=NULL&&!memcmp(&before,actor,sizeof(before)),"invalid room actor reached original motion handler");}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"full campaign remains gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next original dependency");
 const unsigned rates[]={0x8000,0x10000,0x10000,0x8000,0x5555,0x4000,0x3333,0x2aaa,0x8000,0x4000,0x2aaa,0x2000,0x1999,0x1555,0x2492,0x2000,0x1c71,0x1999,0x1745,0x1555,0x1249,0x1000,0xe38,0xccc,0xba2,0xaaa};
 unsigned cases=0;
 for(unsigned record=0;record<2;++record){BH_PWORK *actor=&ene[rom->enep[record].wrk_no];BH_PWORK saved=*actor;ML_WORK *model=actor->mlwP;unsigned n=model->obj_num;check(n<=64,"room actor capacity");NJS_CNK_OBJECT nodes[64];O_WORK work[64];memcpy(nodes,model->objP,n*sizeof(*nodes));memcpy(work,model->owP,n*sizeof(*work));
  for(unsigned test=0;test<2048+80+248;++test){unsigned speed=test<2048?test/8:1,update=test<2128?test%8:test-2120,slot=test>=2048&&test<2128?(test-2048)/8:0;
   *actor=saved;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));actor->flg&=~0x1080u;actor->mtn_no=0;actor->mtn_add=12345;actor->hokan_count=0;actor->px=3.5f;actor->py=-2;actor->pz=7;actor->ax=speed*137;actor->ay=speed*251;actor->az=speed*17;
   unsigned char command[]={0x69,24,speed,1,slot,(unsigned char)(speed&1),update,6};BH_PWORK expected=*actor;
   expected.mnwP=sys->rmthp;expected.mtn_add=speed<26?rates[speed]:12345;expected.mode0=command[5]?5:1;expected.mode1=slot;expected.mode2=expected.mode3=0;expected.mtn_no=slot;expected.mtn_md=0x28;expected.hokan_rate=expected.frm_no=0;expected.ct1=update;expected.ct2=6;
   int evaluate=update==0||update==2||update==5||update==6,calc=update==0||update==3||update==5||update==7;MN_WORK *clip=sys->rmthp+slot;
   NJS_CNK_OBJECT reference[64];memcpy(reference,nodes,n*sizeof(*nodes));
   if(evaluate){
    if(clip->atrP)expected.mtn_attr=clip->atrP[0];
    for(unsigned j=0;j<clip->obj_num;++j){unsigned joint=j<6?j:j+9;
     if(clip->md2P[j].p[0]&&((clip->flg&0x30)||j==0))memcpy(reference[joint].pos,clip->md2P[j].p[0],12);
     if(!(work[joint].flg&2)){const unsigned short *keys=clip->md2P[j].p[1];if(keys)for(unsigned axis=0;axis<3;++axis)reference[joint].ang[axis]=(clip->flg&0x20)?(short)keys[axis]:keys[axis];else if(clip->flg&0x30)reference[joint].ang[0]=0;else memset(reference[joint].ang,0,sizeof(reference[joint].ang));}
    }
   }
   error=run(record,command);if(error)printf("Record %u slot %u update %u: %s\n",record,slot,update,error);check(!error,error);
   check(!memcmp(reference,model->objP,n*sizeof(*nodes)),"original room pose differs from independent frame-zero keys/trailing joints");
   if(calc){Matrix root;identity(root);transform(root,actor->px,actor->py,actor->pz,actor->ax,actor->ay,actor->az);matrix((float *)actor->mtx,root);hierarchy(model,model->objP,root);}else check(!memcmp(work,model->owP,n*sizeof(*work))&&!memcmp(expected.mtxbuf,actor->mtxbuf,64),"room update unexpectedly recalculated model");
   memcpy(expected.mtxbuf,actor->mtxbuf,sizeof(expected.mtxbuf));check(!memcmp(&expected,actor,sizeof(expected)),"original room motion changed unexpected actor state");++cases;
  }
  *actor=saved;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));
 }
 printf("PASS %u original room motion commands, every speed/eight update branches, both actors/all ten clips; %u independent matrix channels (max error %.9g)\n",cases,channels,maximum);
 unsigned char command[]={0x69,24,0,1,0,0,0,6};for(unsigned mode=0;mode<256;++mode)if(mode!=1){command[3]=mode;reject(command);}command[3]=1;for(unsigned blend=0;blend<256;++blend)if(blend!=6){command[7]=blend;reject(command);}command[7]=6;
 MN_WORK *bank=sys->rmthp;sys->rmthp=(void *)(uintptr_t)1;reject(command);sys->rmthp=bank;void *keys=bank[0].md2P[0].p[0];bank[0].md2P[0].p[0]=(void *)(uintptr_t)1;reject(command);bank[0].md2P[0].p[0]=keys;

 for(unsigned slot=10;slot<256;++slot){command[4]=slot;reject(command);}command[4]=0;
 void *tracks=bank[0].md2P;bank[0].md2P=(void *)(uintptr_t)1;reject(command);bank[0].md2P=tracks;
 BH_PWORK *target=&ene[rom->enep[0].wrk_no];BH_PWORK target_saved=*target;
 target->mtx=(void *)(uintptr_t)1;reject_actor(target,command);*target=target_saved;
 O_WORK *owned_work=target->mlwP->owP;target->mlwP->owP=(void *)(uintptr_t)1;reject_actor(target,command);*target=target_saved;
 target->hokan_count=1;reject_actor(target,command);*target=target_saved;
 target->mtn_no=0x10000;reject_actor(target,command);*target=target_saved;
 target->id=1;reject_actor(target,command);*target=target_saved;
 for(unsigned j=0;j<31;++j){unsigned nodeflags=owned_work[j].flg;owned_work[j].flg^=4;reject_actor(target,command);owned_work[j].flg=nodeflags;}
 puts("PASS all 31 altered facial/body motion-protection flags reject before the original evaluator");
 O_WRK old_effect=eff[511];memset(&eff[511],0,sizeof(eff[511]));eff[511].flg=0x81;eff[511].lkwkp=(void *)(uintptr_t)target;reject_actor(target,command);eff[511]=old_effect;
 unsigned char short_data[23]={8,0,0,0,21,0,0,0,5,1,30,0,0,0};memcpy(short_data+14,command,7);RxRoomScripts truncated;check(!rx_room_scripts_open(short_data,sizeof(short_data),&truncated),"truncated room motion fixture");rom->evtp=&truncated;unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"truncated room motion executed prefix");sys->ev_flg[0]=flags;
 unsigned char waiting[36]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0,0x65,1,0,0,0xfe};memcpy(waiting+25,command,8);waiting[33]=0xff;RxRoomScripts delayed;check(!rx_room_scripts_open(waiting,35,&delayed),"delayed actor fixture");rom->evtp=&delayed;check(!rx_events_start_room(),"start delayed room motion");BH_PWORK before=*target;bhEtask[0].work=(void *)(uintptr_t)1;check(rx_events_tick()!=NULL&&!memcmp(&before,target,sizeof(before)),"foreign task reached room motion handler");
 check(original->data[2300]==0x69&&original->data[2301]==24,"actual opening room motion bytes");error=run(0,original->data+2300);check(!error,error);
 puts("PASS actual original room-motion bytes and atomic mode/interpolation/clip/bank/track/model/matrix/blend/link/truncated/foreign-task guards");
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);check(bhCheckNativeRoomMotion(command,NULL)!=NULL,"room motion context survived teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
