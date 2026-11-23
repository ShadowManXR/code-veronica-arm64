#include "face_control_runtime.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "event_audio.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int EventVibrationMode;
extern unsigned Event_T_timer;
extern BH_SCEWORK bhEtask[16];
typedef struct {uint64_t head;int32_t words[RX_FACE_CONTROL_WORDS];uint64_t tail;} Control;
static Control controls[128];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL face controls: %s\n",why);exit(1);}}
static void reject(const unsigned char *code,size_t bytes)
{
    unsigned char data[32]={8,0,0,0};data[4]=(unsigned char)(8+bytes);memcpy(data+8,code,bytes);
    RxRoomScripts scripts;check(!rx_room_scripts_open(data,bytes+10,&scripts),"invalid rejection fixture");
    void *old=rom->evtp;rom->evtp=&scripts;Event_T_timer=123;bhEtask[15].status=1;
    int before[128];for(unsigned i=0;i<128;++i)before[i]=controls[i].words[0];
    check(rx_events_start_room()!=NULL&&Event_T_timer==123&&bhEtask[15].status==1,"bad facial command modified scheduler");
    for(unsigned i=0;i<128;++i)check(before[i]==controls[i].words[0],"bad facial command wrote control storage");
    rx_events_reset();rom->evtp=old;
}
void rx_check_face_controls(void (*execute)(const unsigned char *,size_t),const unsigned char *retail_flag,const unsigned char *retail_face)
{
    check(retail_flag[0]==0x9e&&retail_flag[1]==0&&retail_face[0]==0x9a&&retail_face[1]==1&&retail_face[2]==0,"retail face/policy command differs");
    int old_vibration=EventVibrationMode,queue=rx_sound_queue_peek();
    for(unsigned value=0;value<256;++value) {
        EventVibrationMode=-17;unsigned char code[]={0x9e,(unsigned char)value,0,0};if(!value)memcpy(code,retail_flag,2);
        execute(code,sizeof(code));check(EventVibrationMode==!value&&queue==rx_sound_queue_peek(),"original vibration policy inversion emitted a physical sound/haptic request");
    }
    static BH_PWORK previous[128];memcpy(previous,ene,sizeof(previous));
    ETTY_WORK *old_records=rom->enep;int old_count=rom->ene_n;
    static ETTY_WORK records[128];memset(records,0,sizeof(records));rx_face_control_reset();memset(ene,0,sizeof(previous));
    rom->enep=records;rom->ene_n=128;
    for(unsigned i=0;i<128;++i) {
        records[i].flg=1;records[i].id=(unsigned short)(91+i%10);records[i].wrk_no=(unsigned char)(127-i);
        BH_PWORK *actor=&ene[127-i];actor->id=records[i].id;
        memset(&controls[i],0xa5,sizeof(controls[i]));
        check((uintptr_t)controls[i].words>UINT32_MAX&&!rx_face_control_bind(actor,controls[i].words,RX_FACE_CONTROL_BYTES)&&
              !rx_face_control_check(actor),"owned high-address facial control bind failed");
    }
    const unsigned opcodes[]={0x9a,0x8f,0x90,0x3e,0x3f},masks[]={2,16,32,1,4};
    const uint32_t seeds[]={0,0xffffffffu,0x80000000u,0x55aa55aau};
    for(unsigned op=0;op<5;++op)for(unsigned index=0;index<128;++index)for(unsigned action=0;action<(op==2?1u:256u);++action) {
        memset(&controls[index],0xa5,sizeof(Control));uint32_t seed=seeds[(action+index)%4];memcpy(controls[index].words,&seed,4);
        Control expected=controls[index];uint32_t wanted=op==2||!action?seed|masks[op]:seed&~masks[op];memcpy(expected.words,&wanted,4);
        int neighbors[128];for(unsigned i=0;i<128;++i)neighbors[i]=controls[i].words[0];
        unsigned char code[12]={0};size_t bytes=op==2?2:4;
        code[0]=(unsigned char)opcodes[op];code[1]=(unsigned char)index;
        if(op>=3){code[1]=(unsigned char)(action^0x55);code[2]=(unsigned char)index;code[3]=(unsigned char)action;}
        else if(op!=2){code[2]=(unsigned char)action;code[3]=0xff;}
        if(op==0&&index==1&&!action)memcpy(code,retail_face,4);
        const unsigned char marker[]={5,1,30,0,0,0,0,0};memcpy(code+bytes,marker,sizeof(marker));execute(code,bytes+sizeof(marker));
        check((sys->ev_flg[0]&2)&&!memcmp(&controls[index],&expected,sizeof(Control)),"original facial flag mask, native stride, padding or target record differs");
        for(unsigned i=0;i<128;++i)if(i!=index)check(neighbors[i]==controls[i].words[0],"facial command selected wrong record-to-work mapping");
    }
    const unsigned char valid[]={0x9a,0,0,0};BH_PWORK *actor=&ene[127];
    unsigned char *owned=actor->exp0;actor->exp0=(unsigned char *)(uintptr_t)0x100000004ull;reject(valid,4);actor->exp0=owned;
    actor->id++;records[0].id++;check(rx_face_control_check(actor)!=NULL,"stale facial actor ID remained registered");reject(valid,4);actor->id--;records[0].id--;
    records[0].flg=0;reject(valid,4);records[0].flg=1;
    records[0].id++;reject(valid,4);records[0].id--;
    records[0].wrk_no=128;reject(valid,4);records[0].wrk_no=127;
    rom->enep=NULL;reject(valid,4);rom->enep=records;
    rom->ene_n=-1;reject(valid,4);rom->ene_n=0;reject(valid,4);rom->ene_n=129;reject(valid,4);rom->ene_n=128;
    for(unsigned i=128;i<256;++i)reject((const unsigned char[]){0x9a,(unsigned char)i,0,0},4);
    for(unsigned op=0;op<5;++op)for(unsigned bytes=1;bytes<(op==2?2u:4u);++bytes) {
        unsigned char code[]={opcodes[op],0,0,0};reject(code,bytes);
    }
    reject((const unsigned char[]){0x9e},1);
    check(rx_face_control_bind(actor,controls[0].words,RX_FACE_CONTROL_BYTES-1)!=NULL&&
          rx_face_control_bind(actor,(int32_t *)((unsigned char *)controls[0].words+1),RX_FACE_CONTROL_BYTES)!=NULL&&
          rx_face_control_bind((BH_PWORK *)(uintptr_t)1,controls[0].words,RX_FACE_CONTROL_BYTES)!=NULL,"bad facial registration accepted");
    unsigned char *foreign=(unsigned char *)(uintptr_t)1;actor->exp0=foreign;rx_face_control_reset();
    check(actor->exp0==foreign&&!ene[0].exp0&&rx_face_control_check(actor)!=NULL,"facial reset retained registration or erased unrelated actor work");
    actor->exp0=NULL;reject(valid,4);
    memcpy(ene,previous,sizeof(previous));rom->enep=old_records;rom->ene_n=old_count;EventVibrationMode=old_vibration;
    puts("PASS original face/policy flags: 128 remapped actors/all action bytes, owned high-address scalar work/canaries, original room bytes, stale/foreign references, malformed input and teardown");
}
