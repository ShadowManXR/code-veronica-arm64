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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL enemy task: %s\n",why);exit(1);}}
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

#include "subpl.h"
#include "face_control_runtime.h"
#include "face_sampling.h"
#include "face_pose.h"
extern void (*subpl_mv00_tbl[15])(BH_PWORK*);

static BH_PWORK initial[128];static unsigned char slots[2];static NJS_CNK_OBJECT nodes[2][31];static O_WORK matrices[2][31];
static BH_PWORK *actor(unsigned record){return ene+rom->enep[record].wrk_no;}
static void restore(void){memcpy(ene,initial,sizeof(initial));for(unsigned i=0;i<2;++i){rom->enep[i].wrk_no=slots[i];memcpy(actor(i)->mlwP->objP,nodes[i],sizeof(nodes[i]));memcpy(actor(i)->mlwP->owP,matrices[i],sizeof(matrices[i]));}sys->ewk_n=2;}
static const unsigned states[]={0,0x01000000,0x40000000,0x41000000};
static const int timers[]={0,1,7,-1,INT_MAX};
static void setup(unsigned seed){restore();BH_PWORK copy[2]={*actor(0),*actor(1)};memset(ene,0,sizeof(initial));
 for(unsigned i=0;i<2;++i){unsigned slot=i?3:7;rom->enep[i].wrk_no=slot;BH_PWORK *a=ene+slot;*a=copy[i];a->mlwP=a->mdl;a->mtx=(void*)a->mtxbuf;a->mskp=NULL;
  a->mode0=1;a->mode2=0;a->mode3=3+(seed&1);a->ct2=0;a->mnwP=sys->emtp[a->id];a->flg=(a->flg&~0x80001083u)|((seed>>i)&1)|(((seed>>(i+2))&1)?0x80000000u:0);a->stflg=states[(seed>>(4+i*2))&3];
  a->flg2=0x8f0037u^(seed*37);a->comb_flg=0xabcdefffu^(seed*131);a->comb_timeout=timers[(seed+i)%5];a->px=i*4+2;a->py=-3;a->pz=(int)i*5-8;a->ax=seed*17;a->ay=seed*251;a->az=seed*31;
  a->pxb=a->pyb=a->pzb=999;a->axb=a->ayb=a->azb=-999;
  for(unsigned j=0;j<64;++j)a->dam[j]=(int)(0xa5110000u+seed*64+j);
 }
 sys->sp_flg|=2;sys->ewk_n=8;sys->enow=-777;sys->en_objn=-888;for(unsigned i=0;i<16;++i)sys->en_obj[i]=ene+127;
 plp->ofx=17;plp->ofy=18;plp->ofz=19;
}
static void verify(unsigned seed){setup(seed);BH_PWORK expected[128];memcpy(expected,ene,sizeof(expected));BH_PWORK player=*plp;player.ofx=player.ofz=0;
 BH_PWORK *objects[16];for(unsigned i=0;i<16;++i)objects[i]=ene+127;unsigned count=0;
 for(unsigned slot=0;slot<8;++slot){BH_PWORK *a=expected+slot;if(!(a->flg&1)||(a->stflg&0x41000000))continue;
  a->pxb=a->px;a->pyb=a->py;a->pzb=a->pz;a->axb=a->ax;a->ayb=a->ay;a->azb=a->az;a->flg=(a->flg|0x8100)&~2u;
  for(unsigned j=0;j<31;++j)a->dam[j]=0;if(a->comb_timeout)--a->comb_timeout;a->comb_flg&=~0x70u;a->flg2&=~4u;
  if(a->flg&0x80000000)objects[count++]=ene+slot;
 }
 const char *e=bhStepNativeRoomActors();if(e)fprintf(stderr,"seed%u: %s\n",seed,e);check(!e,e);
 check(!memcmp(&player,plp,sizeof(player)),"original player contact-offset prelude differs");check(sys->enow==7&&sys->en_objn==(int)count&&!memcmp(objects,sys->en_obj,sizeof(objects)),"pool cursor, object order/count or untouched list tail differs");
 for(unsigned slot=0;slot<128;++slot){BH_PWORK *a=expected+slot;int active=slot<8&&(a->flg&1)&&!(a->stflg&0x41000000);if(active){Matrix root;identity(root);transform(root,a->px,a->py,a->pz,a->ax,a->ay,a->az);matrix((float*)ene[slot].mtx,root);hierarchy(ene[slot].mlwP,ene[slot].mlwP->objP,root);memcpy(a->mtxbuf,ene[slot].mtxbuf,64);}check(!memcmp(a,ene+slot,sizeof(*a)),"original callback/damage cleanup or skipped actor state differs");}
}
static void reject(void){BH_PWORK pool[128];memcpy(pool,ene,sizeof(pool));BH_PWORK player=*plp;int cursor=sys->enow,number=sys->en_objn;BH_PWORK *list[16];memcpy(list,sys->en_obj,sizeof(list));NJS_CNK_OBJECT n[2][31];O_WORK w[2][31];for(unsigned i=0;i<2;++i){memcpy(n[i],initial[slots[i]].mdl[0].objP,sizeof(n[i]));memcpy(w[i],initial[slots[i]].mdl[0].owP,sizeof(w[i]));}
 check(bhStepNativeRoomActors()!=NULL,"invalid pool task accepted");check(!memcmp(pool,ene,sizeof(pool))&&!memcmp(&player,plp,sizeof(player))&&cursor==sys->enow&&number==sys->en_objn&&!memcmp(list,sys->en_obj,sizeof(list)),"invalid task changed actor/player/bookkeeping prefix");for(unsigned i=0;i<2;++i)check(!memcmp(n[i],initial[slots[i]].mdl[0].objP,sizeof(n[i]))&&!memcmp(w[i],initial[slots[i]].mdl[0].owP,sizeof(w[i])),"invalid task changed earlier world pose");
}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);check(sys->ewk_n==2&&rom->ene_n==2,"original opening pool fixture");
 memcpy(initial,ene,sizeof(initial));for(unsigned i=0;i<2;++i){slots[i]=rom->enep[i].wrk_no;check(actor(i)->mlwP->obj_num==31,"facial model extent");memcpy(nodes[i],actor(i)->mlwP->objP,sizeof(nodes[i]));memcpy(matrices[i],actor(i)->mlwP->owP,sizeof(matrices[i]));}
 for(unsigned seed=0;seed<4096;++seed)verify(seed);
 printf("PASS 4096 original enemy task updates across reversed record/pool order and holes, active/hidden/dormant masks, five combo timers, 31 damage slots with 33 preserved tail entries, player offset prelude and object-list ordering; %u independent world channels (max %.9g)\n",channels,maximum);
 for(unsigned bad=0;bad<12;++bad){setup(15);BH_PWORK *a=actor(0);switch(bad){case 0:sys->ewk_n=-1;break;case 1:sys->ewk_n=129;break;case 2:rom->enep[1].wrk_no=7;break;case 3:rom->enep[1].wrk_no=128;break;case 4:ene[2].flg=1;break;case 5:sys->ewk_n=7;break;case 6:ene[2].flg=0x80;ene[2].lkwkp=(void*)(uintptr_t)1;break;case 7:a->comb_timeout=INT_MIN;break;case 8:a->mlwP=(void*)(uintptr_t)1;break;case 9:a->ct2=15;break;case 10:a->id=1;break;case 11:a->px=NAN;break;}reject();}
 setup(15);unsigned char *registered=plp->exp0;plp->exp0=(void*)(uintptr_t)1;reject();plp->exp0=registered;
 puts("PASS thirteen malformed task contexts reject before any player/contact, actor/damage, matrix or bookkeeping write");
 setup(15);sys->sp_flg&=~2u;BH_PWORK pool[128];memcpy(pool,ene,sizeof(pool));check(!bhStepNativeRoomActors()&&!memcmp(pool,ene,sizeof(pool))&&plp->ofx==0&&plp->ofz==0&&plp->ofy==18&&sys->enow==-777&&sys->en_objn==-888,"paused source prelude/task semantics");
 restore();for(unsigned i=0;i<128;++i)ene[i].flg=0;sys->sp_flg|=2;sys->ewk_n=0;sys->en_objn=9;sys->enow=77;check(!bhStepNativeRoomActors()&&sys->en_objn==0&&sys->enow==77,"empty source task semantics");
 BH_PWORK before=ene[0];check(bhStepNativeEnemyController(ene)!=NULL&&!memcmp(&before,ene,sizeof(before)),"unguarded direct native callback admitted");puts("PASS source paused/empty task semantics and rejected out-of-task controller call; hostile AI remains gated");
 restore();check(!o.events_ready,"full opening unexpectedly enabled");rx_opening_end(&o);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
