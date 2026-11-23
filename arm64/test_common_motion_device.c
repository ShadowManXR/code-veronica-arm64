#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "player_control_runtime.h"
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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL common motion: %s\n",why);exit(1);}}
typedef double Matrix[16];
static void identity(Matrix m){memset(m,0,sizeof(Matrix));m[0]=m[5]=m[10]=m[15]=1;}
static float sine(int angle){unsigned n=(unsigned)angle&65535,index=n&16383;if(n&16384)index=16383-index;float s=sinf(0.0000958738f*index);return n&32768?-s:s;}
static void product(Matrix a,const Matrix b){Matrix r;for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row){double v=0;for(unsigned k=0;k<4;++k)v+=a[k*4+row]*b[c*4+k];r[c*4+row]=v;}memcpy(a,r,sizeof(r));}
static void rotation(Matrix m,unsigned axis,int angle){Matrix r;identity(r);unsigned p=(axis+1)%3,q=(axis+2)%3;double s=sine(angle),c=sine(angle+16384);r[p*4+p]=r[q*4+q]=c;r[p*4+q]=s;r[q*4+p]=-s;product(m,r);}
static void transform(Matrix m,float x,float y,float z,int ax,int ay,int az){Matrix t;identity(t);t[12]=x;t[13]=y;t[14]=z;product(m,t);rotation(m,2,az);rotation(m,1,ay);rotation(m,0,ax);}
static unsigned channels;static double maximum;
static void matrix(const float *got,const Matrix expected){for(unsigned i=0;i<16;++i){double d=fabs(got[i]-expected[i]);if(d>maximum)maximum=d;check(isfinite(got[i])&&d<0.001,"native world/link matrix differs from independent hierarchy");++channels;}}
static void hierarchy(ML_WORK *model,const NJS_CNK_OBJECT *node,const Matrix parent){while(node){ptrdiff_t index=node-model->objP;check(index>=0&&(unsigned)index<model->obj_num,"reference node extent");Matrix current;memcpy(current,parent,sizeof(current));transform(current,node->pos[0],node->pos[1],node->pos[2],node->ang[0],node->ang[1],node->ang[2]);check(!(model->owP[index].flg&1),"fixture has an unhandled matrix override");matrix(model->owP[index].mtx,current);if(node->child)hierarchy(model,node->child,current);node=node->sibling;}}
static unsigned char data[64];static RxRoomScripts scripts;
static const char *execute(const unsigned char command[8]){memset(data,0,sizeof(data));data[0]=8;data[4]=28;const unsigned char prefix[]={5,1,30,0,0,0};memcpy(data+8,prefix,6);memcpy(data+14,command,8);/* pad with native no-ops through entry end */
 data[4]=24;check(!rx_room_scripts_open(data,26,&scripts),"common motion fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~2u;return rx_events_start_room();}
static unsigned char task_data[64];static RxRoomScripts task_scripts;
static const char *run_n(const unsigned char *command,size_t bytes){memset(task_data,0,sizeof(task_data));task_data[0]=12;task_data[4]=18;task_data[8]=20;const unsigned char entry[]={20,0,0,0,0,0};memcpy(task_data+12,entry,6);const unsigned char select[]={0x65,0,0,0};memcpy(task_data+20,select,4);memcpy(task_data+24,command,bytes);task_data[24+bytes]=0xff;
 check(!rx_room_scripts_open(task_data,26+bytes,&task_scripts),"native motion task fixture");rom->evtp=&task_scripts;return rx_events_start_room();}
static const char *run(const unsigned char command[8]){return run_n(command,8);}
static void reject(unsigned char command[8]){BH_PWORK before=*plp;unsigned word=sys->ev_flg[0];sys->ev_flg[0]=0;check(execute(command)!=NULL&&!sys->ev_flg[0]&&!memcmp(&before,plp,sizeof(before)),"invalid motion request executed prefix or changed player");sys->ev_flg[0]=word;}
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"full opening must remain gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next original player dependency");const RxRoomScripts *original=rom->evtp;check(original->bytes>2210&&original->data[2202]==0x69&&original->data[2203]==25,"actual opening common motion bytes");
 printf("Native player nodes %u, current slot %u, bank %p, objects %d, hands %u/%u\n",plp->mlwP->obj_num,plp->mtn_no,(void *)sys->plmthp,rom->obj_n,sys->obwp[0].mlwP->obj_num,sys->obwp[1].mlwP->obj_num);
 BH_PWORK saved=*plp;EXP_WORK scalar=opening.player_work;ML_WORK *model=plp->mlwP;unsigned n=model->obj_num;check(n<=64,"player reference capacity");
 NJS_CNK_OBJECT nodes[64];O_WORK work[64];memcpy(nodes,model->objP,n*sizeof(*nodes));memcpy(work,model->owP,n*sizeof(*work));O_WRK hands[2]={sys->obwp[0],sys->obwp[1]},shadow=eff[0];
 NJS_CNK_OBJECT hand_nodes[2][64];O_WORK hand_work[2][64];for(unsigned side=0;side<2;++side){check(hands[side].mlwP->obj_num<=64,"hand reference capacity");memcpy(hand_nodes[side],hands[side].mlwP->objP,hands[side].mlwP->obj_num*sizeof(NJS_CNK_OBJECT));memcpy(hand_work[side],hands[side].mlwP->owP,hands[side].mlwP->obj_num*sizeof(O_WORK));}
 const unsigned rates[]={0x8000,0x10000,0x10000,0x8000,0x5555,0x4000,0x3333,0x2aaa,0x8000,0x4000,0x2aaa,0x2000,0x1999,0x1555,0x2492,0x2000,0x1c71,0x1999,0x1745,0x1555,0x1249,0x1000,0xe38,0xccc,0xba2,0xaaa};
 unsigned cases=0;
 for(unsigned speed=0;speed<256;++speed)for(unsigned update=0;update<8;++update){
  *plp=saved;opening.player_work=scalar;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));for(unsigned side=0;side<2;++side){sys->obwp[side]=hands[side];memcpy(hands[side].mlwP->objP,hand_nodes[side],hands[side].mlwP->obj_num*sizeof(NJS_CNK_OBJECT));memcpy(hands[side].mlwP->owP,hand_work[side],hands[side].mlwP->obj_num*sizeof(O_WORK));}
  eff[0]=shadow;plp->px=3.5f;plp->py=-2;plp->pz=7;plp->ax=speed*137;plp->ay=speed*251;plp->az=speed*17;plp->mtn_add=12345;
  unsigned char command[]={0x69,25,speed,0,42,speed,update,6};
  error=run(command);check(!error,error);check(plp->mode0==7&&plp->mode1==42&&plp->mode2==8&&plp->mode3==0&&plp->ct0==(int)speed&&plp->ct1==(int)update&&plp->ct2==6&&plp->mnwP==(void *)sys->plmthp&&plp->hokan_count==0&&(plp->mtn_md&8)&&plp->frm_no==0&&plp->mtn_add==(speed<26?rates[speed]:12345),"original rate/mode/bank/frame setup differs");
  int calc=update==0||update==3||update==5||update==7,link=update==0||update==4||update==6||update==7,motion=update==0||update==2||update==5||update==6;
  Matrix root;identity(root);if(calc){transform(root,plp->px,plp->py,plp->pz,plp->ax,plp->ay,plp->az);matrix((float *)plp->mtx,root);hierarchy(model,model->objP,root);}else check(!memcmp(plp->mtxbuf,saved.mtxbuf,64)&&!memcmp(model->owP,work,n*sizeof(*work)),"update selector unexpectedly recalculated player matrices");
  if(!motion)check(!memcmp(model->objP,nodes,n*sizeof(*nodes)),"update selector unexpectedly evaluated motion");
  for(unsigned side=0;side<2;++side){O_WRK *child=sys->obwp+side;if(link){for(unsigned i=0;i<16;++i)root[i]=model->owP[child->lkono].mtx[i];transform(root,child->lox,child->loy,child->loz,child->ax,child->ay,child->az);matrix((float *)child->mtx,root);hierarchy(child->mlwP,child->mlwP->objP,root);}else check(!memcmp(child,&hands[side],sizeof(*child))&&!memcmp(child->mlwP->owP,hand_work[side],child->mlwP->obj_num*sizeof(O_WORK)),"update selector unexpectedly changed linked hand");}

  if(link){for(unsigned i=0;i<16;++i)root[i]=model->owP[shadow.lkono].mtx[i];transform(root,shadow.lox,shadow.loy,shadow.loz,shadow.ax,shadow.ay,shadow.az);matrix((float *)eff[0].mtx,root);}else check(!memcmp(&eff[0],&shadow,sizeof(shadow)),"update selector unexpectedly changed shadow link");
  ++cases;
 }
 printf("PASS %u original common motion setups, all speed bytes/update branches; %u independent player/hand matrix channels, max error %.9g\n",cases,channels,maximum);
 *plp=saved;opening.player_work=scalar;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));for(unsigned side=0;side<2;++side)sys->obwp[side]=hands[side];
 error=run(original->data+2202);check(!error,error);check(plp->mnwP==(void *)sys->plmthp&&plp->mode1==42,"actual original motion command");
 check(original->bytes>2212&&original->data[2210]==0x69&&original->data[2211]==47,"actual original recalc bytes");
 O_WRK child_before[2]={sys->obwp[0],sys->obwp[1]},shadow_before=eff[0];int frame_before=plp->frm_no;
 error=run_n(original->data+2210,2);check(!error,error);Matrix recalc;identity(recalc);transform(recalc,plp->px,plp->py,plp->pz,plp->ax,plp->ay,plp->az);matrix((float *)plp->mtx,recalc);hierarchy(model,model->objP,recalc);
 check(plp->frm_no==frame_before&&!memcmp(child_before,sys->obwp,sizeof(child_before))&&!memcmp(&shadow_before,&eff[0],sizeof(shadow_before)),"original model-only recalc advanced motion or links");
 unsigned char command[]={0x69,25,1,0,42,0,0,6};
 for(unsigned mode=1;mode<256;++mode){command[3]=mode;reject(command);}command[3]=0;
 for(unsigned blend=0;blend<256;++blend)if(blend!=6){command[7]=blend;reject(command);}command[7]=6;
 unsigned char *bank=sys->plmthp;sys->plmthp=(unsigned char *)(uintptr_t)1;reject(command);sys->plmthp=bank;
 void *keys=plp->mnwPb[42].md2P[0].p[0];plp->mnwPb[42].md2P[0].p[0]=(void *)(uintptr_t)1;reject(command);plp->mnwPb[42].md2P[0].p[0]=keys;
 NJS_MATRIX *mtx=plp->mtx;plp->mtx=(void *)(uintptr_t)1;reject(command);plp->mtx=mtx;
 int count=lNaMatMatrixStuckMax;lNaMatMatrixStuckMax=lNaMatMatrixStuckCnt;reject(command);lNaMatMatrixStuckMax=count;
 int objects=rom->obj_n;rom->obj_n=33;reject(command);rom->obj_n=objects;
 /* A parent with identical low 32 bits must not be mistaken for this actor. */
 O_WRK effect=eff[511];memset(&eff[511],0,sizeof(eff[511]));eff[511].flg=0x81;eff[511].lkwkp=(void *)((uintptr_t)plp^UINT64_C(0x100000000));eff[511].mtx=(void *)(uintptr_t)1;O_WRK alias=eff[511];
 error=run(command);check(!error,error);check(!memcmp(&alias,&eff[511],sizeof(alias)),"linked identity truncated a native pointer");eff[511].lkwkp=(void *)plp;reject(command);eff[511]=effect;
 puts("PASS original instruction bytes, full-width linked identity, foreign bank/track/matrix/stack/pool rejection and gated interpolation/actor modes");

 /* Original standing command: independently derive scalar state, pose keys and
    world hierarchy. Do not call the source routine to construct the oracle. */
 *plp=saved;opening.player_work=scalar;
 const int health[]={INT_MIN,-1,0,1,29,30,31,119,120,121,INT_MAX};
 unsigned standing=0;unsigned before_channels=channels;
 for(unsigned group=0;group<2;++group)for(unsigned h=0;h<sizeof(health)/sizeof(*health);++h)
 for(unsigned injury=0;injury<4;++injury)for(unsigned old=0;old<4;++old){
  *plp=saved;opening.player_work=scalar;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));
  opening.player_work.wpntp=group;opening.player_work.dmlvl=(int[]){INT_MIN,-1,0,INT_MAX}[old];
  plp->hp=health[h];plp->stflg=(saved.stflg&~0x280000u)|((injury&1)?0x80000u:0)|((injury&2)?0x200000u:0)|0x18000u;
  plp->flg=0xffffffffu&~0x1080u;plp->px=3.5f;plp->py=-2;plp->pz=7;plp->ax=standing*13;plp->ay=standing*251;plp->az=standing*17;
  plp->mode0=7;plp->mode1=42;plp->mode2=8;plp->mode3=99;plp->frm_no=0x10000;plp->hokan_rate=231;plp->hokan_count=7;
  BH_PWORK expected=*plp;EXP_WORK expected_scalar=opening.player_work;
  unsigned level=health[h]>=120?(injury!=0):health[h]>=30?1:2,slot=42+level;
  expected_scalar.dmlvl=level;expected_scalar.spx=expected.px;expected_scalar.spz=expected.pz;
  expected.flg=(expected.flg&0xc96efffbu)|8;expected.stflg&=~0x18000u;
  expected.mode0=1;expected.mode1=expected.mode2=expected.mode3=0;expected.mtn_no=slot;
  expected.hokan_rate=expected.hokan_count=expected.frm_no=0;expected.mnwP=expected.mnwPb;
  if(expected.mnwP[slot].atrP)expected.mtn_attr=expected.mnwP[slot].atrP[0];
  O_WRK links_before[2]={sys->obwp[0],sys->obwp[1]},effect_before=eff[0];
  error=run_n((unsigned char[]){0x64,52},2);check(!error,error);
  Matrix root;identity(root);transform(root,expected.px,expected.py,expected.pz,expected.ax,expected.ay,expected.az);matrix((float *)plp->mtx,root);
  /* The original fast format sets root translation and every unlocked angle. */
  NJS_CNK_OBJECT expected_nodes[64];memcpy(expected_nodes,nodes,n*sizeof(*nodes));
  MN_WORK *clip=expected.mnwP+slot;check(!(clip->flg&0x30),"standing reference fast format");
  const float *position=clip->md2P[0].p[0];memcpy(expected_nodes[0].pos,position,3*sizeof(float));
  for(unsigned j=0;j<n;++j)if(!(work[j].flg&6)){
   const unsigned short *angle=clip->md2P[j].p[1];for(unsigned axis=0;axis<3;++axis)expected_nodes[j].ang[axis]=angle?angle[axis]:0;
  }
  check(!memcmp(expected_nodes,model->objP,n*sizeof(*nodes)),"standing pose differs from independent frame-zero keys");hierarchy(model,model->objP,root);
  memcpy(expected.mtxbuf,plp->mtxbuf,sizeof(expected.mtxbuf));
  check(!memcmp(&expected,plp,sizeof(expected))&&!memcmp(&expected_scalar,&opening.player_work,sizeof(expected_scalar)),"standing player/scalar state differs from original contract");
  check(!memcmp(links_before,sys->obwp,sizeof(links_before))&&!memcmp(&effect_before,&eff[0],sizeof(effect_before)),"standing command unexpectedly recalculated linked hands/shadow");++standing;
 }
 printf("PASS %u original standing resets: health boundaries, both weapon groups, injury bits, arbitrary old levels; %u independent matrix channels and frame-zero keys\n",standing,channels-before_channels);
 *plp=saved;opening.player_work=scalar;
 /* Missing/foreign player context must reject the whole stream before flags. */
 unsigned char stand[8]={0x64,52};
 opening.player_work.wpntp=-1;reject(stand);opening.player_work.wpntp=2;reject(stand);opening.player_work=scalar;
 unsigned char *owned_scalar=plp->exp0;plp->exp0=(void *)(uintptr_t)1;reject(stand);plp->exp0=owned_scalar;
 void *stand_track=plp->mnwPb[42].md2P[0].p[0];plp->mnwPb[42].md2P[0].p[0]=(void *)(uintptr_t)1;reject(stand);plp->mnwPb[42].md2P[0].p[0]=stand_track;
 check(original->data[2216]==0x64&&original->data[2217]==52,"actual original standing bytes");error=run_n(original->data+2216,2);check(!error,error);
 puts("PASS original opening standing bytes and atomic invalid group/scalar/track rejection");

 *plp=saved;opening.player_work=scalar;unsigned handoffs=0;
 for(unsigned a=0;a<66;++a)for(unsigned b=0;b<66;++b){
  *plp=saved;opening.player_work=scalar;
  unsigned bits=a<2?(a?~0u:0):a<34?1u<<(a-2):~(1u<<(a-34));
  unsigned status=b<2?(b?~0u:0):b<34?1u<<(b-2):~(1u<<(b-34));
  plp->flg=bits;plp->stflg=status;plp->mode0=7;plp->mode1=42;plp->mode2=8;plp->mode3=99;plp->mtn_add=1234;plp->mnwP=NULL;
  BH_PWORK expected=*plp;expected.mtn_add=65536;expected.flg=(bits|0x10)&~0x210000u;expected.stflg=status&~0x10080u; /* LoadWork clears 0x80 before handoff. */
  expected.mnwP=expected.mnwPb;expected.mode0=1;expected.mode1=expected.mode2=expected.mode3=0;
  error=run_n((unsigned char[]){0x64,128},2);check(!error,error);
  if(memcmp(plp,&expected,sizeof(expected))||memcmp(&scalar,&opening.player_work,sizeof(scalar))||bhEtask[0].mode0||bhEtask[0].mode1){
   printf("Handoff case %u/%u task %u/%u\n",a,b,bhEtask[0].mode0,bhEtask[0].mode1);
   for(unsigned byte=0;byte<sizeof(expected);++byte)if(((unsigned char *)plp)[byte]!=((unsigned char *)&expected)[byte])printf("Player offset %u actual %02x expected %02x\n",byte,((unsigned char *)plp)[byte],((unsigned char *)&expected)[byte]);
   check(0,"original handoff state or task release differs");
  }++handoffs;
 }
 *plp=saved;opening.player_work=scalar;
 bank=sys->plmthp;sys->plmthp=(void *)(uintptr_t)1;reject((unsigned char[]){0x64,128,0,0,0,0,0,0});sys->plmthp=bank;
 check(original->data[2218]==0x64&&original->data[2219]==128,"actual original handoff bytes");error=run_n(original->data+2218,2);check(!error,error);
 for(unsigned sub=52;sub<=128;sub+=76){
  *plp=saved;opening.player_work=scalar;
  unsigned char waiting[35]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0,0x65,0,0,0,0xfe,0x64,sub,5,1,30,0,0,0,0xff,0};
  RxRoomScripts bad;check(!rx_room_scripts_open(waiting,sizeof(waiting),&bad),"delayed standing/handoff fixture");rom->evtp=&bad;check(!rx_events_start_room(),"start delayed standing/handoff");
  BH_PWORK before=*plp;EXP_WORK previous=opening.player_work;bhEtask[0].work=(void *)(uintptr_t)1;unsigned char task_mode=bhEtask[0].mode0;
  check(rx_events_tick()!=NULL&&!memcmp(plp,&before,sizeof(before))&&!memcmp(&previous,&opening.player_work,sizeof(previous))&&bhEtask[0].mode0==task_mode,"foreign task reached original standing/handoff handler");
 }
 printf("PASS %u original control handoffs, all flag bits/complements, source task release, actual opening bytes and foreign-target guards\n",handoffs);
 rx_events_reset();rom->evtp=(void *)original;rx_opening_end(&opening);check(!sys->plmthp&&!plp->exp0&&rx_player_control_check(),"native script model teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}

