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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL continuous actor: %s\n",why);exit(1);}}
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


static BH_PWORK saved[2];static NJS_CNK_OBJECT nodes[2][64];static O_WORK work[2][64];
static BH_PWORK *actor(unsigned i){return &ene[rom->enep[i].wrk_no];}
static void restore(void){for(unsigned i=0;i<2;++i){*actor(i)=saved[i];memcpy(actor(i)->mlwP->objP,nodes[i],sizeof(*nodes[i])*actor(i)->mlwP->obj_num);memcpy(actor(i)->mlwP->owP,work[i],sizeof(*work[i])*actor(i)->mlwP->obj_num);}}
static void setup(unsigned i,unsigned clip,unsigned mode,unsigned reset,int fixed,int step,unsigned md){BH_PWORK *a=actor(i);a->mskp=NULL;a->flg=(a->flg&~0x1080u)|1;a->stflg=0;a->mode0=mode;a->mode1=clip;a->mode2=0;a->mode3=reset?0:1;a->ct2=6;a->mnwP=sys->rmthp;a->mtn_tp=NULL;a->mtn_md=md;a->mtn_no=reset?9:clip;a->frm_no=fixed;a->mtn_add=step;a->hokan_count=reset?17:0;a->frm_mode=3;a->spd=0.125f;a->px=3.5f;a->py=-2;a->pz=7;a->ax=137;a->ay=251;a->az=17;}
static unsigned cases;
static void verify(unsigned record,unsigned clip,unsigned mode,unsigned reset,int fixed,int step,unsigned md){
 restore();setup(record,clip,mode,reset,fixed,step,md);actor(1-record)->stflg=0x40000000;
 BH_PWORK *a=actor(record),expected=*a;ML_WORK *model=a->mlwP;NJS_CNK_OBJECT ref[64];memcpy(ref,model->objP,model->obj_num*sizeof(*ref));
 expected.pxb=a->px;expected.pyb=a->py;expected.pzb=a->pz;expected.axb=a->ax;expected.ayb=a->ay;expected.azb=a->az;expected.flg=(expected.flg|0x8100)&~2u;
 if(reset){expected.mode3=1;expected.mtn_no=clip;expected.hokan_count=0;expected.frm_mode=0;if(mode!=1)expected.spd=0;}
 MN_WORK *m=sys->rmthp+clip;unsigned frame=fixed>>16,neighbor=frame+1<m->frm_num?frame+1:0;float fraction=(fixed&65535)*0.000015258789f;int slow=(m->flg&0x20)||(md&8);
 if(mode==1){int next=fixed+step,period=m->frm_num*65536;if(next<0)next+=period;if(next>=period)next-=period;expected.frm_no=next;if(m->atrP)expected.mtn_attr=m->atrP[frame];
  for(unsigned j=0;j<m->obj_num;++j){unsigned joint=j<6?j:j+9;const float *key=m->md2P[j].p[0];if(key&&(slow||(m->flg&0x30)||!j))for(unsigned k=0;k<3;++k){float v=key[frame*3+k];if(slow)v+=fraction*(key[neighbor*3+k]-v);ref[joint].pos[k]=v;}
   if(!(model->owP[joint].flg&2)){const unsigned short *ang=m->md2P[j].p[1];if(ang)for(unsigned k=0;k<3;++k){float v=slow?(short)ang[frame*3+k]:ang[frame*3+k];if(slow)v+=fraction*(short)(ang[neighbor*3+k]-ang[frame*3+k]);ref[joint].ang[k]=(int)v;}else {ref[joint].ang[0]=0;if(!slow&&!(m->flg&0x30))ref[joint].ang[1]=ref[joint].ang[2]=0;}}
  }
 }
 const char *error=bhStepNativeRoomActors();if(error)fprintf(stderr,"record%u clip%u mode%u frame%d: %s\n",record,clip,mode,fixed,error);check(!error,error);
 check(!memcmp(ref,model->objP,model->obj_num*sizeof(*ref)),"motion keys, fractional interpolation or protected nodes differ");
 Matrix root;identity(root);transform(root,a->px,a->py,a->pz,a->ax,a->ay,a->az);matrix((float*)a->mtx,root);hierarchy(model,model->objP,root);
 memcpy(expected.mtxbuf,a->mtxbuf,sizeof(expected.mtxbuf));check(!memcmp(&expected,a,sizeof(expected)),"original callback/backup modified unexpected actor fields");++cases;
}
static void rejected(void){BH_PWORK before[2]={*actor(0),*actor(1)};NJS_CNK_OBJECT n[2][64];O_WORK w[2][64];for(unsigned i=0;i<2;++i){memcpy(n[i],saved[i].mlwP->objP,sizeof(*nodes[i])*31);memcpy(w[i],saved[i].mlwP->owP,sizeof(*work[i])*31);}check(bhStepNativeRoomActors()!=NULL,"invalid batch accepted");for(unsigned i=0;i<2;++i){check(!memcmp(before+i,actor(i),sizeof(*before)),"invalid batch advanced actor prefix");check(!memcmp(n[i],saved[i].mlwP->objP,sizeof(*nodes[i])*31)&&!memcmp(w[i],saved[i].mlwP->owP,sizeof(*work[i])*31),"invalid batch wrote pose prefix");}}
static unsigned char bank[38]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0};
static RxRoomScripts scripts;
static void begin(const unsigned char command[6]){
 const unsigned char body[]={0xfc,8,0x38,0,0,0,0,0,0xfe,0xf3,5,1,30,0,0,0,0xff,0};
 memcpy(bank+20,body,sizeof(body));memcpy(bank+22,command,6);check(!rx_room_scripts_open(bank,sizeof(bank),&scripts),"native wait fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~2u;
 const char *error=rx_events_start_room();check(!error,error);
}
static void tick(void){const char *error=rx_events_tick();check(!error,error);}
static int done(void){return (sys->ev_flg[0]&2)&&!bhEtask[0].status;}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&opening,0);check(!e,e);RxEventFault fault;check(!rx_events_validate_room(&fault)&&opening.commands_ready&&!opening.events_ready,"command coverage and live gate");
 for(unsigned i=0;i<2;++i){saved[i]=*actor(i);check(saved[i].mlwP->obj_num==31,"facial model count");memcpy(nodes[i],saved[i].mlwP->objP,sizeof(*nodes[i])*31);memcpy(work[i],saved[i].mlwP->owP,sizeof(*work[i])*31);}unsigned flags=sys->sp_flg;sys->sp_flg|=2;
 const int steps[]={-65536,-32768,0,32768,65536};
 for(unsigned i=0;i<2;++i)for(unsigned clip=0;clip<10;++clip)for(unsigned reset=0;reset<2;++reset)for(unsigned md=0x20;md<=0x28;md+=8){unsigned last=sys->rmthp[clip].frm_num-1;int frames[]={0,32768,(int)last*65536,(int)last*65536+32768};for(unsigned f=0;f<4;++f)for(unsigned s=0;s<5;++s)verify(i,clip,1,reset,frames[f],steps[s],md);for(unsigned mode=5;mode<=6;++mode)verify(i,clip,mode,reset,frames[3],65536,md);}
 for(unsigned i=0;i<2;++i)for(unsigned clip=0;clip<10;++clip)for(unsigned f=0;f<sys->rmthp[clip].frm_num;++f)verify(i,clip,1,0,f*65536,65536,0x28);
 printf("PASS %u original actor callback cases; %u independent world matrix channels, max error %.9g\n",cases,channels,maximum);
 for(unsigned bad=0;bad<18;++bad){restore();setup(0,0,1,0,0,65536,0x28);setup(1,0,1,0,0,65536,0x28);BH_PWORK *a=actor(1);switch(bad){case 0:a->mode0=2;break;case 1:a->mode2=1;break;case 2:a->mode3=2;break;case 3:a->ct2=15;break;case 4:a->mnwP=(void*)(uintptr_t)1;break;case 5:a->mtn_no=255;break;case 6:a->frm_no=-1;break;case 7:a->frm_no=sys->rmthp[0].frm_num*65536;break;case 8:a->hokan_count=1;break;case 9:a->mtn_md=0x22;break;case 10:a->mtn_tp=(void*)(uintptr_t)1;break;case 11:a->flg|=0x80;break;case 12:a->px=NAN;break;case 13:a->px=1e30f;break;case 14:a->mlwP->owP[0].flg=1;break;case 15:a->mlwP->objP[0].child=NULL;break;case 16:a->mtn_add=INT_MAX;break;case 17:a->mlwP->objP[0].pos[0]=1e30f;break;}rejected();}
 restore();sys->sp_flg&=~2u;BH_PWORK before[2]={*actor(0),*actor(1)};check(!bhStepNativeRoomActors()&&!memcmp(before,actor(0),sizeof(*before))&&!memcmp(before+1,actor(1),sizeof(*before)),"paused actor phase must not write or dispatch unsupported callbacks");sys->sp_flg=flags;
 puts("PASS paused original phase and 18 malformed second-actor batches rejected before any actor/pose writes");
 RxRoomScripts *original=rom->evtp;unsigned event_flags=sys->ev_flg[0];restore();setup(0,0,1,0,0,32768,0x28);actor(1)->stflg=0x40000000;sys->sp_flg|=2;
 unsigned char wait[]={0x38,1,0,0,2,0};begin(wait);check(!done(),"actor wait released before motion");
 for(unsigned frame=1;frame<=4;++frame){check(!bhStepNativeRoomActors(),"continuous actor phase failed before script wait");tick();check(done()==(frame==4),"script wait did not release at original exact motion boundary");if(frame==1){sys->sp_flg&=~2u;check(!bhStepNativeRoomActors(),"paused phase rejected");tick();check(!done()&&actor(0)->frm_no==32768,"paused actor advanced a script wait");sys->sp_flg|=2;}}
 setup(0,0,1,0,0,-65536,0x28);wait[4]=1;begin(wait);check(!done(),"reverse wait already released");check(!bhStepNativeRoomActors(),"reverse original actor phase");tick();check(done()&&actor(0)->frm_no==(int)(sys->rmthp[0].frm_num-1)*65536,"reverse wrap predicate differs from original signed clock");
 rx_events_reset();rom->evtp=original;sys->ev_flg[0]=event_flags;sys->sp_flg=flags;puts("PASS original suspended FrameCheck/While/EvtNext waits driven by per-frame actor motion, half-frame threshold, pause and reverse wrap in enemy-before-event order");
 restore();rx_opening_end(&opening);check(bhStepNativeRoomActors()!=NULL,"retired room owner accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
