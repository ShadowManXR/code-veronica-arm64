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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL room motion frame: %s\n",why);exit(1);}}
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
static const unsigned rates[]={0x8000,0x10000,0x10000,0x8000,0x5555,0x4000,0x3333,0x2aaa,0x8000,0x4000,0x2aaa,0x2000,0x1999,0x1555,0x2492,0x2000,0x1c71,0x1999,0x1745,0x1555,0x1249,0x1000,0xe38,0xccc,0xba2,0xaaa};
static const char *run(unsigned record,const unsigned char command[10]){memset(data,0,sizeof(data));data[0]=12;data[4]=18;data[8]=20;data[12]=20;data[20]=0x65;data[21]=1;data[22]=record;memcpy(data+24,command,10);data[34]=0xff;check(!rx_room_scripts_open(data,36,&script),"native explicit-frame fixture");rom->evtp=&script;return rx_events_start_room();}
static void reject(const unsigned char command[10],unsigned bytes){unsigned char bad[30]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};bad[4]=14+bytes;memcpy(bad+14,command,bytes);RxRoomScripts bank;check(!rx_room_scripts_open(bad,16+bytes,&bank),"bad explicit-frame fixture");rom->evtp=&bank;unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"invalid explicit-frame command executed prefix");sys->ev_flg[0]=flags;}
static unsigned cases;
static void verify(unsigned record,unsigned mode,unsigned speed,unsigned update,unsigned slot,unsigned frame,const BH_PWORK *saved,const NJS_CNK_OBJECT *nodes,const O_WORK *work){
 BH_PWORK *actor=&ene[rom->enep[record].wrk_no];*actor=*saved;ML_WORK *model=actor->mlwP;unsigned n=model->obj_num;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));actor->flg&=~0x1080u;actor->mtn_no=0;actor->mtn_add=12345;actor->hokan_count=0;actor->px=3.5f;actor->py=-2;actor->pz=7;actor->ax=speed*137;actor->ay=speed*251;actor->az=speed*17;
 if(mode!=1){actor->mode0=0x5a;actor->mode1=0x67;actor->mode2=0x89;actor->mode3=0xfe;actor->ct1=0x12345678;actor->ct2=(int)0x80000001u;actor->mtn_no=(int)0xa5ff1234u;actor->frm_mode=77;actor->hokan_count=17;actor->hokan_rate=37;}
 unsigned char command[]={0x69,34,speed,mode,slot,(unsigned char)(speed&1),update,6,frame,frame>>8};BH_PWORK expected=*actor;MN_WORK *clip=sys->rmthp+slot;
 expected.mnwP=sys->rmthp;expected.mtn_add=speed<26?rates[speed]:12345;expected.mtn_no=slot;expected.mtn_md=0x28;expected.hokan_rate=0;expected.frm_no=frame<<16;
 if(mode==1){expected.mode0=command[5]?5:1;expected.mode1=slot;expected.mode2=expected.mode3=0;expected.ct1=update;expected.ct2=6;}else{expected.mode3=1;expected.frm_mode=0;expected.hokan_count=0;}
 int evaluate=update==0||update==2||update==5||update==6,calc=update==0||update==3||update==5||update==7;
 NJS_CNK_OBJECT reference[64];memcpy(reference,nodes,n*sizeof(*nodes));
 if(evaluate){
  if(clip->atrP)expected.mtn_attr=clip->atrP[frame];
  for(unsigned j=0;j<clip->obj_num;++j){unsigned joint=j<6?j:j+9;
   if(clip->md2P[j].p[0]&&((clip->flg&0x30)||j==0))memcpy(reference[joint].pos,(unsigned char *)clip->md2P[j].p[0]+frame*12,12);
   if(!(work[joint].flg&2)){const unsigned short *keys=clip->md2P[j].p[1];if(keys)for(unsigned axis=0;axis<3;++axis)reference[joint].ang[axis]=(clip->flg&0x20)?(short)keys[frame*3+axis]:keys[frame*3+axis];else if(clip->flg&0x30)reference[joint].ang[0]=0;else memset(reference[joint].ang,0,sizeof(reference[joint].ang));}
  }
 }
 const char *error=run(record,command);if(error)printf("Record %u slot %u frame %u: %s\n",record,slot,frame,error);check(!error,error);
 for(unsigned j=0;j<n;++j)for(unsigned axis=0;axis<3;++axis){check(reference[j].pos[axis]==model->objP[j].pos[axis],"explicit frame position differs from independent keys");reference[j].pos[axis]=model->objP[j].pos[axis];}
 check(!memcmp(reference,model->objP,n*sizeof(*nodes)),"explicit frame angle or trailing joint/node metadata differs");
 if(calc){Matrix root;identity(root);transform(root,actor->px,actor->py,actor->pz,actor->ax,actor->ay,actor->az);matrix((float *)actor->mtx,root);hierarchy(model,model->objP,root);}else check(!memcmp(work,model->owP,n*sizeof(*work))&&!memcmp(expected.mtxbuf,actor->mtxbuf,64),"explicit frame unexpectedly recalculated model");
 memcpy(expected.mtxbuf,actor->mtxbuf,sizeof(expected.mtxbuf));check(!memcmp(&expected,actor,sizeof(expected)),"explicit frame changed unexpected actor fields");++cases;
}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"full opening must remain gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next original dependency");
 for(unsigned mode=1;mode<=3;++mode)for(unsigned record=0;record<2;++record){BH_PWORK *actor=&ene[rom->enep[record].wrk_no];BH_PWORK saved=*actor;ML_WORK *model=actor->mlwP;unsigned n=model->obj_num;check(n<=64,"room skeleton capacity");NJS_CNK_OBJECT nodes[64];O_WORK work[64];memcpy(nodes,model->objP,n*sizeof(*nodes));memcpy(work,model->owP,n*sizeof(*work));
  for(unsigned speed=0;speed<256;++speed)for(unsigned update=0;update<8;++update)verify(record,mode,speed,update,0,(speed*17)%sys->rmthp[0].frm_num,&saved,nodes,work);
  for(unsigned slot=0;slot<10;++slot){for(unsigned frame=0;frame<sys->rmthp[slot].frm_num;++frame)verify(record,mode,1,2,slot,frame,&saved,nodes,work);for(unsigned update=0;update<8;++update)verify(record,mode,1,update,slot,sys->rmthp[slot].frm_num-1,&saved,nodes,work);}
  for(unsigned update=8;update<256;++update)verify(record,mode,255,update,0,77,&saved,nodes,work);
  *actor=saved;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));
 }
 printf("PASS %u original explicit-frame commands: modes 1/2/3, all frames/ten clips/both actors, speed/callback branches and final-to-first neighbor reads; %u independent matrix channels (max error %.9g)\n",cases,channels,maximum);
 unsigned char command[]={0x69,34,0,1,0,0,0,6,0,0};
 for(unsigned mode=1;mode<=3;++mode)for(unsigned frame=0;frame<65536;++frame){command[3]=mode;command[8]=frame;command[9]=frame>>8;check((bhCheckNativeRoomMotion(command,NULL)==NULL)==(frame<sys->rmthp[0].frm_num),"encoded frame guard disagrees with clip length");}command[3]=1;
 for(unsigned slot=0;slot<10;++slot){unsigned frame=sys->rmthp[slot].frm_num;command[4]=slot;command[8]=frame;command[9]=frame>>8;reject(command,10);}command[4]=0;command[8]=command[9]=0;
 for(unsigned bytes=1;bytes<10;++bytes)reject(command,bytes);
 MN_WORK *bank=sys->rmthp;sys->rmthp=(void *)(uintptr_t)1;reject(command,10);sys->rmthp=bank;
 for(unsigned mode=0;mode<256;++mode)if(mode!=1&&mode!=2&&mode!=3){command[3]=mode;reject(command,10);}command[3]=1;
 for(unsigned blend=0;blend<256;++blend)if(blend!=6){command[7]=blend;reject(command,10);}command[7]=6;
 /* A selected neighbor is read even at integer fraction zero. */
 float value;memcpy(&value,bank[0].md2P[0].p[0],4);float invalid=NAN;memcpy(bank[0].md2P[0].p[0],&invalid,4);unsigned frame=bank[0].frm_num-1;command[8]=frame;command[9]=frame>>8;reject(command,10);memcpy(bank[0].md2P[0].p[0],&value,4);command[8]=command[9]=0;

 unsigned char waiting[39]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0,0x65,1,0,0,0xfe};memcpy(waiting+25,command,10);waiting[35]=0xff;RxRoomScripts delayed;check(!rx_room_scripts_open(waiting,37,&delayed),"delayed explicit-frame fixture");rom->evtp=&delayed;check(!rx_events_start_room(),"start delayed frame command");BH_PWORK *target=&ene[rom->enep[0].wrk_no];BH_PWORK before=*target;bhEtask[0].work=(void *)(uintptr_t)1;check(rx_events_tick()!=NULL&&!memcmp(&before,target,sizeof(before)),"reassigned task reached original explicit-frame handler");
 check(original->data[2318]==0x69&&original->data[2319]==34,"actual original explicit-frame instruction");error=run(0,original->data+2318);check(!error,error);check(ene[rom->enep[0].wrk_no].frm_no==77<<16,"actual opening frame77");
 puts("PASS all 65536 encoded frame values for each mode 1/2/3, per-clip bounds/truncation/foreign-bank/unsupported-mode/interpolation/nonfinite wrap-neighbor rejection, foreign-task guard and actual frame77 opening bytes");
 check(original->bytes>5516&&!memcmp(original->data+5506,(unsigned char[]){0x69,34,0,2,3,0,0,6,10,0},10),"actual opening mode2 bytes");before=*target;error=run(0,original->data+5506);check(!error,error);check(target->mtn_no==3&&target->frm_no==(10<<16)&&target->mtn_add==0x8000&&target->mode0==before.mode0&&target->mode1==before.mode1&&target->mode2==before.mode2&&target->mode3==1&&target->frm_mode==0&&target->hokan_count==0&&target->ct1==before.ct1&&target->ct2==before.ct2,"actual opening mode2 state preservation");puts("PASS actual opening mode2 frame10/clip3 bytes, full clip replacement and original state/blend reset");
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);check(bhCheckNativeRoomMotion(command,NULL)!=NULL,"retired native room still accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
