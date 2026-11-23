#include "opening.h"
#include "boot_disc.h"
#include "enemy01_work.h"
#include "en01.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL enemy01 work: %s\n",why);exit(1);}}
static unsigned random_word(unsigned *state){unsigned v=*state;v^=v<<13;v^=v>>17;v^=v<<5;return *state=v;}
static void rejected_reset(BH_PWORK *actor,RxEnemy01Work *work){BH_PWORK before=*actor;RxEnemy01Work copy;memcpy(&copy,work,sizeof(copy));check(rx_enemy01_work_reinitialize(actor)!=NULL,"invalid reset admitted");check(!memcmp(&before,actor,sizeof(before))&&!memcmp(&copy,work,sizeof(copy)),"invalid reset changed pointer/scalar/actor prefix");}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&opening,0);check(!e,e);
 BH_PWORK *actors[]={ene+126,ene+127};BH_PWORK saved[]={*actors[0],*actors[1]};
 RxEnemy01Work *work=calloc(3,sizeof(*work));ATR_WORK *attributes=calloc(2,sizeof(*attributes));check(work&&attributes,"native work allocation");
 check((uintptr_t)work>UINT32_MAX&&(uintptr_t)attributes>UINT32_MAX&&(uintptr_t)ene>UINT32_MAX,"native pointers must exercise addresses above 4 GiB");
 for(unsigned i=0;i<2;++i){actors[i]->id=1;e=rx_enemy01_work_bind(actors[i],work+i,sizeof(*work));check(!e,e);check(!rx_enemy01_work_bind(actors[i],work+i,sizeof(*work)),"idempotent binding");}
 unsigned cases=0,pointers=0;
 for(unsigned actor=0;actor<2;++actor)for(unsigned seed=0;seed<8192;++seed){BH_PWORK *a=actors[actor];RxEnemy01Work *w=work+actor;memset(w,0,sizeof(*w));unsigned state=seed+1;
  for(unsigned i=0x24;i<RX_ENEMY01_SCALAR_BYTES;++i)if(i<0x70||i>=0x78)w->scalar[i]=(unsigned char)random_word(&state);
  unsigned char scalar[RX_ENEMY01_SCALAR_BYTES];memcpy(scalar,w->scalar,sizeof(scalar));
  for(unsigned i=0;i<9;++i){BH_PWORK *child=seed&(1u<<i)?NULL:ene+(random_word(&state)%128);EN01_CHILD(a,i*4)=child;check(EN01_CHILD(a,i*4)==child,"wide child pointer round trip");++pointers;}
  for(unsigned i=0;i<2;++i){EN01_ATTRIBUTE(a,0x70+i*4)=attributes+i;check(EN01_ATTRIBUTE(a,0x70+i*4)==attributes+i,"wide collision pointer round trip");++pointers;}
  check(!memcmp(scalar,w->scalar,sizeof(scalar)),"typed pointer writes overlap original scalar cells");
  RxEnemy01Work expected,other;memcpy(&expected,w,sizeof(expected));memcpy(&other,work+(1-actor),sizeof(other));memset(expected.scalar,0,sizeof(expected.scalar));memcpy(expected.scalar+0x24,scalar+0x24,4);expected.children[7]=expected.children[8]=NULL;expected.attributes[0]=expected.attributes[1]=NULL;BH_PWORK before=*a;
  e=rx_enemy01_work_reinitialize(a);check(!e,e);check(!memcmp(&expected,w,sizeof(expected)),"original seven-child/shadow preservation or scalar/tail clearing differs");check(!memcmp(&before,a,sizeof(before)),"work reset changed actor state");check(!memcmp(&other,work+(1-actor),sizeof(other)),"work reset changed another actor context");++cases;
 }
 printf("PASS %u original work resets and %u pointer round trips above 4 GiB: nine child/two collision slots never overlap 176 scalar bytes; seven children and exact shadow bytes survive while other fields clear\n",cases,pointers);
 unsigned rejected=0;for(unsigned actor=0;actor<2;++actor){BH_PWORK *a=actors[actor];RxEnemy01Work *w=work+actor;memset(w,0,sizeof(*w));
  for(unsigned offset=0;offset<512;++offset){if(offset%4||offset>0x20){check(!rx_enemy01_child_slot(a,offset),"invalid child slot admitted");++rejected;}if(offset!=0x70&&offset!=0x74){check(!rx_enemy01_attribute_slot(a,offset),"invalid collision slot admitted");++rejected;}}
  check(!rx_enemy01_child_slot(a,UINT_MAX)&&!rx_enemy01_attribute_slot(a,UINT_MAX),"overflow slot admitted");
  for(unsigned i=0;i<176;++i)if(i<0x24||(i>=0x70&&i<0x78)){w->scalar[i]=1;rejected_reset(a,w);w->scalar[i]=0;}
  for(unsigned i=0;i<9;++i){w->children[i]=(void*)(uintptr_t)1;rejected_reset(a,w);w->children[i]=NULL;}
  unsigned char *old=a->exp0;a->exp0=(void*)(uintptr_t)1;rejected_reset(a,w);a->exp0=old;a->id=98;rejected_reset(a,w);a->id=1;
 }
 printf("PASS %u invalid pointer-slot selections, all 44 forbidden packed pointer bytes, nine foreign child references and mismatched actor/work reject without partial reset\n",rejected);
 rx_enemy01_work_reset();for(unsigned i=0;i<2;++i)check(!actors[i]->exp0&&!rx_enemy01_work_get(actors[i]),"work retirement did not detach actor");memset(work,0,3*sizeof(*work));check(!rx_enemy01_work_bind(actors[0],work,sizeof(*work)),"primary owner binding");
 BH_PWORK foreign={.id=1};BH_PWORK before=*actors[1];
 check(rx_enemy01_work_bind(&foreign,work+1,sizeof(*work))!=NULL&&!foreign.exp0,"non-pool owner admitted");
 check(rx_enemy01_work_bind(actors[1],work,sizeof(*work))!=NULL,"shared work admitted");check(rx_enemy01_work_bind(actors[1],(void*)((unsigned char*)work+16),sizeof(*work))!=NULL,"overlapping work admitted");check(rx_enemy01_work_bind(actors[1],(void*)(UINTPTR_MAX-15),sizeof(*work))!=NULL,"overflowing storage admitted");
 check(rx_enemy01_work_bind(actors[1],work+1,sizeof(*work)-1)!=NULL,"short work admitted");check(rx_enemy01_work_bind(actors[1],(void*)((unsigned char*)(work+1)+1),sizeof(*work))!=NULL,"misaligned work admitted");check(rx_enemy01_work_bind(actors[1],NULL,sizeof(*work))!=NULL,"null work admitted");
 work[1].scalar[0]=1;check(rx_enemy01_work_bind(actors[1],work+1,sizeof(*work))!=NULL,"packed pointer bytes bound");work[1].scalar[0]=0;work[1].children[0]=(void*)(uintptr_t)1;check(rx_enemy01_work_bind(actors[1],work+1,sizeof(*work))!=NULL,"foreign child bound");work[1].children[0]=NULL;
 check(!memcmp(&before,actors[1],sizeof(before))&&!rx_enemy01_work_get(actors[1]),"rejected bind occupied or changed another actor");
 check(!rx_enemy01_work_bind(actors[1],work+1,sizeof(*work)),"adjacent native work wrongly rejected");check(rx_enemy01_work_bind(actors[1],work+2,sizeof(*work))!=NULL&&rx_enemy01_work_get(actors[1])==work+1,"occupied context replaced");
 check(!rx_enemy01_work_get((void*)(uintptr_t)1)&&rx_enemy01_work_reinitialize((void*)(uintptr_t)1)!=NULL,"foreign actor dereferenced");
 actors[0]->exp0=(void*)(uintptr_t)1;rx_enemy01_work_reset();check(actors[0]->exp0==(void*)(uintptr_t)1&&!actors[1]->exp0,"retirement overwrote a foreign replacement");actors[0]->exp0=NULL;
 check(!rx_enemy01_work_bind(actors[0],work,sizeof(*work)),"lifetime rebind");check(!opening.events_ready,"hostile work enabled full opening");rx_opening_end(&opening);actors[0]->id=1;actors[0]->exp0=work->scalar;check(!rx_enemy01_work_get(actors[0])&&rx_enemy01_work_reinitialize(actors[0])!=NULL,"room teardown retained zombie work binding");
 puts("PASS independent/adjacent owners, atomic bad binding, overlap/overflow guards, foreign replacement preservation and room teardown; hostile Init/MainLoop still gated");
 *actors[0]=saved[0];*actors[1]=saved[1];free(attributes);free(work);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
