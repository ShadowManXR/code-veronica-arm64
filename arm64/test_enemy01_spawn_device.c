#include "opening.h"
#include "boot_disc.h"
#include "enemy01_work.h"
#include "en01.h"
#include "eneset.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
extern EGG_WORK zob_lkmtab;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL enemy01 allocation/links: %s\n",why);exit(1);}}
static BH_PWORK pool[128];static unsigned char *game_cursor,*game_origin,*game_end;static float ground[32];
static void allocator_cases(RxOpening *o){
 unsigned char *scratch=aligned_alloc(64,8192);check(scratch&&(uintptr_t)scratch>UINT32_MAX,"high native arena fixture");
 rx_enemy_arena_release(o->arena);check(!rx_enemy_arena_bind(scratch,8192),"scratch arena registration");sys->mempb=scratch;sys->endp=scratch+8192;unsigned cases=0;
 for(unsigned pass=0;pass<2;++pass)for(unsigned offset=0;offset<128;++offset)for(unsigned bytes=0;bytes<256;++bytes){memset(scratch,0xa5,8192);unsigned char *current=scratch+(pass?8192-offset:offset);sys->memp=current;uintptr_t expected=((uintptr_t)current+63)&~(uintptr_t)63;int fits=expected<=(uintptr_t)sys->endp&&bytes<=(uintptr_t)sys->endp-expected;
  unsigned char *got=bhEne_CallocWork(bytes,8);check(got==(fits?(void*)expected:NULL),"original aligned allocation pointer or capacity check");check(sys->memp==(fits?(unsigned char*)expected+bytes:current),"original allocation cursor/failed prefix");
  for(unsigned i=0;i<8192;++i){unsigned char value=fits&&(uintptr_t)(scratch+i)>=expected&&(uintptr_t)(scratch+i)<expected+bytes?0:0xa5;check(scratch[i]==value,"allocation changed padding/tail or failed to zero bytes");}++cases;
 }
 printf("PASS %u original bhEne_CallocWork updates: all 64 alignment residues, zero-sized/positive/exact-end/overflow requests, byte-exact zeroing and untouched padding/tail above 4 GiB\n",cases);
 for(unsigned bad=0;bad<7;++bad){sys->memp=scratch+128;sys->mempb=scratch;sys->endp=scratch+8192;int size=32;switch(bad){case 0:size=-1;break;case 1:size=INT_MIN;break;case 2:size=INT_MAX;break;case 3:sys->memp=(void*)((uintptr_t)scratch-1);break;case 4:sys->memp=scratch+8193;break;case 5:sys->mempb=scratch+129;break;case 6:sys->endp=scratch+8191;break;}unsigned char *before=sys->memp;check(!bhEne_CallocWork(size,8)&&sys->memp==before,"invalid arena state changed cursor");}
 sys->memp=scratch+128;sys->mempb=scratch;sys->endp=scratch+8192;
 check(rx_enemy_arena_bind(NULL,8192)&&rx_enemy_arena_bind(scratch+1,8191)&&rx_enemy_arena_bind(scratch,0)&&rx_enemy_arena_bind((void*)(UINTPTR_MAX-63),128),"bad arena registration admitted");check(rx_enemy_arena_bind(scratch+64,4096)!=NULL&&rx_enemy_arena_preview(16)==scratch+128,"occupied registration replaced");rx_enemy_arena_release(scratch+64);check(rx_enemy_arena_preview(16)==scratch+128,"foreign release retired owner");rx_enemy_arena_release(scratch);check(!bhEne_CallocWork(16,8),"retired arena allocated");
 check(!rx_enemy_arena_bind(o->arena,(uintptr_t)game_end-(uintptr_t)o->arena),"restore native game arena");sys->memp=game_cursor;sys->mempb=game_origin;sys->endp=game_end;free(scratch);
}
static void reject_allocate(BH_PWORK *a){unsigned char *cursor=sys->memp;BH_PWORK before=*a;check(!rx_enemy01_work_allocate(a)&&sys->memp==cursor&&!memcmp(&before,a,sizeof(before)),"invalid typed allocation changed actor/cursor");}
static void typed_cases(void){
 for(unsigned i=2;i<128;++i){ene[i].id=1;ene[i].exp0=NULL;unsigned char *cursor=sys->memp;unsigned char *expected=(void*)(((uintptr_t)cursor+63)&~(uintptr_t)63);BH_PWORK before=ene[i];unsigned char *got=rx_enemy01_work_allocate(ene+i);check(got==expected&&(uintptr_t)got>UINT32_MAX,"typed work allocation lost high address/alignment");RxEnemy01Work *w=rx_enemy01_work_get(ene+i);check(w&&(unsigned char*)w==got&&sys->memp==expected+sizeof(*w),"typed arena binding/cursor");for(unsigned b=0;b<sizeof(*w);++b)check(!((unsigned char*)w)[b],"typed pointer/scalar work was not zeroed");before.exp0=got;check(!memcmp(&before,ene+i,sizeof(before)),"fresh allocation changed non-work actor state");w->children[0]=ene;int shadow=(int)(0x80000000u+i);memcpy(w->scalar+0x24,&shadow,4);RxEnemy01Work copy;memcpy(&copy,w,sizeof(copy));reject_allocate(ene+i);check(!memcmp(&copy,w,sizeof(copy)),"occupied allocation reset existing work");check(!rx_enemy01_work_reinitialize(ene+i)&&w->children[0]==ene&&!memcmp(w->scalar+0x24,&shadow,4),"existing-work original reset differs from fresh allocation");}
 rx_enemy01_work_reset();memcpy(ene,pool,sizeof(pool));sys->memp=game_cursor;
 BH_PWORK *a=ene+126;a->id=1;unsigned char *cursor=sys->memp;sys->memp=game_end-1;reject_allocate(a);sys->memp=cursor;a->id=31;reject_allocate(a);a->id=1;a->exp0=(void*)(uintptr_t)1;reject_allocate(a);a->exp0=NULL;
 RxEnemy01Work *overlap=(void*)rx_enemy_arena_preview(sizeof(RxEnemy01Work));memset(overlap,0,sizeof(*overlap));check(!rx_enemy01_work_bind(a,overlap,sizeof(*overlap)),"overlap owner fixture");ene[127].id=1;ene[127].exp0=NULL;reject_allocate(ene+127);rx_enemy01_work_reset();memcpy(ene,pool,sizeof(pool));sys->memp=game_cursor;
 puts("PASS 126 original arena-backed native work allocations, exact fresh zeroing, existing-work preservation/reset, occupied/invalid/exhausted/overlapping allocation rejection and room-reset arena retention");
}
static BH_PWORK *parent=ene+126;static unsigned char *parent_work;
static void setup(unsigned free_slot){
 rx_enemy01_links_reset();memcpy(ene,pool,sizeof(pool));for(unsigned i=0;i<128;++i)ene[i].flg=1;
 *parent=pool[0];parent->id=1;parent->flg=1;parent->mskp=NULL;parent->exp0=parent_work;parent->exp1=NULL;parent->mlwP=parent->mdl;parent->mtx=(void*)parent->mtxbuf;
 memset(ene+free_slot,0xa5,sizeof(*ene));ene[free_slot].flg=0;ene[free_slot].exp0=NULL;sys->ewk_n=128;memcpy(rom->grand,ground,sizeof(ground));
 const char *e=rx_enemy01_model_bind(parent);check(!e,e);check(!rx_enemy01_model_bind(parent),"idempotent parent model registration");
}
static void rejected_link(int joint,short id){BH_PWORK before[128];memcpy(before,ene,sizeof(before));int extent=sys->ewk_n;unsigned char *cursor=sys->memp;check(rx_enemy01_link_preflight(parent,joint,id)!=NULL&&!bhEne01_SetLinkEnemy(parent,joint,id),"invalid original link spawn admitted");check(!memcmp(before,ene,sizeof(before))&&extent==sys->ewk_n&&cursor==sys->memp,"invalid original link spawn changed pool/extent/cursor");}
static void link_cases(void){
 memcpy(ene,pool,sizeof(pool));parent->id=1;parent->exp0=NULL;parent_work=rx_enemy01_work_allocate(parent);check(parent_work!=NULL,"parent native work");unsigned slots[]={2,7,63,127},cases=0;short ids[]={1,31,32,33,34,35,36,37,38};
 for(unsigned free=0;free<4;++free)for(unsigned id=0;id<9;++id)for(unsigned joint=0;joint<pool[0].mdl[0].obj_num;++joint){setup(slots[free]);BH_PWORK before[128];memcpy(before,ene,sizeof(before));BH_PWORK *child=bhEne01_SetLinkEnemy(parent,joint,ids[id]);check(child==ene+slots[free],"original link did not choose first free pool slot");check(!rx_enemy01_link_check(child),"original link model/parent identity registration");
  BH_PWORK expected={0};expected.flg=0x81;expected.id=ids[id];expected.mdflg=0x21;expected.idx_ct=slots[free];expected.sx=expected.sy=expected.sz=expected.sxb=expected.syb=expected.szb=1;expected.mtx=(void*)child->mtxbuf;expected.mtn_add=65536;expected.clp_jno[0]=1;expected.clp_jno[1]=-1;expected.lkwkp=(void*)parent;expected.lkono=joint;expected.mlwP=parent->mdl;expected.mnwP=parent->mnwP;int floor=2;for(unsigned n=0;n<31;++n)if((ground[n]!=0||n==2)&&ground[n]<=0)floor=n;expected.flr_no=floor-2;
  check(!memcmp(&expected,child,sizeof(expected)),"original bhSetEnemy/SetLinkEnemy fields differ from spawn reference");check(sys->ewk_n==128,"original enemy extent update");for(unsigned i=0;i<128;++i)if(i!=slots[free])check(!memcmp(before+i,ene+i,sizeof(*ene)),"link spawn changed another actor");++cases;
 }
 printf("PASS %u original SetLinkEnemy/bhSetEnemy constructions: all nine linked IDs, every parent joint, sparse first-free slots, exact native aliases and source floor/scale/motion/default fields\n",cases);
 for(unsigned bad=0;bad<20;++bad){setup(2);int joint=0;short id=31;EGG_WORK template=zob_lkmtab;switch(bad){case 0:rx_enemy01_links_reset();break;case 1:parent->exp0=(void*)(uintptr_t)1;break;case 2:parent->flg|=0x80;break;case 3:parent->flg=0;break;case 4:parent->mlwP=(void*)(uintptr_t)1;break;case 5:parent->mdl[0].objP=(void*)(uintptr_t)1;break;case 6:parent->mdl[0].obj_num=65;break;case 7:parent->mnwP=(void*)(uintptr_t)1;break;case 8:joint=-1;break;case 9:joint=parent->mdl[0].obj_num;break;case 10:id=0;break;case 11:id=30;break;case 12:id=39;break;case 13:zob_lkmtab.flg^=1;break;case 14:rom->grand[4]=NAN;break;case 15:sys->ewk_n=-1;break;case 16:sys->ewk_n=129;break;case 17:ene[2].flg=1;break;case 18:ene[2].exp0=(void*)(uintptr_t)1;break;case 19:check(bhEne01_SetLinkEnemy(parent,0,31)==ene+2,"stale link fixture");ene[2].flg=0;break;}rejected_link(joint,id);zob_lkmtab=template;}
 setup(2);BH_PWORK *child=bhEne01_SetLinkEnemy(parent,0,31);BH_PWORK save=*child;for(unsigned bad=0;bad<6;++bad){*child=save;switch(bad){case 0:child->flg&=~1u;break;case 1:child->id=32;break;case 2:child->lkwkp=(void*)(uintptr_t)1;break;case 3:child->lkono=1;break;case 4:child->mlwP=(void*)(uintptr_t)1;break;case 5:child->mnwP=(void*)(uintptr_t)1;break;}BH_PWORK before=*child;check(rx_enemy01_link_check(child)!=NULL&&!memcmp(&before,child,sizeof(before)),"altered linked ownership admitted or mutated");}*child=save;
 check(rx_enemy01_link_check((void*)(uintptr_t)1)!=NULL,"foreign child pointer admitted");

 setup(2);for(unsigned i=2;i<12;++i){ene[i].flg=0;ene[i].exp0=NULL;}
 parent->exp1=(void*)bhEne01_SetLinkEnemy(parent,1,1);check(parent->exp1==(void*)(ene+2),"original secondary body link");
 const short body_ids[]={32,32,33,33,31,34,35,36,37};const int body_joints[]={0,0,0,0,0,11,9,9,11};
 BH_PWORK *children[9];for(unsigned i=0;i<9;++i){children[i]=bhEne01_SetLinkEnemy(parent,body_joints[i],body_ids[i]);check(children[i]==ene+3+i&&!rx_enemy01_link_check(children[i]),"combined original body-part constructor chain");EN01_CHILD(parent,i*4)=children[i];}
 int shadow=0x76543210;memcpy(parent->exp0+0x24,&shadow,4);BH_PWORK body=*parent;RxEnemy01Work snapshot;memcpy(&snapshot,rx_enemy01_work_get(parent),sizeof(snapshot));unsigned char *cursor=sys->memp;check(!rx_enemy01_work_allocate(parent)&&sys->memp==cursor&&!memcmp(&snapshot,rx_enemy01_work_get(parent),sizeof(snapshot)),"combined existing allocation destroyed child pointers");
 check(!rx_enemy01_work_reinitialize(parent)&&!memcmp(&body,parent,sizeof(body)),"combined source work reset changed parent/secondary body");for(unsigned i=0;i<9;++i)check(EN01_CHILD(parent,i*4)==(i<7?children[i]:NULL)&&!rx_enemy01_link_check(children[i]),"combined seven-child preservation/link lifetime");
 puts("PASS combined original secondary-body/nine-part construction with native allocation, typed pointer publication and existing-work reset; fixture does not execute hostile Init/MainLoop");
 puts("PASS twenty malformed/full/stale link spawns reject atomically and six altered child ownership contexts reject; no linked controller or hostile AI enabled");
}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);memcpy(pool,ene,sizeof(pool));memcpy(ground,rom->grand,sizeof(ground));game_cursor=sys->memp;game_origin=sys->mempb;game_end=sys->endp;check(rx_enemy_arena_preview(16)!=NULL,"opening did not register its native arena");allocator_cases(&o);typed_cases();link_cases();
 check(!o.events_ready,"full runtime unexpectedly enabled");rx_opening_end(&o);check(!rx_enemy_arena_preview(16)&&rx_enemy01_link_check(ene+2)!=NULL,"arena/model/link views survived game teardown");puts("PASS native arena registration/release and linked ownership room lifetime; complete original enemy Init/MainLoop remain gated");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
