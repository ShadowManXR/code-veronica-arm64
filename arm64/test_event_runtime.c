#include "event_runtime.h"
#include "room_scripts.h"
#include "room_header.h"
#include "event.h"
#include "main.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaGraphics3D.h"
#include "ps2_NaMath.h"
#include "ps2/vu0.h"
#include "random_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

/* The event harness initializes the real VU0 matrix state without a profiler
   or a VU1 graphics consumer. Unexpected VU1 work is a test failure. */
int prof_enabled;
void prof_push(int category) { (void)category; }
void prof_pop(void) {}
void vu1_xgkick(const VuVec *memory,uint32_t quadword,uint32_t mask)
{ (void)memory;(void)quadword;(void)mask;abort(); }

static SYS_WORK system_work;
static ROM_WORK room_work;
static BH_PWORK player_work;
SYS_WORK *sys = &system_work;
ROM_WORK *rom = &room_work;
BH_PWORK *plp = &player_work;
BH_PWORK ene[128];
O_WRK eff[512];
unsigned char pl_sleep_cnt;
CAM_WORK cam;
NJS_MATRIX *cmat;
extern BH_SCEWORK bhEtask[16];
extern unsigned Event_T_timer;
extern unsigned char *bhScePtr;
extern BH_SCEWORK *bhCetask;

typedef struct { const unsigned char *data; size_t bytes; } Code;
static unsigned char *storage;
static RxRoomScripts scripts;
static const unsigned char end[] = {0,0};

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL events: %s\n", message);
        exit(1);
    }
}

static void put(unsigned char *p, size_t value)
{
    for (unsigned i=0; i<4; ++i) p[i]=(unsigned char)(value>>(i*8));
}

static void bind(const Code *code, size_t count)
{
    rx_events_reset();
    free(storage);
    size_t bytes=count*4;
    for (size_t i=0; i<count; ++i) bytes+=code[i].bytes;
    storage=calloc(1,bytes);
    check(storage && (uintptr_t)storage>UINT32_MAX, "bytecode must use high native pointers");
    size_t offset=count*4;
    for (size_t i=0; i<count; ++i) {
        put(storage+i*4,offset);
        memcpy(storage+offset,code[i].data,code[i].bytes);
        offset+=code[i].bytes;
    }
    check(!rx_room_scripts_open(storage,bytes,&scripts), "could not bind bytecode");
    rom->evtp=&scripts;
    memset(sys->ev_flg,0,sizeof(sys->ev_flg));
}

static int flag(unsigned bit) { return !!(sys->ev_flg[bit/32] & (0x80000000u>>(bit%32))); }
static void tick(void)
{
    const char *error=rx_events_tick();
    check(!error,error);
}

static void check_sleep(void)
{
    const unsigned char entry[]={5,1,0,0,0,0, 20,0,0,0, 20,0,1,1, 0,0};
    const unsigned char frame[]={5,1,7,0,0,0, 0,0};
    const unsigned char slow[]={0xf8,0xf9,3,0, 5,1,1,0,0,0, 0xff,0};
    const unsigned char fast[]={0xf8,0xf9,1,0, 5,1,2,0,0,0, 0xff,0};
    const Code code[]={{entry,sizeof(entry)},{frame,sizeof(frame)},{slow,sizeof(slow)},{fast,sizeof(fast)}};
    bind(code,4);
    check(!rx_events_start_room(),"could not start timed scenario");
    check(flag(0)&&!flag(1)&&!flag(2)&&Event_T_timer==1,"initial task timing differs");
    check(bhEtask[0].loop==0 && bhEtask[0].cnt[0]==2 &&
          (uintptr_t)bhEtask[0].data>UINT32_MAX,"Sleep continuation was truncated");
    tick();
    check(flag(7)&&!flag(1)&&flag(2)&&!bhEtask[1].status&&Event_T_timer==2,
          "parallel one-frame sleep or frame scenario differs");
    tick();
    check(!flag(1)&&bhEtask[0].status&&Event_T_timer==3,"Sleep resumed one frame early");
    tick();
    check(flag(1)&&!bhEtask[0].status&&Event_T_timer==4,"Sleep did not resume at the original frame");
    tick();
    check(Event_T_timer==4,"idle scheduler advanced the event timer");
    puts("PASS original concurrent Sleep/Sleeping tasks, frame scenario and shared timer");
}

static void check_loops(void)
{
    const unsigned char entry[]={20,0,0,0, 0,0};
    const unsigned char loop[]={0xfa,0,3,0, 5,1,3,0,0,2, 0xfe, 0xfb,0, 0xff,0};
    const Code code[]={{entry,sizeof(entry)},{end,sizeof(end)},{loop,sizeof(loop)}};
    bind(code,3);
    check(!rx_events_start_room(),"could not start For loop");
    check(flag(3)&&bhEtask[0].cnt[0]==3,"first For iteration differs");
    check((uintptr_t)bhEtask[0].lstack[0]>UINT32_MAX,"For continuation was truncated");
    tick(); check(!flag(3)&&bhEtask[0].cnt[0]==2,"second For iteration differs");
    tick(); check(flag(3)&&bhEtask[0].cnt[0]==1,"third For iteration differs");
    tick(); check(!bhEtask[0].status&&bhEtask[0].loop==-1,"For loop did not end");

    unsigned char waiting[]={0xfc,8,4,1,5,0,0,1,0xfe,0xf3,5,1,6,0,0,0,0xff,0};
    for (unsigned variant=0; variant<2; ++variant) {
        waiting[9]=variant?0xfd:0xf3;
        const Code while_code[]={{entry,sizeof(entry)},{end,sizeof(end)},{waiting,sizeof(waiting)}};
        bind(while_code,3);
        check(!rx_events_start_room(),"could not start While loop");
        tick();
        check(bhEtask[0].status&&!flag(6)&&bhEtask[0].loop==0,"While failed to wait");
        sys->ev_flg[0]|=0x80000000u>>5;
        tick();
        if (variant) {
            check(!flag(6)&&bhEtask[0].status,"Ewhile failed to yield on completion");
            tick();
        }
        check(flag(6)&&!bhEtask[0].status&&bhEtask[0].loop==-1,"While continuation differs");
    }
    puts("PASS original For/Next and both While endings with high native continuation pointers");
}

static void check_conditions(void)
{
    const unsigned char entry[]={20,0,0,0,0,0};
    const unsigned char choice[]={1,14,4,1,8,0,0,0, 5,1,9,0,0,0,
                                   2,8, 5,1,10,0,0,0, 0xff,0};
    const Code code[]={{entry,sizeof(entry)},{end,sizeof(end)},{choice,sizeof(choice)}};
    for (unsigned set=0; set<2; ++set) {
        bind(code,3);
        if (set) sys->ev_flg[0]|=0x80000000u>>8;
        check(!rx_events_start_room(),"If/Else dispatch failed");
        check(flag(9)==(int)set&&flag(10)==!(int)set&&!bhEtask[0].status,
              "original conditional selected the wrong branch");
    }
    puts("PASS original flag tests, conditional unwind and Else targets");
}

static void check_state_commands(void)
{
    const unsigned char entry[]={8,21,3,0, 6,21,0,3,
        9,5,0xd2,4, 7,0,5,0,0xd2,4,
        9,9,0x59,1, 7,0,9,0,0x59,1,
        0xf4, 0x61,0, 0x60,0,0,0,1,0, 5,1,30,0,0,0, 0,0};
    const Code state[]={{entry,sizeof(entry)},{end,sizeof(end)}};
    bind(state,2);
    check(!rx_events_start_room()&&flag(30)&&sys->rcase==3&&plp->hp==1234&&
          sys->stv_tm==345&&Event_T_timer==0,"original stores, comparisons or timer check differs");
    const unsigned char pad[]={0x5d,1,1,0,0,0, 5,1,31,0,0,0, 0,0};
    const Code input[]={{pad,sizeof(pad)},{end,sizeof(end)}};
    for (unsigned held=0; held<2; ++held) {
        bind(input,2);
        sys->pad_on=held?2:0;
        sys->pad_ps=0;
        check(!rx_events_start_room()&&flag(31)==!held,"original pad condition differs");
    }
    sys->pad_on=0;
    const unsigned char start[]={20,0,0,0,20,0,1,1,0,0};
    const unsigned char killer[]={0xbc,1,5,1,30,0,0,0,0xff,0};
    const unsigned char victim[]={5,1,31,0,0,0,0xff,0};
    const Code tasks[]={{start,sizeof(start)},{end,sizeof(end)},
                        {killer,sizeof(killer)},{victim,sizeof(victim)}};
    bind(tasks,4);
    check(!rx_events_start_room()&&flag(30)&&!flag(31)&&!bhEtask[1].status,
          "original task kill allowed a later task to execute");
    puts("PASS original state stores/comparisons, pad conditions, timer checks and task cancellation");
}

static void check_effect_commands(void)
{
    EF_WRK records[1]={{.id=9}};
    rom->effp=records;rom->eff_n=1;sys->efid[0]=23;
    eff[23]=(O_WRK){.id=9,.flg=1,.stflg=0x40};
    eff[0].stflg=0x20;
    const unsigned char hide[]={0x43,0,0,0,0x44,5,8,10,3,0,0,0};
    const Code code[]={{hide,sizeof(hide)},{end,sizeof(end)}};
    bind(code,2);
    check(!rx_events_start_room()&&eff[23].stflg==0x1000040&&eff[0].stflg==0x20&&
          rom->amb_r[3]==0.5f&&rom->amb_g[3]==0.8f&&rom->amb_b[3]==1.0f,
          "original effect display/ambient dispatch differs");
    const unsigned char show[]={0x43,0,1,0,0,0};
    const Code shown[]={{show,sizeof(show)},{end,sizeof(end)}};
    bind(shown,2);
    check(!rx_events_start_room()&&eff[23].stflg==0x40,"original effect show changed unrelated flags");
    for (unsigned variant=0;variant<3;++variant) {
        sys->efid[0]=variant==0?-1:variant==1?512:0;
        bind(shown,2);
        check(rx_events_start_room()!=NULL,"invalid or unpublished effect map was executed");
    }
    const unsigned char bad_ambient[]={0x44,5,8,10,4,0,0,0};
    const Code bad[]={{bad_ambient,sizeof(bad_ambient)},{end,sizeof(end)}};
    bind(bad,2); check(rx_events_start_room()!=NULL,"invalid ambient bank was written");
    rom->effp=NULL;rom->eff_n=0;sys->efid[0]=-1;memset(eff,0,sizeof(eff));
    puts("PASS original EffDispSet/EffAmbSet dispatch, native slot selection and unpublished-map rejection");
}

static void run_common(const unsigned char *entry,size_t bytes)
{
    const Code code[]={{entry,bytes},{end,sizeof(end)}};bind(code,2);
    const char *error=rx_events_start_room();check(!error,error);
}
typedef struct {unsigned seed,value[2],fraction[2],bucket[2];} RandomVector;
static RandomVector random_vectors[128];static unsigned random_count;
static void check_random_generator(const char *path)
{
    check(rx_ps2_rand()==1103527590,"retail default random state is not 1");
    FILE *file=fopen(path,"r");check(file!=NULL,"open private retail RNG reference");
    char tag[16];unsigned version;
    check(fscanf(file,"%15s%u%u",tag,&version,&random_count)==3&&!strcmp(tag,"RNGREF")&&version==1&&random_count<=128,
          "invalid retail RNG reference header");
    for(unsigned i=0;i<random_count;++i) {
        unsigned count;RandomVector *v=&random_vectors[i];
        check(fscanf(file,"%15s%u%u",tag,&v->seed,&count)==3&&!strcmp(tag,"SEED")&&count>=2&&count<=1024,"invalid retail RNG vector");
        rx_ps2_srand(v->seed);
        for(unsigned k=0;k<count;++k) {
            unsigned value,fraction,bucket;
            check(fscanf(file,"%u%u%u",&value,&fraction,&bucket)==3&&value<=INT_MAX&&bucket<=100,"truncated RNG reference");
            check((unsigned)rx_ps2_rand()==value,"native RNG differs from executed retail EE routine");
            if(k<2){v->value[k]=value;v->fraction[k]=fraction;v->bucket[k]=bucket;}
        }
    }
    fclose(file);rx_ps2_srand(1);
    puts("PASS native retail RNG: default state, zero/high-bit seeds and reference vectors executed from original EE instructions");
}
static void check_random_commands(const unsigned char *retail)
{
    check(retail[0]==0x63&&retail[1]==4,"original RM_0000 random selector bytes differ");
    for(unsigned i=0;i<random_count;++i) {
        const RandomVector *v=&random_vectors[i];
        for(unsigned divisor=1;divisor<256;++divisor) {
            unsigned char code[]={0x63,(unsigned char)divisor,5,1,30,0,0,0,0,0};
            if(divisor==4)memcpy(code,retail,2);
            rx_ps2_srand(v->seed);run_common(code,sizeof(code));
            check(bhEtask[0].wpnl_no==(int)(v->bucket[0]%divisor)&&flag(30)&&rx_ps2_rand()==(int)v->value[1],
                  "original RandamSet quantization/modulus/result or RNG call count differs");
        }
        for(unsigned mode=0;mode<3;++mode) {
            unsigned encoded=mode?(v->seed^0xabcdef01u):v->seed;
            unsigned char code[]={0x8c,mode==0?0:mode==1?1:255,(unsigned char)(encoded>>16),(unsigned char)(encoded>>24),
                (unsigned char)encoded,(unsigned char)(encoded>>8),0,0};
            rx_ps2_srand(mode?v->seed:71);run_common(code,sizeof(code));unsigned bits;memcpy(&bits,&bhEtask[0].addpx,4);
            check(bits==v->fraction[0]&&(unsigned)bhEtask[0].wpnl_no==bits&&rx_ps2_rand()==(int)v->value[1],
                  "original RandTest seed order, nonzero mode, float bits or RNG call count differs");
        }
        /* Original conditions read task zero's shared random register even
           when the producing command runs in task seven. */
        for(unsigned match=0;match<2;++match) {
            unsigned target=(v->bucket[0]+!match)%4;
            unsigned char entry[]={0x14,0,7,0,0,0};
            unsigned char worker[]={0x63,4,1,12,6,17,0,(unsigned char)target,5,1,9,0,0,0,3,0,0xff,0};
            const Code code[]={{entry,sizeof(entry)},{end,sizeof(end)},{worker,sizeof(worker)}};
            bind(code,3);rx_ps2_srand(v->seed);
            check(!rx_events_start_room()&&flag(9)==(int)match&&bhEtask[7].wpnl_no==0&&bhEtask[0].wpnl_no==(int)(v->bucket[0]%4),
                  "original RandamSet/CmpB shared task-zero register differs");
        }
    }
    const unsigned char entry[]={0x14,0,1,0,0x14,0,7,1,0,0};
    const unsigned char first[]={0x63,5,0xff,0},second[]={0x63,7,0xff,0};
    const Code concurrent[]={{entry,sizeof(entry)},{end,sizeof(end)},{first,sizeof(first)},{second,sizeof(second)}};
    bind(concurrent,4);rx_ps2_srand(random_vectors[1].seed);
    check(!rx_events_start_room()&&bhEtask[0].wpnl_no==(int)(random_vectors[1].bucket[1]%7)&&
          !bhEtask[1].wpnl_no&&!bhEtask[7].wpnl_no,"concurrent original random tasks used independent state or wrong ordering");
    const unsigned char invalid[]={0x63,0,0,0};const Code bad[]={{invalid,sizeof(invalid)},{end,sizeof(end)}};bind(bad,2);
    rx_ps2_srand(1);Event_T_timer=123;bhEtask[15].status=1;
    check(rx_events_start_room()!=NULL&&rx_events_fault()->opcode==0x63&&Event_T_timer==123&&bhEtask[15].status==1&&rx_ps2_rand()==1103527590,
          "zero modulus reached original division or consumed RNG/scheduler state");
    for(unsigned opcode=0;opcode<2;++opcode)for(unsigned length=1;length<(opcode?6u:2u);++length) {
        unsigned char code[]={opcode?0x8c:0x63,1,2,3,4,5};const Code short_code[]={{code,length},{end,sizeof(end)}};bind(short_code,2);
        rx_ps2_srand(1);Event_T_timer=123;
        check(rx_events_start_room()!=NULL&&Event_T_timer==123&&rx_ps2_rand()==1103527590,"truncated random command consumed state or crossed script entry");
    }
    rx_ps2_srand(1);
    puts("PASS original RandamSet/RandTest: all nonzero moduli, float-rounded 100 edge, original seed byte order, shared task-zero state/CmpB, concurrent ordering and rejected zero/truncation");
}
void rx_check_event_audio(void (*execute)(const unsigned char *,size_t),const unsigned char *retail_stop);
void rx_check_sound_requests(void (*execute)(const unsigned char *,size_t),const unsigned char *retail_stop);
void rx_check_face_controls(void (*execute)(const unsigned char *,size_t),const unsigned char *policy,const unsigned char *face);
static void check_item_motion_commands(const unsigned char *retail)
{
    static O_WRK items[32];static MN_WORK bank[512];static NJS_CNK_OBJECT nodes[2];static O_WORK work[2];
    static NJS_MATRIX stack[16];static float keys[3][3]={{1,2,3},{7,8,9},{-4,3,2}};
    static short angles[3][3]={{0}},attributes[]={0x123,0x456,0x789};
    static NJS_MDATA2_MOD tracks[2];
    O_WRK *old_items=sys->itwp;MN_WORK *old_bank=sys->rmthp;unsigned old_sp=sys->sp_flg;
    sys->itwp=items;sys->rmthp=bank;_Make_SinTable();njInitMatrix(stack,16,0);
    check((uintptr_t)items>UINT32_MAX&&(uintptr_t)bank>UINT32_MAX&&(uintptr_t)tracks>UINT32_MAX,"item motion fixture must use high native pointers");
    O_WRK *item=&items[31];
    *item=(O_WRK){.mode3=1,.mnwP=bank,.mtn_no=511,.mtn_add=65536,.px=10,.py=-2,.pz=5};
    item->mtx=(void *)item->mtxbuf;item->mlwP=&item->mdl[15];
    *item->mlwP=(ML_WORK){.obj_num=2,.objP=nodes,.owP=work};
    nodes[0].child=&nodes[1];nodes[1].pos[0]=4;nodes[1].pos[1]=5;nodes[1].pos[2]=6;
    tracks[0].p[0]=keys;tracks[0].p[1]=angles;tracks[1].p[1]=angles;
    bank[511]=(MN_WORK){.obj_num=2,.frm_num=3,.datP=keys,.md2P=tracks,.atrP=(unsigned short *)attributes};
    sys->sp_flg=4;const unsigned char code[]={0x9c,31,0,0};
    for(unsigned frame=0;frame<3;++frame) {
        item->frm_no=(int)frame*65536;run_common(code,sizeof(code));
        check(item->frm_no==(int)((frame+1)%3)*65536&&item->mtn_attr==(unsigned short)attributes[frame],"original item frame wrap/attributes differ");
        for(unsigned c=0;c<3;++c) {
            float world=keys[frame][c]+(&item->px)[c];
            check(fabsf(work[0].mtx[12+c]-world)<0.0001f&&fabsf(work[1].mtx[12+c]-(world+nodes[1].pos[c]))<0.0001f,
                  "original item root/child matrices differ from independent translation reference");
        }
    }
    item->frm_no=0;item->mtn_add=(unsigned)-65536;run_common(code,sizeof(code));check(item->frm_no==2*65536,"original reverse item wrap differs");
    item->mtn_add=0;run_common(code,sizeof(code));check(item->frm_no==2*65536,"original stationary motion advanced frame");
    /* Every non-running mode and disabled item task must leave all work intact,
       even when the motion/model pointers are absent. */
    O_WRK valid=*item;
    for(unsigned mode=0;mode<256;++mode) for(unsigned enabled=0;enabled<2;++enabled) {
        if(mode==1&&enabled)continue;
        *item=valid;item->mode3=(unsigned char)mode;item->mnwP=NULL;item->mlwP=NULL;sys->sp_flg=enabled?4:0;
        O_WRK before=*item;run_common(code,sizeof(code));check(!memcmp(&before,item,sizeof(before)),"paused/disabled original item motion changed work");
    }
    *item=valid;sys->sp_flg=4;
    for(unsigned variant=0;variant<19;++variant) {
        *item=valid;bank[511].datP=keys;bank[511].flg=0;work[0].flg=0;
        switch(variant) {
        case 0:sys->itwp=NULL;break;case 1:item->mnwP=NULL;break;case 2:item->mtn_no=512;break;
        case 3:item->mlwP=NULL;break;case 4:item->mtx=NULL;break;case 5:item->mtn_md=2;break;
        case 6:item->hokan_count=1;break;case 7:bank[511].flg=0x10;break;case 8:item->flg|=0x80;break;
        case 9:bank[511].datP=NULL;break;case 10:item->frm_no=-1;break;case 11:item->frm_no=3*65536;break;
        case 12:item->mtn_add=4*65536;break;case 13:work[0].flg=4;break;
        case 14:item->mlwP->obj_num=65;break;case 15:item->mlwP->owP=NULL;break;
        case 16:bank[511].frm_num=32768;break;case 17:bank[511].md2P=NULL;break;case 18:bank[511].obj_num=3;break;
        }
        const Code bad[]={{code,sizeof(code)},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
        O_WRK before=*item;
        check(rx_events_start_room()!=NULL&&rx_events_fault()->opcode==0x9c&&Event_T_timer==123&&!memcmp(&before,item,sizeof(before)),
              "invalid native item motion reached evaluator or changed scheduler/work");
        sys->itwp=items;valid.mdl[15].obj_num=2;valid.mdl[15].owP=work;
        bank[511].frm_num=3;bank[511].md2P=tracks;bank[511].obj_num=2;
    }
    *item=valid;bank[511].datP=keys;bank[511].flg=0;work[0].flg=0;
    check(retail[0]==0x9c&&retail[1]==1&&retail[2]==0x9c&&retail[3]==2,"original item motion command bytes differ");
    for(unsigned i=1;i<=2;++i) {
        items[i]=valid;items[i].mlwP=&items[i].mdl[15];items[i].mtx=(void *)items[i].mtxbuf;
        items[i].frm_no=(int)(i-1)*65536;items[i].mtn_add=65536;
    }
    unsigned char retail_code[6]={0};memcpy(retail_code,retail,4);O_WRK untouched=*item;run_common(retail_code,sizeof(retail_code));
    check(items[1].frm_no==65536&&items[2].frm_no==2*65536&&items[1].mtn_attr==0x123&&items[2].mtn_attr==0x456&&
          !memcmp(item,&untouched,sizeof(untouched)),"original RM_0000 item motion commands lost independent item state");
    const unsigned char index_bad[]={0x9c,32,0,0},truncated[]={0x9c};
    const Code bad_index[]={{index_bad,sizeof(index_bad)},{end,sizeof(end)}};bind(bad_index,2);check(rx_events_start_room()!=NULL,"out-of-range item index accepted");
    const Code short_code[]={{truncated,sizeof(truncated)},{end,sizeof(end)}};bind(short_code,2);check(rx_events_start_room()!=NULL,"truncated item motion accepted");
    sys->itwp=old_items;sys->rmthp=old_bank;sys->sp_flg=old_sp;
    puts("PASS original SetItmMotion fast tracks, root/child world matrices, attributes, forward/reverse wrap, all pause modes and guarded native references/formats");
}
static void check_equipment_menu_conditions(void)
{
    int old_weapon=plp->wpnr_no;unsigned old_status=sys->st_flg;
    unsigned char code[]={1,10,0x26,0,5,1,9,0,0,0,2,8,5,1,10,0,0,0,0,0};
    /* Compare full signed weapon storage to the unsigned script byte. */
    const int weapons[]={INT_MIN,-32768,-1,0,1,2,3,127,128,254,255,256,32767,INT_MAX};
    for(unsigned operand=0;operand<256;++operand) for(unsigned w=0;w<sizeof(weapons)/sizeof(*weapons);++w) {
        plp->wpnr_no=weapons[w];code[3]=(unsigned char)operand;sys->st_flg=0xa5a5a5a5;
        unsigned inventory[384];memcpy(inventory,sys->itm,sizeof(inventory));
        run_common(code,sizeof(code));
        check(flag(9)==(weapons[w]==(int)operand)&&flag(10)==(weapons[w]!=(int)operand),"original equipped-weapon conditional differs");
        check(plp->wpnr_no==weapons[w]&&sys->st_flg==0xa5a5a5a5&&!memcmp(inventory,sys->itm,sizeof(inventory)),
              "equipment condition modified player, inventory or menu state");
    }
    code[2]=0x81;
    const unsigned unrelated[]={0,0xffffffffu&~0x40008u,0x80000001u,0x00401010u};
    for(unsigned operand=0;operand<256;++operand) for(unsigned busy=0;busy<4;++busy)
        for(unsigned other=0;other<sizeof(unrelated)/sizeof(*unrelated);++other) {
            unsigned status=unrelated[other]|(busy&1?8u:0)|(busy&2?0x40000u:0);
            sys->st_flg=status;code[3]=(unsigned char)operand;
            run_common(code,sizeof(code));int expected=busy?operand==0:operand!=0;
            check(flag(9)==expected&&flag(10)==!expected&&sys->st_flg==status,
                  "original map/menu busy flag condition or nonzero operand semantics differ");
        }
    plp->wpnr_no=old_weapon;sys->st_flg=old_status;
    for(unsigned op=0;op<2;++op) {
        unsigned char truncated[]={op?0x81:0x26};
        const Code bad[]={{truncated,sizeof(truncated)},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
        check(rx_events_start_room()!=NULL&&Event_T_timer==123,"truncated equipment/menu condition changed scheduler");
    }
    code[2]=0x26;const Code valid[]={{code,sizeof(code)},{end,sizeof(end)}};bind(valid,2);
    BH_PWORK *old_player=plp;plp=NULL;Event_T_timer=123;
    check(rx_events_start_room()!=NULL&&rx_events_fault()->opcode==0x26&&Event_T_timer==123,
          "absent native player reached original equipment check");plp=old_player;
    puts("PASS original ArmsItemCheck/SubMapBusyCk: all operand bytes, signed weapon values, both menu bits, unrelated state, branches and invalid/truncated input");
}
static void check_cinematic_commands(const unsigned char *retail)
{
    unsigned old_cb=sys->cb_flg,old_status=sys->st_flg,old_sp=sys->sp_flg,old_game=sys->gm_flg;
    float old_ap=sys->cine_ap,old_an=sys->cine_an;
    check(retail[0]==0x12&&retail[1]==0,"retail cinematic command differs");
    const unsigned seeds[]={0,0xffffffffu,0x12345678u,0x40000004u};
    for(unsigned mode=0;mode<256;++mode)for(unsigned seed=0;seed<4;++seed)for(unsigned old_cinema=0;old_cinema<2;++old_cinema) {
        unsigned cb=(seeds[seed]&~0x40u)|(old_cinema?0x40u:0),status=seeds[seed],sp=seeds[seed]|0x10;
        float ap=-17.25f,an=93.5f;sys->cb_flg=cb;sys->st_flg=status;sys->sp_flg=sp;sys->cine_ap=ap;sys->cine_an=an;
        switch(mode) {
        case 0:cb=(cb&~0x40u)|4;status|=4;break;
        case 1:if(old_cinema)ap=an=0;cb&=~4u;status&=~4u;sp=~0u;break;
        case 2:cb&=~4u;status&=~4u;ap=an=0;break;
        case 3:cb|=0x44;status|=4;break;
        case 4:cb&=~0x44u;status&=~4u;sp=~0u;ap=an=0;break;
        case 5:cb=(cb&~0x40u)|4;status|=4;ap=an=0;break;
        }
        unsigned char code[]={0x12,(unsigned char)mode,5,1,30,0,0,0,0,0};if(!mode)memcpy(code,retail,2);
        run_common(code,sizeof(code));
        check(flag(30)&&sys->cb_flg==cb&&sys->st_flg==status&&sys->sp_flg==sp&&sys->cine_ap==ap&&sys->cine_an==an,
              "original cinematic mode, task flags, offset retention/reset or original default no-op differs");
    }
    for(unsigned value=0;value<256;++value)for(unsigned seed=0;seed<4;++seed) {
        sys->gm_flg=seeds[seed];unsigned char code[]={0x6a,(unsigned char)value,0,0};run_common(code,sizeof(code));
        check(sys->gm_flg==(value?seeds[seed]&~0x40000000u:seeds[seed]|0x40000000u),"original event-skip zero/nonzero flag mask differs");
    }
    const unsigned char short_cine[]={0x12},short_skip[]={0x6a};const Code bad_cine[]={{short_cine,1},{end,sizeof(end)}},bad_skip[]={{short_skip,1},{end,sizeof(end)}};
    bind(bad_cine,2);check(rx_events_start_room()!=NULL,"truncated cinematic command accepted");
    bind(bad_skip,2);check(rx_events_start_room()!=NULL,"truncated skip command accepted");
    sys->cb_flg=old_cb;sys->st_flg=old_status;sys->sp_flg=old_sp;sys->gm_flg=old_game;sys->cine_ap=old_ap;sys->cine_an=old_an;
    puts("PASS original CineSet/EventSkipSet: all selector bytes, cinema/task/skip flag masks, offsets, original default behavior, actual room bytes and truncation");
}
static void check_equipment_change(const unsigned char *retail)
{
    BH_PWORK saved=*plp;char saved_bank[4];memcpy(saved_bank,sys->ply_wno,4);
    char old_player=sys->ply_id;unsigned char old_monitor[4];memcpy(old_monitor,&sys->mn_mode0,4);
    for(unsigned bank=0;bank<4;++bank)for(unsigned number=0;number<=20;++number) {
        sys->ply_id=(char)bank;memset(sys->ply_wno,0x55,4);memset(&sys->mn_mode0,0xa5,4);
        plp->wpnr_no=-1;plp->wpnl_no=17;unsigned char code[]={0x27,(unsigned char)number,0,0};
        if(number==1){check(retail[0]==0x27&&retail[1]==1,"retail weapon-change command differs");memcpy(code,retail,2);}
        BH_PWORK expected=*plp;expected.wpnr_no=(int)number;run_common(code,sizeof(code));
        check(!memcmp(plp,&expected,sizeof(expected)),"original equipment request changed unrelated player fields");
        for(unsigned i=0;i<4;++i)check((unsigned char)sys->ply_wno[i]==(i==bank?number:0x55),"original weapon request changed wrong save bank");
        check(sys->mn_mode0==3&&!sys->mn_mode1&&!sys->mn_mode2&&!sys->mn_mode3,"original monitor request word differs");
    }
    for(unsigned bad=4;bad<256;++bad) {
        sys->ply_id=(char)bad;const unsigned char code[]={0x27,1,0,0};const Code fixture[]={{code,sizeof(code)},{end,sizeof(end)}};bind(fixture,2);
        Event_T_timer=123;BH_PWORK before=*plp;check(rx_events_start_room()!=NULL&&Event_T_timer==123&&!memcmp(plp,&before,sizeof(before)),"invalid equipment player bank modified state");
    }
    sys->ply_id=0;
    for(unsigned bad=21;bad<256;++bad) {
        unsigned char code[]={0x27,(unsigned char)bad,0,0};const Code fixture[]={{code,sizeof(code)},{end,sizeof(end)}};bind(fixture,2);
        Event_T_timer=123;check(rx_events_start_room()!=NULL&&Event_T_timer==123,"unported equipment package passed preflight");
    }
    const unsigned char truncated[]={0x27};const Code short_code[]={{truncated,1},{end,sizeof(end)}};bind(short_code,2);
    check(rx_events_start_room()!=NULL,"truncated equipment request accepted");
    const unsigned char valid[]={0x27,1,0,0};const Code fixture[]={{valid,sizeof(valid)},{end,sizeof(end)}};bind(fixture,2);
    BH_PWORK *previous=plp;plp=NULL;check(rx_events_start_room()!=NULL,"absent player reached equipment request");plp=previous;
    *plp=saved;memcpy(sys->ply_wno,saved_bank,4);sys->ply_id=old_player;memcpy(&sys->mn_mode0,old_monitor,4);
    puts("PASS original ArmsItemChange: 21 selectors/four saved banks, complete monitor word, unchanged left weapon/player state, actual RM_0000 bytes and guarded invalid requests");
}
static void check_inventory_commands(void)
{
    unsigned saved[384];memcpy(saved,sys->itm,sizeof(saved));
    char old_player=sys->ply_id;unsigned old_flags=sys->rm_flg;
    /* Execute the original condition inside If/Else, including both slot edges.
       Slot zero and slots 12..15 belong outside this original search. */
    unsigned char code[]={1,10,0x11,104,5,1,9,0,0,0,2,8,5,1,10,0,0,0,0,0};
    const unsigned ids[]={0,31,55,72,104,255};
    for (unsigned bank=0;bank<24;++bank) for (unsigned slot=0;slot<16;++slot)
        for (unsigned id=0;id<sizeof(ids)/sizeof(*ids);++id) {
            memset(sys->itm,0xaa,sizeof(sys->itm));sys->ply_id=(char)bank;
            sys->itm[bank*16+slot]=(ids[id]<<16)|0x1234;code[3]=(unsigned char)ids[id];
            sys->rm_flg=0x502;
            run_common(code,sizeof(code));
            int found=slot>=1&&slot<=11;
            check(flag(9)==found && flag(10)==!found,"original inventory search range/branch differs");
            check(sys->rm_flg==0x502,"ordinary inventory check changed room flags");
            check(sys->itm[bank*16+slot]==((ids[id]<<16)|0x1234),"inventory check consumed or changed item");
        }
    for (unsigned variant=0;variant<2;++variant) {
        memset(sys->itm,0xff,sizeof(sys->itm));sys->ply_id=0;sys->rm_flg=0x502;
        sys->itm[1]=((72u|(variant?0x100u:0))<<16)|7;code[3]=72;
        run_common(code,sizeof(code));
        check(flag(9)&&sys->rm_flg==(0x502u|(variant?0x10000u:0)),"original extended item-72 flag behavior differs");
    }
    /* Another player's matching item must not satisfy the active bank. */
    memset(sys->itm,0xff,sizeof(sys->itm));sys->ply_id=1;sys->itm[1]=104u<<16;code[3]=104;
    run_common(code,sizeof(code));check(flag(10),"inventory search leaked across player banks");
    for (unsigned bank=24;bank<256;++bank) {
        sys->ply_id=(char)bank;
        const Code invalid[]={{code,sizeof(code)},{end,sizeof(end)}};bind(invalid,2);
        Event_T_timer=123;bhEtask[15].status=1;
        check(rx_events_start_room()!=NULL&&rx_events_fault()->opcode==0x11&&Event_T_timer==123&&bhEtask[15].status==1,
              "invalid player bank reached inventory or modified scheduler");
    }
    sys->ply_id=0;const unsigned char truncated[]={0x11};
    const Code invalid[]={{truncated,sizeof(truncated)},{end,sizeof(end)}};bind(invalid,2);
    Event_T_timer=123;
    check(rx_events_start_room()!=NULL&&Event_T_timer==123,"truncated inventory condition passed preflight");
    memcpy(sys->itm,saved,sizeof(saved));sys->ply_id=old_player;sys->rm_flg=old_flags;
    puts("PASS original PlItemCheck: all 24 banks/16 slots, item bytes, conditional branches, extended item-72 flag and rejected banks/truncation");
}
static void check_item_consumption(const unsigned char *retail)
{
    unsigned saved[384];memcpy(saved,sys->itm,sizeof(saved));char old_player=sys->ply_id;
    unsigned old_room=sys->rm_flg,old_cb=sys->cb_flg;
    const unsigned ids[]={0,31,55,72,104,255};
    check(retail[0]==0x31&&retail[1]==104,"retail item consumption bytes differ");
    for(unsigned bank=0;bank<24;++bank)for(unsigned slot=0;slot<16;++slot)
        for(unsigned selection=0;selection<16;++selection)for(unsigned id=0;id<6;++id) {
            unsigned base=bank*16,before[384],expected[384];
            for(unsigned i=0;i<384;++i)sys->itm[i]=0xb0aa0000u|i;
            sys->itm[base]=selection<14?selection:selection==14?base+slot:UINT_MAX;
            sys->itm[base+slot]=0x84000000u|(ids[id]<<16)|0x1234;
            /* Matching items in another bank and a duplicate later in this bank
               must survive. Shift expectations use the untouched input snapshot. */
            sys->itm[((bank+1)%24)*16+2]=ids[id]<<16;
            if(slot==4)sys->itm[base+8]=(ids[id]<<16)|0x5678;
            sys->ply_id=(char)bank;sys->rm_flg=0x502;sys->cb_flg=0xa5a5;
            memcpy(before,sys->itm,sizeof(before));memcpy(expected,before,sizeof(expected));
            if(slot>=2&&slot<=11) {
                unsigned header=before[base];
                if(header>=slot)expected[base]=header==base+slot?0:header-1;
                memcpy(expected+base+slot,before+base+slot+1,(12-slot)*sizeof(unsigned));
            }
            unsigned char code[]={0x31,(unsigned char)ids[id],5,1,30,0,0,0,0,0};
            if(ids[id]==104)memcpy(code,retail,2);
            run_common(code,sizeof(code));
            check(flag(30)&&!memcmp(sys->itm,expected,sizeof(expected))&&sys->rm_flg==0x502&&sys->cb_flg==0xa5a5,
                  "original item removal/selection/compaction or unrelated state differs");
        }
    for(unsigned id=0;id<256;++id) {
        unsigned filler=((id+1)&255)<<16;for(unsigned i=0;i<384;++i)sys->itm[i]=filler;
        sys->ply_id=23;sys->itm[23*16]=11;sys->itm[23*16+11]=(id<<16)|0xffff;
        unsigned tail=sys->itm[23*16+12];unsigned char code[]={0x31,(unsigned char)id,0,0};run_common(code,sizeof(code));
        check(sys->itm[23*16]==10&&sys->itm[23*16+11]==tail&&sys->itm[23*16+12]==tail,
              "original full item byte or final-slot tail copy differs");
    }
    /* The source compares the absolute slot with the selection word. Preserve
       this behavior even for banks where a relative-slot interpretation differs. */
    for(unsigned bank=0;bank<2;++bank) {
        memset(sys->itm,0xaa,sizeof(sys->itm));sys->ply_id=(char)bank;sys->itm[bank*16]=2;sys->itm[bank*16+2]=104u<<16;
        const unsigned char code[]={0x31,104,0,0};run_common(code,sizeof(code));
        check(sys->itm[bank*16]==(bank?1u:0u),"original absolute-slot selection comparison was normalized");
    }
    for(unsigned bank=24;bank<256;++bank) {
        sys->ply_id=(char)bank;const Code bad[]={{retail,2},{end,sizeof(end)}};bind(bad,2);
        unsigned before[384];memcpy(before,sys->itm,sizeof(before));Event_T_timer=123;bhEtask[15].status=1;
        check(rx_events_start_room()!=NULL&&Event_T_timer==123&&bhEtask[15].status==1&&!memcmp(before,sys->itm,sizeof(before)),
              "invalid consumption bank changed storage/scheduler");
    }
    sys->ply_id=0;const Code bad[]={{retail,1},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
    check(rx_events_start_room()!=NULL&&rx_events_fault()->opcode==0x31&&Event_T_timer==123,"truncated consumption crossed its script entry");
    memcpy(sys->itm,saved,sizeof(saved));sys->ply_id=old_player;sys->rm_flg=old_room;sys->cb_flg=old_cb;
    puts("PASS original PlItemLost: all storage banks/slots, all item bytes, selection-word quirks, first duplicate, slot-12 tail copy, actual RM_0000 command and guarded malformed input");
}

static void check_trigger_update(const unsigned char *retail)
{
    struct Bank {uint64_t head;ATR_WORK records[256];uint64_t tail;};static struct Bank bank,expected;
    ATR_WORK *old_table=rom->etcp;int old_count=rom->etc_n;
    check(retail[0]==0x25&&retail[1]==5,"retail interaction record command differs");
    rom->etcp=bank.records;rom->etc_n=256;
    const unsigned words[]={0,1,0x8000,0xffff};
    for(unsigned index=0;index<256;++index)for(unsigned value=0;value<4;++value) {
        memset(&bank,0xa5,sizeof(bank));memcpy(&expected,&bank,sizeof(bank));
        unsigned char code[]={0x25,(unsigned char)index,(unsigned char)words[value],(unsigned char)(words[value]>>8),
            (unsigned char)index,(unsigned char)(255-index),(unsigned char)(index^0x80),(unsigned char)(value*85),(unsigned char)(value*85),0xff,0,0};
        ATR_WORK *record=&expected.records[index];record->attr=words[value];
        record->prm0=code[4];record->prm1=code[5];record->prm2=code[6];record->prm3=code[7];record->type=code[8];
        run_common(code,sizeof(code));check(!memcmp(&bank,&expected,sizeof(bank)),"original trigger update changed flags/geometry/other records or used wrong native stride");
    }
    memset(&bank,0xa5,sizeof(bank));memcpy(&expected,&bank,sizeof(bank));unsigned char code[12]={0};memcpy(code,retail,10);
    ATR_WORK *record=&expected.records[5];record->attr=0x8000;record->prm0=0;record->prm1=3;record->prm2=0;record->prm3=0;record->type=3;
    run_common(code,sizeof(code));check(!memcmp(&bank,&expected,sizeof(bank)),"retail trigger update differs from independent expected record");
    for(unsigned variant=0;variant<4;++variant) {
        rom->etcp=variant?bank.records:NULL;rom->etc_n=variant==1?0:variant==2?-1:5;
        const Code bad[]={{code,sizeof(code)},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
        memcpy(&expected,&bank,sizeof(bank));check(rx_events_start_room()!=NULL&&Event_T_timer==123&&!memcmp(&expected,&bank,sizeof(bank)),"invalid trigger bank changed state");
    }
    rom->etcp=bank.records;rom->etc_n=256;
    for(unsigned bytes=1;bytes<10;++bytes) {
        const Code bad[]={{code,bytes},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
        check(rx_events_start_room()!=NULL&&Event_T_timer==123,"truncated trigger update passed preflight");
    }
    rom->etcp=old_table;rom->etc_n=old_count;
    puts("PASS original EtcAtariSet2: 256 records, unsigned attributes/parameters/type, ignored padding, full geometry/canary preservation, actual room bytes and rejected banks/truncation");
}

static void check_case_cleanup(const unsigned char *retail)
{
    static O_WRK previous[512],expected[512];memcpy(previous,eff,sizeof(previous));
    const unsigned flags[]={0,1,2,3,0xffffffffu,0x80000000u,0x80000001u};
    for(unsigned f=0;f<7;++f)for(unsigned padding=0;padding<3;++padding) {
        memset(eff,0xa5,sizeof(expected));
        for(unsigned i=0;i<512;++i){eff[i].id=(unsigned short)(10+i);eff[i].flg=flags[f];}
        eff[0].id=0;eff[127].id=7;eff[511].id=7;eff[255].id=65535;
        memcpy(expected,eff,sizeof(expected));
        if(flags[f]&1){expected[0].flg=0;expected[127].flg=0;expected[511].flg=0;}
        unsigned char code[]={0x6b,(unsigned char)(padding?padding==1?1:255:0),0,0};
        if(!padding){check(retail[0]==0x6b&&retail[1]==0,"retail case cleanup bytes differ");memcpy(code,retail,2);}
        run_common(code,sizeof(code));check(!memcmp(expected,eff,sizeof(expected)),"original case cleanup changed non-case effects or non-flag data");
    }
    const Code bad[]={{retail,1},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
    check(rx_events_start_room()!=NULL&&Event_T_timer==123,"truncated case cleanup changed scheduler");
    memcpy(eff,previous,sizeof(previous));
    puts("PASS original DelYakkyou/bhDeleteYakkyou: full 512-slot pool, IDs/active-bit masks, ignored padding, unrelated data preservation and actual room bytes");
}
static void check_pickup_commands(void)
{
    static O_WRK items[32],expected_items[32];static ATR_WORK collisions[3],expected_collisions[3];
    O_WRK *old_items=sys->itwp;ATR_WORK *old_collisions=rom->etcp;int old_count=rom->etc_n;
    unsigned old_cb=sys->cb_flg,old_idx=sys->etc_idx,old_it[16],old_ic[16];
    memcpy(old_it,sys->it_flg,sizeof(old_it));memcpy(old_ic,sys->ic_flg,sizeof(old_ic));
    sys->itwp=items;rom->etcp=collisions;rom->etc_n=3;
    for (unsigned bit=0;bit<512;++bit) for (unsigned present=0;present<2;++present)
        for (unsigned keep=0;keep<2;++keep) {
            unsigned word=bit/32,mask=0x80000000u>>(bit%32);
            memset(items,0x5a,sizeof(items));memset(collisions,0xa5,sizeof(collisions));
            memset(sys->it_flg,0,sizeof(sys->it_flg));if(present)sys->it_flg[word]|=mask;
            memcpy(expected_items,items,sizeof(items));memcpy(expected_collisions,collisions,sizeof(collisions));
            unsigned char code[]={0x23,31,(unsigned char)bit,(unsigned char)((bit|0xfe00)>>8),2,keep?255:0,0,0};
            if(present) { if(!keep)expected_collisions[2].flg&=~1u;expected_items[31].stflg|=0x1000000; }
            else expected_collisions[2].flg|=1;
            run_common(code,sizeof(code));
            check(!memcmp(items,expected_items,sizeof(items))&&!memcmp(collisions,expected_collisions,sizeof(collisions)),
                  "original pickup visibility/interaction restoration differs");
            check(sys->it_flg[word]==(present?mask:0),"pickup restoration modified collected flags");
        }
    for (unsigned bit=0;bit<512;++bit) for (unsigned collected=0;collected<2;++collected)
        for (unsigned take=0;take<2;++take) for (unsigned same=0;same<2;++same) {
            unsigned word=bit/32,mask=0x80000000u>>(bit%32),expected_it[16]={0},expected_ic[16];
            memset(items,0x5a,sizeof(items));memset(collisions,0xa5,sizeof(collisions));collisions[2].prm0=31;
            memset(sys->it_flg,0,sizeof(sys->it_flg));if(collected)sys->it_flg[word]|=mask;
            memset(sys->ic_flg,0x55,sizeof(sys->ic_flg));
            memcpy(expected_it,sys->it_flg,sizeof(expected_it));memcpy(expected_ic,sys->ic_flg,sizeof(expected_ic));
            memcpy(expected_items,items,sizeof(items));memcpy(expected_collisions,collisions,sizeof(collisions));
            sys->etc_idx=same?2:1;sys->cb_flg=0x124|(take?0x800:0);
            unsigned char code[]={0x0e,0xff,(unsigned char)bit,(unsigned char)((bit|0xfc00)>>8),2,(unsigned char)(bit&1),0,0};
            if(same&&!collected) {
                if(take) {if(!code[5])expected_collisions[2].flg&=~1u;expected_items[31].stflg|=0x1000000;
                    expected_it[word]|=mask;expected_ic[(bit&0x1ff)/32]&=~mask;}
                else expected_ic[word]|=mask;
            }
            run_common(code,sizeof(code));
            check(!memcmp(items,expected_items,sizeof(items))&&!memcmp(collisions,expected_collisions,sizeof(collisions))&&
                  !memcmp(sys->it_flg,expected_it,sizeof(expected_it))&&!memcmp(sys->ic_flg,expected_ic,sizeof(expected_ic))&&
                  sys->cb_flg==(0x124u|(!same&&take?0x800u:0)),"original pickup result/cancel/index handling differs");
        }
    for (unsigned variant=0;variant<8;++variant) {
        unsigned char code[]={variant&1?0x0e:0x23,31,0,0,2,0,0,0};
        rom->etcp=variant==0?NULL:collisions;rom->etc_n=variant==1?0:variant==2?-1:3;
        sys->itwp=variant==3?NULL:items;collisions[2].prm0=variant==5?32:31;
        if(variant==4)code[1]=32;if(variant==6)code[4]=3;if(variant==7)code[3]=2;
        const Code bad[]={{code,sizeof(code)},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
        check(rx_events_start_room()!=NULL&&Event_T_timer==123,"invalid pickup references passed preflight");
    }
    rom->etcp=collisions;rom->etc_n=3;sys->itwp=items;collisions[2].prm0=31;
    for(unsigned op=0;op<2;++op) for(unsigned length=1;length<6;++length) {
        unsigned char code[]={op?0x0e:0x23,31,0,0,2,0};
        const Code bad[]={{code,length},{end,sizeof(end)}};bind(bad,2);Event_T_timer=123;
        check(rx_events_start_room()!=NULL&&Event_T_timer==123,"truncated pickup command changed scheduler");
    }
    sys->itwp=old_items;rom->etcp=old_collisions;rom->etc_n=old_count;sys->cb_flg=old_cb;sys->etc_idx=old_idx;
    memcpy(sys->it_flg,old_it,sizeof(old_it));memcpy(sys->ic_flg,old_ic,sizeof(old_ic));
    puts("PASS original ItmSetCk/ItmCk all 512 pickup flags, original masks, visibility/trigger restoration, take/cancel and rejected native references/truncation");
}
static void check_common_commands(void)
{
    static O_WRK objects[32],items[32];static NJS_CNK_OBJECT nodes[3],other[2];
    O_WRK *previous_objects=sys->obwp,*previous_items=sys->itwp;
    sys->obwp=objects;sys->itwp=items;
    check((uintptr_t)objects>UINT32_MAX && (uintptr_t)nodes>UINT32_MAX,"common checks require high native work/model addresses");
    const unsigned char angles[]={0x65,2,31,0, 0x69,13,1,0,255,0, 0x69,11,90,255,180,0, 0,0};
    run_common(angles,sizeof(angles));
    check(objects[31].ax==-16384 && objects[31].ay==46421 && objects[31].az==-32768 &&
          objects[31].axb==objects[31].ax && objects[31].ayb==objects[31].ay && objects[31].azb==objects[31].az &&
          bhEtask[0].bax==1 && !bhEtask[0].bay && bhEtask[0].baz==255,
          "original common unsigned angles/sign selectors/backup rotation differ");
    objects[31].px=2;objects[31].py=3;objects[31].pz=4;
    objects[31].pxb=-111;objects[31].pyb=-222;objects[31].pzb=-333;
    objects[31].ax=INT_MAX-10;objects[31].ay=INT_MIN+10;objects[31].az=0;objects[31].axb=123;
    const unsigned char steps[]={0x65,2,31,0, 0x69,12,0,1,255,0, 0x69,5,255,17,1,0,
        0x69,2,0, 0x69,13,0,1,255,0, 0x69,6,255,17,1,0, 0x69,3,0, 0,0};
    run_common(steps,sizeof(steps));
    check(fabsf(objects[31].px-27.5f)<.00001f && fabsf(objects[31].py-1.3f)<.00001f && fabsf(objects[31].pz-3.9f)<.00001f &&
          objects[31].pxb==-111 && objects[31].pyb==-222 && objects[31].pzb==-333,
          "original common translation steps changed signs or backup positions");
    check((uint32_t)objects[31].ax==(uint32_t)(INT_MAX-10)+23210u &&
          (uint32_t)objects[31].ay==(uint32_t)(INT_MIN+10)-1547u && objects[31].az==-91 && objects[31].axb==123,
          "original common angular steps/32-bit wrapping differ");
    float ground[32];memcpy(ground,rom->grand,sizeof(ground));memset(rom->grand,0,sizeof(ground));
    rom->grand[0]=-50;rom->grand[1]=-25;rom->grand[3]=10;rom->grand[4]=20;rom->grand[5]=30;
    const unsigned char position[]={0x65,2,31,0, 0x69,12,1,0,1,0, 0x69,7,0x39,0x30,0x02,0x08,0xff,0xff, 0,0};
    run_common(position,sizeof(position));
    check(fabsf(objects[31].px+123.45f)<.0001f && objects[31].py==20.5f && fabsf(objects[31].pz+655.35f)<.0001f &&
          objects[31].pxb==objects[31].px && objects[31].pyb==objects[31].py && objects[31].pzb==objects[31].pz &&
          objects[31].flr_no==2,"original common absolute position/unsigned words/floor selection differ");
    memcpy(rom->grand,ground,sizeof(ground));
    objects[31].mdl[0]=(ML_WORK){.objP=nodes,.obj_num=3};objects[31].mdl[1]=(ML_WORK){.objP=other,.obj_num=2};
    objects[31].mlwP=&objects[31].mdl[1];nodes[0].pos[0]=42;other[0].pos[0]=1;other[0].pos[1]=2;other[0].pos[2]=3;
    const unsigned char joint[]={0x65,2,31,2, 0x69,13,1,0,255,0, 0x69,9,255,90,128,0, 0x69,36, 0,0};
    run_common(joint,sizeof(joint));
    check(nodes[2].ang[0]==-46421 && nodes[2].ang[1]==16384 && nodes[2].ang[2]==-23301 &&
          !other[0].pos[0] && !other[0].pos[1] && !other[0].pos[2] && nodes[0].pos[0]==42 && objects[31].ax!=(int)nodes[2].ang[0],
          "original common selected-node rotation/active-model root reset differ");
    const uint32_t seed=0x80000119;const uint32_t expected[]={seed&~0x118u,seed|0x118u,seed&~0x18u,seed|0x18u};
    for (unsigned selector=0;selector<4;++selector) {
        unsigned char flags[]={0x65,2,31,0,0x69,23,(unsigned char)selector,0,0,0};
        objects[31].flg=seed;run_common(flags,sizeof(flags));check(objects[31].flg==expected[selector],"original common display flag mask differs");
    }
    for (unsigned selector=0;selector<2;++selector) {
        unsigned char flags[]={0x65,2,31,0,0x69,41,0,0,0,0};flags[6]=flags[7]=selector?255:0;
        objects[31].flg=seed;run_common(flags,sizeof(flags));
        check(objects[31].flg==(selector?seed|0x40000u:seed&~0x40000u),"original common word-controlled flag differs");
    }
    const unsigned char nop[]={0x69,0,5,1,0,0,0,0,0,0};run_common(nop,sizeof(nop));check(flag(0),"original common no-op length lost following command");
    const unsigned char entry[]={20,0,0,0,20,0,1,1,0,0};
    const unsigned char first[]={0x65,2,0,0,0x69,12,0,0,0,0,0x69,5,10,0,0,0,0xf8,0xf9,1,0,0x69,2,0,0xff,0};
    const unsigned char second[]={0x65,3,31,0,0x69,12,1,0,0,0,0x69,5,20,0,0,0,0xf8,0xf9,1,0,0x69,2,0,0xff,0};
    const Code concurrent[]={{entry,sizeof(entry)},{end,sizeof(end)},{first,sizeof(first)},{second,sizeof(second)}};
    objects[0].px=100;items[31].px=50;bind(concurrent,4);check(!rx_events_start_room(),"start concurrent common commands");
    for (unsigned i=0;i<4;++i) tick();
    check(objects[0].px==101 && items[31].px==48 && !bhEtask[0].status && !bhEtask[1].status,
          "common transform task state leaked across suspended native events");
    const unsigned char selected_joint[]={0x65,2,31,3,0x69,9,1,2,3,0,0,0};
    const Code invalid_joint[]={{selected_joint,sizeof(selected_joint)},{end,sizeof(end)}};
    bind(invalid_joint,2);check(rx_events_start_room()!=NULL,"common node index escaped model bounds");
    const unsigned char guarded[]={0x69,11,1,2,3,0,0xff,0};
    const Code deferred[]={{end,sizeof(end)},{end,sizeof(end)},{guarded,sizeof(guarded)}};
    bind(deferred,3);check(!rx_events_start_room(),"bind guarded common command");
    bhScePtr=rx_room_script(&scripts,2);bhCetask->work=(BH_PWORK *)(uintptr_t)1;
    check(!rx_event_dispatch() && rx_events_fault()->opcode==0x69,"invalid native common actor was dereferenced");
    bind(deferred,3);check(!rx_events_start_room(),"rebind guarded common command");
    bhScePtr=rx_room_script(&scripts,2);bhCetask=(BH_SCEWORK *)(uintptr_t)1;
    check(!rx_event_dispatch() && rx_events_fault()->opcode==0x69,"invalid native common task was dereferenced on failure");
    const unsigned char variable[][8]={{0x69,2,0},{0x69,23,0,0},{0x69,13,0,0,0,0},{0x69,7,0,0,0,0,0,0}};
    const unsigned lengths[]={3,4,6,8};
    for (unsigned command=0;command<4;++command) for (unsigned length=1;length<lengths[command];++length) {
        const Code short_command[]={{variable[command],length},{end,sizeof(end)}};bind(short_command,2);
        Event_T_timer=123;bhEtask[15].status=1;
        check(rx_events_start_room()!=NULL && rx_events_fault()->opcode==0x69 && Event_T_timer==123 && bhEtask[15].status==1,
              "truncated or cross-entry common command changed engine state");
    }
    rx_events_reset();sys->obwp=previous_objects;sys->itwp=previous_items;
    puts("PASS original common transforms/signs/unsigned words, backup fields, floor selection, node/root references and flag masks");
    puts("PASS variable-length/unaligned common dispatch, signed 32-bit angle wrapping and suspended task isolation above 4 GiB");
    puts("PASS common native actor/task/model guards and truncated/cross-entry preflight without state changes");
}

static void check_light_commands(void)
{
    struct Bank { uint64_t before;LGT_WORK values[4];uint64_t after; };
    static struct Bank room_lights,event_lights;
    LGT_WORK *previous_room=rom->lgtp,*previous_event=rom->evlp;
    int room_count=rom->lgt_n,event_count=rom->evl_n;
    memset(&room_lights,0xa5,sizeof(room_lights));memset(&event_lights,0x5a,sizeof(event_lights));
    room_lights.before=event_lights.before=UINT64_C(0x1122334455667788);
    room_lights.after=event_lights.after=UINT64_C(0x8877665544332211);
    rom->lgtp=room_lights.values;rom->lgt_n=3;rom->evlp=event_lights.values;rom->evl_n=4;
    check((uintptr_t)rom->lgtp>UINT32_MAX && (uintptr_t)rom->evlp>UINT32_MAX,"light banks require native high addresses");
    const unsigned selectors[]={0,1,2,255},actions[]={0,1,2,255};
    for (unsigned bank=0;bank<4;++bank) for (unsigned action=0;action<4;++action)
        for (unsigned index=0;index<(bank?4u:3u);++index) {
            LGT_WORK *target=bank?rom->evlp:rom->lgtp;
            target[index].flg=action?0x5a5a5a5b:0xa5a5a5a4;
            struct Bank expected_room,expected_event;
            memcpy(&expected_room,&room_lights,sizeof(room_lights));memcpy(&expected_event,&event_lights,sizeof(event_lights));
            LGT_WORK *expected=bank?expected_event.values:expected_room.values;
            if (action) expected[index].flg&=~1u;else expected[index].flg|=1;
            unsigned char code[]={0x35,(unsigned char)actions[action],(unsigned char)index,(unsigned char)selectors[bank],0,0};
            run_common(code,sizeof(code));
            check(!memcmp(&expected_room,&room_lights,sizeof(room_lights)) && !memcmp(&expected_event,&event_lights,sizeof(event_lights)),
                  "original LightSet bank/unsigned selectors/flag preservation/native stride differ");
        }
    const unsigned char bad[][6]={{0x35,0,3,0,0,0},{0x35,0,4,1,0,0},{0x35,0,255,255,0,0}};
    for (unsigned i=0;i<3;++i) {
        const Code invalid[]={{bad[i],sizeof(bad[i])},{end,sizeof(end)}};bind(invalid,2);
        struct Bank saved_room,saved_event;memcpy(&saved_room,&room_lights,sizeof(saved_room));memcpy(&saved_event,&event_lights,sizeof(saved_event));
        Event_T_timer=123;bhEtask[15].status=1;
        check(rx_events_start_room()!=NULL && rx_events_fault()->opcode==0x35 && Event_T_timer==123 && bhEtask[15].status==1 &&
              !memcmp(&saved_room,&room_lights,sizeof(saved_room)) && !memcmp(&saved_event,&event_lights,sizeof(saved_event)),
              "out-of-range light selection changed native bank/task state");
    }
    const unsigned char valid[]={0x35,0,0,0,0,0};
    for (unsigned variant=0;variant<3;++variant) {
        rom->lgtp=variant?room_lights.values:NULL;rom->lgt_n=variant==1?0:variant==2?-1:3;
        const Code invalid[]={{valid,sizeof(valid)},{end,sizeof(end)}};bind(invalid,2);
        check(rx_events_start_room()!=NULL,"absent/empty/negative light bank was dereferenced");
    }
    rom->lgtp=room_lights.values;rom->lgt_n=3;
    for (unsigned length=1;length<4;++length) {
        const Code truncated[]={{valid,length},{end,sizeof(end)}};bind(truncated,2);
        Event_T_timer=123;
        check(rx_events_start_room()!=NULL && rx_events_fault()->opcode==0x35 && Event_T_timer==123,
              "truncated/cross-entry LightSet changed event state");
    }
    rx_events_reset();rom->lgtp=previous_room;rom->lgt_n=room_count;rom->evlp=previous_event;rom->evl_n=event_count;
    puts("PASS original LightSet room/event banks, zero/nonzero byte semantics, unrelated flags/data and guarded native strides above 4 GiB");
    puts("PASS LightSet absent/out-of-range banks and truncated/cross-entry command preflight without state changes");
}

static void check_faults(void)
{
    const unsigned char unported[]={0x69,0xfe,0,1,0,0};
    const Code pending[]={{unported,sizeof(unported)},{end,sizeof(end)}};
    bind(pending,2);
    RxEventFault fault;
    bhEtask[15].status=1;
    Event_T_timer=123;
    check(rx_events_validate_room(&fault)&&fault.opcode==0x69&&fault.offset==8,
          "unported handler was accepted");
    check(rx_events_start_room()&&rx_events_fault()->opcode==0x69&&
          bhEtask[15].status==1&&Event_T_timer==123,
          "room preflight changed task state before rejecting an unported command");

    const unsigned char entry[]={20,0,0,0,0,0};
    const unsigned char underflow[]={0xfb,0,0xff,0};
    const Code invalid[]={{entry,sizeof(entry)},{end,sizeof(end)},{underflow,sizeof(underflow)}};
    bind(invalid,3);
    check(rx_events_start_room()&&rx_events_fault()->opcode==0xfb&&!bhEtask[0].status,
          "counter underflow was executed");
    const unsigned char overflow[]={0xfa,0,1,0,0xfa,0,1,0,0xfa,0,1,0,0xfa,0,1,0,
                                    0xfa,0,1,0,0xff,0};
    const Code deep[]={{entry,sizeof(entry)},{end,sizeof(end)},{overflow,sizeof(overflow)}};
    bind(deep,3);
    check(rx_events_start_room()&&bhEtask[0].loop==3&&rx_events_fault()->opcode==0xfa,
          "counter overflow reached original work");

    const unsigned char busy[]={0xfa,0,0,0,0xfb,0,0xff,0};
    const Code endless[]={{entry,sizeof(entry)},{end,sizeof(end)},{busy,sizeof(busy)}};
    bind(endless,3);
    check(rx_events_start_room()&&strstr(rx_events_fault()->reason,"budget"),
          "non-yielding loop escaped the frame budget");
    const Code empty[]={{end,sizeof(end)},{end,sizeof(end)}};
    bind(empty,2);
    sys->sp_flg|=0x10;
    bhEtask[0].status=1;
    bhEtask[0].data=(unsigned char *)(uintptr_t)1;
    check(rx_events_run_tasks()&&!bhEtask[0].status,"invalid continuation was dereferenced");
    puts("PASS unsupported-opcode preflight, counter bounds, bad continuations and finite dispatch");
}

static void check_actor_commands(void)
{
    static O_WRK objects[32], items[32];
    static ETTY_WORK records[2];
    static NJS_CNK_OBJECT nodes[4];
    static ML_WORK model;
    static MN_WORK motions[10];
    static ATR_WORK walls[2], triggers[2], floors[2];
    sys->obwp=objects; sys->itwp=items;
    rom->enep=records; rom->ene_n=2;
    records[0].wrk_no=7; records[1].wrk_no=11;
    memset(ene,0,sizeof(ene));
    memset(plp,0,sizeof(*plp));
    model=(ML_WORK){.obj_num=4,.objP=nodes};
    plp->mlwP=ene[7].mlwP=objects[31].mlwP=&model;
    sys->rmthp=motions;
    motions[2]=(MN_WORK){.datP=nodes,.frm_num=30};
    rom->walp=walls; rom->wal_n=2;
    rom->etcp=triggers; rom->etc_n=2;
    rom->flrp=floors; rom->flr_n=2;

    const unsigned char hide[]={0x24,0,0,0, 0x24,1,0,0, 0x24,2,31,0, 0x24,3,31,0,
        0x20,0,0,0, 0x20,0,1,0, 0x20,31,2,0, 0x20,31,3,0, 0,0};
    const Code hidden[]={{hide,sizeof(hide)},{end,sizeof(end)}};
    bind(hidden,2);
    plp->stflg=0x40;
    check(!rx_events_start_room()&&(plp->stflg&0x1000040)==0x1000040&&
        (ene[7].stflg&0x1000000)&&(ene[7].flg&0x8000)&&
        (objects[31].stflg&0x1000000)&&(items[31].stflg&0x1000000)&&
        (plp->mdflg&1)&&(ene[7].mdflg&1)&&(objects[31].mdflg&1)&&(items[31].mdflg&1),
        "original actor hide/display flags or reserved work bounds differ");
    const unsigned char show[]={0x24,0,0,1, 0x24,1,0,1, 0x24,2,31,1, 0x24,3,31,1,
        0x20,0,0,1, 0x20,0,1,1, 0x20,31,2,1, 0x20,31,3,1, 0,0};
    const Code shown[]={{show,sizeof(show)},{end,sizeof(end)}};
    bind(shown,2);
    check(!rx_events_start_room()&&plp->stflg==0x40&&!(ene[7].stflg&0x1000000)&&
        !(ene[7].flg&0x8000)&&!(objects[31].stflg&0x1000000)&&
        !(items[31].stflg&0x1000000)&&!(plp->mdflg&1)&&!(ene[7].mdflg&1)&&
        !(objects[31].mdflg&1)&&!(items[31].mdflg&1),"original show did not clear only its flags");
    bind(shown,2);
    ene[7].flg=0x80008000u;
    check(!rx_events_start_room()&&ene[7].flg==0x80008000u,
          "original show cleared the inactive/dead enemy latch");
    bind(hidden,2);
    sys->stg_no=9; sys->rom_no=30; sys->rcase=2; pl_sleep_cnt=0;
    check(!rx_events_start_room()&&pl_sleep_cnt==2,"original player hide special case differs");
    sys->stg_no=sys->rom_no=sys->rcase=0;

    const unsigned char links[]={0x32,0,31,3,0,5, 0xdc,5,250,0,0x90,1,
        0x53,0,3,2,1,2, 100,0,200,0,0x2c,1,
        0x34,0,2,1,0,0, 50,0,60,0,70,0, 0x3b,0,0,2,0,0, 0,0};
    const Code attachments[]={{links,sizeof(links)},{end,sizeof(end)}};
    bind(attachments,2);
    items[3].flg=0x80;
    check(!rx_events_start_room()&&objects[31].lkwkp==(unsigned char *)&ene[7]&&
        objects[31].lkono==3&&(objects[31].flg&0x80)&&objects[31].lox==-15&&
        objects[31].loy==2.5f&&objects[31].loz==-4&&
        items[3].lkwkp==(unsigned char *)&ene[7]&&items[3].lkono==2&&
        !(items[3].flg&0x80)&&items[3].lox==1&&items[3].loy==-2&&items[3].loz==3&&
        objects[2].lkwkp==(unsigned char *)plp&&objects[2].lkono==1&&
        objects[2].lox==0.5f&&(nodes[2].evalflags&8),
        "original native actor attachment, offsets or node visibility differs");
    check((uintptr_t)objects[31].lkwkp>UINT32_MAX,"actor link pointer was truncated");

    static O_WORK shadow_joints[4];
    model.owP=shadow_joints;
    sys->efm[1]=(ML_WORK){.obj_num=2,.objP=nodes};
    memset(eff,0,sizeof(eff)); ene[7].flg=1;
    const unsigned char shadow_setup[]={0x2f,0,4,0,0x2f,0,0,0,0,0};
    const Code shadow_code[]={{shadow_setup,sizeof(shadow_setup)},{end,sizeof(end)}};
    bind(shadow_code,2);
    check(!rx_events_start_room()&&(ene[7].flg&0x800)&&eff[0].id==1&&
          eff[0].lkwkp==(unsigned char *)&ene[7]&&eff[0].sx==6.5f&&eff[0].sy==4&&
          eff[0].sz==4.5f&&!eff[1].flg,"original shadow setup or duplicate suppression differs");
    for (unsigned i=0;i<512;++i) eff[i].flg=3;
    ene[7].flg=1;
    bind(shadow_code,2);
    check(rx_events_start_room()&&strstr(rx_events_fault()->reason,"exhausted")&&ene[7].flg==1,
          "shadow pool exhaustion changed the actor before reporting failure");
    memset(eff,0,sizeof(eff));
    memset(&sys->efm[1],0,sizeof(sys->efm[1]));

    const unsigned char entry[]={20,0,0,0,20,0,1,1,0,0};
    const unsigned char player[]={0x65,0,0,0,0x2b,0,0x2e,0,0xfe,0x2b,1,0x2e,1,0xff,0};
    const unsigned char enemy[]={0x65,1,0,2,0x2b,0,0xfe,0x2b,1,0xff,0};
    const Code tasks[]={{entry,sizeof(entry)},{end,sizeof(end)},
                       {player,sizeof(player)},{enemy,sizeof(enemy)}};
    bind(tasks,4);
    ene[7].id=1; ene[7].exp1=(unsigned char *)&ene[11];
    ene[7].mtn_md=0x20; ene[7].mdflg=0x40; ene[7].hokan_rate=123;
    check(!rx_events_start_room()&&bhEtask[0].work==plp&&plp->mode0==7&&
        plp->mode3==4&&(plp->mtn_no&255)==42&&(plp->stflg&0x10000)&&
        !(sys->sp_flg&1)&&bhEtask[1].work==&ene[7]&&bhEtask[1].model_cno==2&&
        bhEtask[1].mtn_md==0x20&&bhEtask[1].mdflg==0x40&&bhEtask[1].hokan_rate==123&&
        ene[7].mode3==4&&ene[11].mode3==4,"original task actor binding or motion pause differs");
    tick();
    check(plp->mode3==1&&(sys->sp_flg&1)&&ene[7].mode3==1&&ene[11].mode3==1,
          "original multipart actor resume differs");
    const unsigned char setup[]={0x2d,0,0x30,0,2,0, 0x65,2,31,3,0,0};
    const Code paused[]={{setup,sizeof(setup)},{end,sizeof(end)}};
    bind(paused,2);
    ene[7].frm_no=ene[11].frm_no=65536;
    check(!rx_events_start_room()&&ene[7].mode0==5&&ene[11].mode0==5&&
        ene[7].mode3==4&&ene[11].mode3==4&&ene[7].frm_no==0&&ene[11].frm_no==0&&
        ene[7].mtn_no==2&&ene[11].mtn_no==2&&ene[7].mnwP==motions&&
        bhEtask[0].work==(BH_PWORK *)&objects[31]&&bhEtask[0].model_cno==3,
        "original room motion binding or object work selection differs");
    const unsigned char collision[]={0x0a,1,0,0,0x0b,1,1,0,0x0c,0,0,0,0,0};
    const Code bounds[]={{collision,sizeof(collision)},{end,sizeof(end)}};
    bind(bounds,2); walls[1].flg=0x40; triggers[1].flg=0x41;
    check(!rx_events_start_room()&&walls[1].flg==0x41&&triggers[1].flg==0x40&&
          (floors[0].flg&1),"original collision activation differs");

    const unsigned char invalid[][6]={{0x24,2,32,0,0,0},{0x24,1,2,0,0,0},
        {0x20,0,4,0,0,0},{0x0a,2,0,0,0,0},{0x3b,0,0,4,0,0},
        {0x65,4,0,0,0,0},{0x30,0,10,0,0,0}};
    for (size_t i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        const Code bad[]={{invalid[i],sizeof(invalid[i])},{end,sizeof(end)}};
        bind(bad,2);
        check(rx_events_start_room()!=NULL,"invalid actor operand reached original handler");
    }
    const unsigned char orphan[]={20,0,0,0,0,0};
    const unsigned char pause[]={0x2b,0,0xff,0};
    const Code missing[]={{orphan,sizeof(orphan)},{end,sizeof(end)},{pause,sizeof(pause)}};
    bind(missing,3);
    check(rx_events_start_room()&&strstr(rx_events_fault()->reason,"valid player/enemy"),
          "unbound task work reached original motion handler");
    ene[7].exp1=(unsigned char *)(uintptr_t)1;
    const Code bad_companion[]={{setup,sizeof(setup)},{end,sizeof(end)}};
    bind(bad_companion,2);
    check(rx_events_start_room()!=NULL,"invalid multipart companion was dereferenced");
    rx_events_reset();
    rom->enep=NULL; rom->ene_n=0;
    rom->walp=rom->etcp=rom->flrp=NULL; rom->wal_n=rom->etc_n=rom->flr_n=0;
    sys->obwp=sys->itwp=NULL; sys->rmthp=NULL;
    puts("PASS original actor visibility, native joint links, task binding, multipart motion and collision flags");
    puts("PASS invalid actor, joint, work, companion and collision references rejected before access");
    puts("PASS original InitSetKage, duplicate suppression and guarded shadow pool exhaustion");
}

void rx_check_face_sampling(const unsigned char *rdt,size_t bytes);
static void check_retail(const char *path)
{
    rx_events_reset();
    free(storage); storage=NULL;
    FILE *file=fopen(path,"rb");
    check(file&&fseek(file,0,SEEK_END)==0,"could not open retail room");
    long length=ftell(file);
    check(length>0&&fseek(file,0,SEEK_SET)==0,"invalid retail room size");
    storage=malloc((size_t)length);
    check(storage&&fread(storage,1,(size_t)length,file)==(size_t)length,"could not read retail room");
    fclose(file);
    RxRoomHeader header={0};
    const char *error=rx_decode_room_header(storage,(size_t)length,&header);
    check(!error,error);
    RxRoomSpan events=header.tables[RX_EVENTS];
    check(!rx_room_scripts_open((unsigned char *)events.data,events.bytes,&scripts),"invalid retail events");
    rom->evtp=&scripts;
    check(events.bytes>730 && events.data[506]==0x69 && events.data[507]==13 && events.data[552]==0x69,
          "retail common-control offsets differ from independent reference");
    unsigned char *room_data=storage;storage=NULL;
    rx_check_face_sampling(room_data,(size_t)length);
    check(events.bytes>1746,"retail face policy controls unavailable");rx_check_face_controls(run_common,events.data+1740,events.data+1742);
    check(events.bytes>1722,"retail deferred music stop unavailable");rx_check_sound_requests(run_common,events.data+1720);
    check(events.bytes>1728,"retail cinematic command unavailable");check_cinematic_commands(events.data+1726);
    check(events.bytes>1626,"retail random selector unavailable");check_random_commands(events.data+1624);
    check(events.bytes>1616,"retail consumption/trigger/cleanup commands unavailable");
    check_item_consumption(events.data+1418);check_trigger_update(events.data+1604);check_case_cleanup(events.data+1614);
    check(events.bytes>1350,"retail equipment request unavailable");check_equipment_change(events.data+1348);
    check(events.bytes>1318,"retail item motion commands unavailable");
    check_item_motion_commands(events.data+1314);
    check(events.bytes>1136,"retail event audio command unavailable");
    rx_check_event_audio(run_common,events.data+1134);
    const unsigned char truncated_stop[]={0x18};
    const Code bad_stop[]={{truncated_stop,sizeof(truncated_stop)},{end,sizeof(end)}};bind(bad_stop,2);Event_T_timer=123;
    check(rx_events_start_room()!=NULL&&rx_events_fault()->opcode==0x18&&Event_T_timer==123,
          "truncated SeOff passed preflight or changed scheduler state");
    static ATR_WORK equipment_walls[25],expected_walls[25];
    ATR_WORK *previous_walls=rom->walp;int previous_wall_count=rom->wal_n,previous_weapon=plp->wpnr_no;
    unsigned previous_status=sys->st_flg;
    check(events.bytes>910&&events.data[884]==1&&events.data[886]==0x26&&events.data[887]==2&&
          events.data[908]==0x81&&events.data[909]==1,"retail equipment/menu condition offsets differ");
    rom->walp=equipment_walls;rom->wal_n=25;
    for(unsigned equipped=0;equipped<2;++equipped) {
        unsigned char probe[24]={0};memcpy(probe,events.data+884,22);
        memset(equipment_walls,0xa5,sizeof(equipment_walls));memcpy(expected_walls,equipment_walls,sizeof(expected_walls));
        plp->wpnr_no=equipped?2:0;
        if(equipped){expected_walls[2].flg|=1;expected_walls[24].flg&=~1u;}
        else {expected_walls[2].flg&=~1u;expected_walls[24].flg|=1;}
        run_common(probe,sizeof(probe));
        check(!memcmp(equipment_walls,expected_walls,sizeof(equipment_walls)),"original equipped-weapon branch changed wrong collision state");
    }
    for(unsigned busy=0;busy<4;++busy) {
        unsigned char probe[]={1,10,0,0,5,1,9,0,0,0,2,8,5,1,10,0,0,0,0,0};
        memcpy(probe+2,events.data+908,2);sys->st_flg=(busy&1?8u:0)|(busy&2?0x40000u:0);
        run_common(probe,sizeof(probe));check(flag(9)==!busy&&flag(10)==!!busy,"original disc menu-busy branch differs");
    }
    rom->walp=previous_walls;rom->wal_n=previous_wall_count;plp->wpnr_no=previous_weapon;sys->st_flg=previous_status;
    puts("PASS original RM_0000 equipped-weapon collision branches and menu-busy condition bytes");
    check(events.bytes>840 && events.data[836]==0x11 && events.data[837]==104 &&
          events.data[838]==0x11 && events.data[839]==55,"retail inventory condition offsets differ");
    unsigned saved_inventory[384];memcpy(saved_inventory,sys->itm,sizeof(saved_inventory));
    char saved_player=sys->ply_id;sys->ply_id=0;
    for (unsigned offset=836;offset<=838;offset+=2) for (unsigned present=0;present<2;++present) {
        unsigned char probe[]={1,10,0,0,5,1,9,0,0,0,2,8,5,1,10,0,0,0,0,0};
        memcpy(probe+2,events.data+offset,2);memset(sys->itm,0,sizeof(sys->itm));
        if (present) sys->itm[11]=((unsigned)events.data[offset+1]<<16)|3;
        run_common(probe,sizeof(probe));
        check(flag(9)==(int)present&&flag(10)==!present,"original disc inventory condition branch differs");
    }
    memcpy(sys->itm,saved_inventory,sizeof(saved_inventory));sys->ply_id=saved_player;
    static O_WRK retail_items[32];static ATR_WORK retail_triggers[256];
    O_WRK *previous_items=sys->itwp;ATR_WORK *previous_triggers=rom->etcp;int previous_trigger_count=rom->etc_n;
    unsigned previous_it[16],previous_ic[16],previous_cb=sys->cb_flg,previous_idx=sys->etc_idx;
    memcpy(previous_it,sys->it_flg,sizeof(previous_it));memcpy(previous_ic,sys->ic_flg,sizeof(previous_ic));
    _Static_assert(sizeof(ATR_WORK)==36,"item trigger disk stride changed");
    unsigned trigger_count=header.counts[RX_TRIGGERS];
    check(trigger_count>8&&trigger_count<=256&&header.tables[RX_TRIGGERS].bytes>=trigger_count*sizeof(ATR_WORK),"retail item triggers unavailable");
    sys->itwp=retail_items;rom->etcp=retail_triggers;rom->etc_n=(int)trigger_count;
    check(events.bytes>1318&&events.data[1314]==0x9c&&events.data[1315]==1&&events.data[1316]==0x9c&&events.data[1317]==2,
          "retail SetItmMotion command offsets differ");
    memset(retail_items,0,sizeof(retail_items));O_WRK saved_items[32];memcpy(saved_items,retail_items,sizeof(saved_items));
    unsigned char item_motion_probe[6]={0};memcpy(item_motion_probe,events.data+1314,4);
    run_common(item_motion_probe,sizeof(item_motion_probe));
    check(!memcmp(saved_items,retail_items,sizeof(saved_items)),"original dormant retail item motion changed room work");
    const unsigned pickup_offsets[]={846,852,858,866,872,878};
    for(unsigned command=0;command<6;++command) for(unsigned taken=0;taken<2;++taken) {
        unsigned offset=pickup_offsets[command];
        unsigned char item_probe[8]={0};memcpy(item_probe,events.data+offset,6);
        check(item_probe[0]==(offset<866?0x23:0x0e),"retail pickup command offsets differ");
        memcpy(retail_triggers,header.tables[RX_TRIGGERS].data,trigger_count*sizeof(ATR_WORK));
        memset(retail_items,0,sizeof(retail_items));memset(sys->it_flg,0,sizeof(sys->it_flg));memset(sys->ic_flg,0,sizeof(sys->ic_flg));
        unsigned trigger=item_probe[4],bit=item_probe[2]|(unsigned)item_probe[3]<<8;
        unsigned item=item_probe[0]==0x23?item_probe[1]:retail_triggers[trigger].prm0;
        check(trigger<trigger_count&&item<32&&bit<512,"retail pickup operand differs");
        sys->cb_flg=item_probe[0]==0x0e&&taken?0x800:0;sys->etc_idx=trigger;
        if(item_probe[0]==0x23&&taken)sys->it_flg[bit/32]=0x80000000u>>(bit%32);
        run_common(item_probe,sizeof(item_probe));
        check(!!(retail_items[item].stflg&0x1000000)==(int)taken,"retail pickup visibility differs");
        check(!!(retail_triggers[trigger].flg&1)==!taken,"retail pickup collision activation differs");
        check(sys->it_flg[bit/32]==(taken?0x80000000u>>(bit%32):0),"retail pickup persistence differs");
    }
    sys->itwp=previous_items;rom->etcp=previous_triggers;rom->etc_n=previous_trigger_count;
    memcpy(sys->it_flg,previous_it,sizeof(previous_it));memcpy(sys->ic_flg,previous_ic,sizeof(previous_ic));sys->cb_flg=previous_cb;sys->etc_idx=previous_idx;
    puts("PASS actual RM_0000 inventory conditions and all six item restoration/result commands against independent pickup state");
    static O_WRK retail_objects[32];O_WRK *previous_objects=sys->obwp;sys->obwp=retail_objects;
    for (unsigned branch=0;branch<2;++branch) {
        unsigned char probe[18]={0x65,2,4,0};
        memcpy(probe+4,events.data+(branch?546:506),12);
        run_common(probe,sizeof(probe));
        check(retail_objects[4].ax==0 && retail_objects[4].ay==(branch?13653:0) && retail_objects[4].az==0,
              "original disc common rotation commands differ from reference");
    }
    unsigned char probe[20]={0x65,2,5,0};
    memcpy(probe+4,events.data+676,6);memcpy(probe+10,events.data+696,8);run_common(probe,sizeof(probe));
    check(retail_objects[5].px==4 && retail_objects[5].py==0 && retail_objects[5].pz==52.5f &&
          retail_objects[5].pxb==4 && retail_objects[5].pzb==52.5f,"original disc common position commands differ from reference");
    static LGT_WORK retail_lights[12];LGT_WORK *previous_lights=rom->lgtp;int previous_count=rom->lgt_n;
    check(header.counts[RX_LIGHTS]==12 && header.lights && events.data[606]==0x35 && events.data[632]==0x35,
          "retail light command references differ");
    rom->lgtp=retail_lights;rom->lgt_n=12;
    for (unsigned branch=0;branch<2;++branch) {
        LGT_WORK expected_lights[12];
        memset(retail_lights,0,sizeof(retail_lights));
        for (unsigned i=0;i<12;++i) memcpy(&retail_lights[i],&header.lights[i].values,sizeof(RxLightValues));
        memcpy(expected_lights,retail_lights,sizeof(retail_lights));
        if (branch) { expected_lights[11].flg&=~1u;expected_lights[5].flg|=1;expected_lights[10].flg|=1; }
        else { expected_lights[5].flg&=~1u;expected_lights[10].flg&=~1u;expected_lights[11].flg|=1; }
        unsigned char light_probe[14]={0};memcpy(light_probe,events.data+(branch?632:606),12);
        run_common(light_probe,sizeof(light_probe));
        check(!memcmp(expected_lights,retail_lights,sizeof(retail_lights)),"original disc LightSet branch differs from independent room-light reference");
    }
    rom->lgtp=previous_lights;rom->lgt_n=previous_count;
    puts("PASS original RM_0000 LightSet bytes from both entry branches against independent native light-bank state");
    rx_events_reset();free(storage);storage=room_data;sys->obwp=previous_objects;
    check(!rx_room_scripts_open((unsigned char *)events.data,events.bytes,&scripts),"restore original retail script table");
    rom->evtp=&scripts;
    puts("PASS original RM_0000 common sign/rotation/position bytes from both entry branches against independent scalar state");
    memset(sys->ky_flg,0xa5,sizeof(sys->ky_flg));
    check(Event_exec(7,38),"could not schedule original opening key-mask event");
    sys->sp_flg|=0x10;
    error=rx_events_run_tasks();
    check(!error,error);
    const uint32_t expected[]={0xfb73cdf4,0x4b7e06ff,0x7b4f7f3f,0xf1e74000,
        0xa5a5a5a5,0xa5a5a5a5,0xa5a5a1a5,0xa5a5a5a5,
        0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,
        0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5,0xa5a5a5a5};
    check(!memcmp(sys->ky_flg,expected,sizeof(expected))&&!bhEtask[7].status&&Event_T_timer==1,
          "original retail key-mask event differs from disc reference");
    RxEventFault fault;
    /* Visibility commands need the real room actor table for preflight.
       This harness is otherwise deliberately independent of actor loading. */
    _Static_assert(sizeof(ETTY_WORK)==sizeof(Ps2RoomSpawn32),"spawn disk layout must be pointer-free");
    rom->ene_n=(int)header.counts[RX_ENEMIES];
    size_t enemy_bytes=(size_t)rom->ene_n*sizeof(*rom->enep);
    check(enemy_bytes&&enemy_bytes<=header.tables[RX_ENEMIES].bytes,"invalid retail enemy records");
    rom->enep=malloc(enemy_bytes);
    check(rom->enep!=NULL,"could not allocate retail enemy records");
    memcpy(rom->enep,header.tables[RX_ENEMIES].data,enemy_bytes);
    check(rx_events_validate_room(&fault)&&fault.opcode==0x2f,"wrong next opening-runtime dependency");
    rx_events_reset();
    free(rom->enep); rom->enep=NULL; rom->ene_n=0;
    rx_room_header_destroy(&header);
    rom->evtp=NULL;
    free(storage); storage=NULL;
    puts("PASS 44 original flag commands from RM_0000 event 38 against independent disc reference");
}

void rx_check_frame_check(void (*)(const unsigned char*,size_t));
void rx_check_player_controls(void (*)(const unsigned char*,size_t));
static void check_native_view_planes(void)
{
    float (*points)[4]=malloc(3*sizeof(*points));check(points&&(uintptr_t)points>UINT32_MAX,"high native plane points");
    for(unsigned i=0;i<128;++i){
        const float input[3][4]={{1.0f+i,2,3,1},{-4,7.0f+i,2,1},{3,-8,11.0f+i,1}};memcpy(points,input,sizeof(input));PS2_PLANE plane;
        Ps2SetPlane(&plane,points[0],points[1],points[2]);check(!memcmp(plane.pos,points[0],sizeof(*points)),"native plane copied a pointer slot instead of its point");
        double a[3],b[3],normal[3];for(unsigned k=0;k<3;++k){a[k]=points[1][k]-points[0][k];b[k]=points[2][k]-points[0][k];}
        for(unsigned k=0;k<3;++k)normal[k]=a[(k+1)%3]*b[(k+2)%3]-a[(k+2)%3]*b[(k+1)%3];double length=sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        for(unsigned k=0;k<3;++k)check(fabs(plane.norm[k]-normal[k]/length)<0.000001,"native view plane normal reference");
    }
    free(points);puts("PASS 128 original view-plane normals and full-width source point copies");
}
int main(int argc,char **argv)
{
    check(argc==3,"supply the expanded RM_0000 fixture and private retail RNG reference");
    check_native_view_planes();
    check_random_generator(argv[2]);
    check_sleep();
    check_loops();
    check_conditions();
    check_state_commands();
    rx_check_frame_check(run_common);
    rx_check_player_controls(run_common);
    check_effect_commands();
    check_faults();
    check_actor_commands();
    check_common_commands();
    check_light_commands();
    check_inventory_commands();
    check_equipment_menu_conditions();
    check_pickup_commands();
    check_retail(argv[1]);
    puts("PASS native original event execution core");
    return 0;
}
