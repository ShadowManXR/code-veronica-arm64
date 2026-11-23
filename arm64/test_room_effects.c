#include "room_effects.h"
#include "room.h"
#include "event.h"
#include "main.h"
#include "MdlPut.h"
#include "ps2_NaMath.h"
#include "ps2_NaMatrix.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char *bhScePtr;
static void check(int ok,const char *message)
{
    if (!ok) { fprintf(stderr,"FAIL room effects: %s\n",message); exit(1); }
}
static void synthetic(void)
{
    static ROM_WORK room;
    static BH_PWORK player;
    static O_WRK props[32],items[32],saved_pool[512];
    static ML_WORK model;
    static O_WORK bones[3];
    static EF_WRK records[6],original[6];
    short saved_map[256];
    rom=&room; plp=&player; sys->obwp=props;sys->itwp=items;
    model=(ML_WORK){.obj_num=3,.owP=bones};
    for (unsigned i=0;i<3;++i) njUnitMatrix(&bones[i].mtx);
    bones[2].mtx[12]=4; bones[2].mtx[13]=5; bones[2].mtx[14]=6;
    player=(BH_PWORK){.flg=1,.mlwP=&model,.px=10,.py=20,.pz=30};
    ene[0]=(BH_PWORK){.flg=1,.mlwP=&model,.px=-10,.py=2,.pz=4};
    props[6]=(O_WRK){.flg=1,.mlwP=&model,.px=100,.py=3,.pz=200};
    items[5]=(O_WRK){.flg=1,.mlwP=&model,.px=1,.py=2,.pz=3};
    props[0]=(O_WRK){.flg=1,.mlwP=&model,.px=7,.py=8,.pz=9};
    room.eff_n=6;room.effp=records; room.ene_n=1;room.obj_n=8;room.itm_n=6;
    for (unsigned i=0;i<6;++i) records[i]=(EF_WRK){.flg=1,.id=9,.type=1,.sx=1,.sy=2,.sz=3,.param=1234};
    records[0].lx=9;
    records[1].lkflg=1;records[1].lkno=99;records[1].lkono=2;
    records[1].lx=1;records[1].ly=2;records[1].lz=3;
    records[2].lkflg=2;records[2].lkno=99;records[2].lkono=99; records[2].lx=1;records[2].lz=1;
    records[3].lkflg=3;records[3].lkno=6;records[3].lkono=2;
    records[4].lkflg=4;records[4].lkno=5;records[4].lx=0.5f;
    records[5].lkflg=3;records[5].lkno=7;records[5].lkono=2; // Inactive actor: no model required.
    memcpy(original,records,sizeof(records));
    memset(eff,0,sizeof(eff)); eff[0].flg=3;eff[0].id=77;eff[2].flg=2;
    for (unsigned i=0;i<256;++i) sys->efid[i]=-1;
    memcpy(saved_pool,eff,sizeof(eff)); memcpy(saved_map,sys->efid,sizeof(saved_map));
    RxRoomEffects *table=NULL;
    const char *error=rx_room_effects_prepare(&table);
    check(!error,error);
    check(rx_room_effects_count(table)==6&&!memcmp(saved_pool,eff,sizeof(eff))&&
          !memcmp(saved_map,sys->efid,sizeof(saved_map))&&!memcmp(original,records,sizeof(records))&&
          player.mlwP==&model&&!player.mtx&&props[6].mlwP==&model&&!props[6].mtx&&
          bones[2].mtx[12]==4,"preparation changed live work, mapping, records or local joints");
    const O_WRK *work=rx_room_effects_work(table,1);
    const EF_WRK *linked=rx_room_effects_record(table,1);
    check(work&&work->lkwkp==(unsigned char *)&player&&(uintptr_t)work->lkwkp>UINT32_MAX&&
          work->px==0&&work->py==0&&work->pz==0&&work->lkono==2&&
          linked->px==15&&linked->py==27&&linked->pz==39,
          "original player link lost native pointers or world-joint offsets");
    work=rx_room_effects_work(table,2);
    linked=rx_room_effects_record(table,2);
    check(work->lkwkp==(unsigned char *)&ene[0]&&work->lkono==0&&linked->px==-9&&linked->py==2&&linked->pz==5,
          "original enemy/joint fallback-to-zero semantics changed");
    work=rx_room_effects_work(table,3);
    linked=rx_room_effects_record(table,3);
    check(work->lkwkp==(unsigned char *)&props[6]&&linked->px==104&&linked->py==8&&linked->pz==206,
          "original prop joint link differs");
    work=rx_room_effects_work(table,4);
    linked=rx_room_effects_record(table,4);
    check(work->lkwkp==(unsigned char *)&items[5]&&linked->px==1.5f&&linked->py==2&&linked->pz==3,
          "original item root link differs");
    work=rx_room_effects_work(table,5);
    check(!work->lkwkp&&!(work->flg&0x80),"inactive parent link was retained");
    check(rx_room_effects_slot(table,0)==1&&rx_room_effects_slot(table,1)==3&&
          rx_room_effects_slot(table,6)==-1&&!rx_room_effects_work(table,6),"first-fit pool mapping differs");
    RxRoomEffectFault fault;
    check(rx_room_effects_check_live(table,&fault)&&fault.record==0&&fault.id==9&&
          !memcmp(saved_pool,eff,sizeof(eff)),"unported effect was treated as runtime-ready");
    eff[1].flg=1;
    check(rx_room_effects_publish(table)&&sys->efid[0]==-1&&!memcmp(original,records,sizeof(records)),
          "slot race published a partial table");
    memcpy(eff,saved_pool,sizeof(eff)); records[0].param++;
    check(rx_room_effects_publish(table)&&sys->efid[0]==-1,"stale records were published");
    records[0].param--;
    error=rx_room_effects_publish(table); check(!error,error);
    check(eff[0].id==77&&eff[2].flg==2&&records[0].lx==0&&records[1].lkno==0&&records[2].lkno==0&&
          records[2].lkono==0&&records[1].px==15&&records[1].param==1234,"original linker mutations differ");
    for (unsigned i=0;i<6;++i) {
        int slot=sys->efid[i];
        check(slot==rx_room_effects_slot(table,i)&&eff[slot].mtx==(NJS_MATRIX *)eff[slot].mtxbuf&&
              eff[slot].pvp==eff[slot].pv&&eff[slot].tvp==eff[slot].tv,
              "published native effect retained temporary self pointers");
    }
    int selected=sys->efid[3];
    unsigned char command[]={0x43,3,0,0};
    bhScePtr=command; check(bhEffDispSet()==1&&(eff[selected].stflg&0x1000000)&&!eff[0].stflg,
                          "original display command selected wrong native effect");
    command[2]=1; bhScePtr=command;
    check(bhEffDispSet()==1&&!(eff[selected].stflg&0x1000000),"original effect show failed");
    check(rx_room_effects_publish(table)!=NULL,"already-published table accepted twice");
    rx_room_effects_destroy(table); table=NULL;
    check(eff[selected].tvp==eff[selected].tv&&eff[selected].lkwkp==(unsigned char *)&props[6],
          "publication borrowed the destroyed preparation");
    for (unsigned variant=0;variant<7;++variant) {
        memcpy(records,original,sizeof(records));
        switch (variant) {
        case 0: records[0].id=65535; break;
        case 1: records[0].lkflg=5; break;
        case 2: records[0].lkno=-1; break;
        case 3: records[0].sx=NAN; break;
        case 4: records[0].flg=0; break;
        case 5: player.mlwP=NULL; break;
        case 6: player.flg|=0x80; break;
        }
        memcpy(saved_pool,eff,sizeof(eff)); memcpy(saved_map,sys->efid,sizeof(saved_map));
        check(rx_room_effects_prepare(&table)&&!table&&!memcmp(saved_pool,eff,sizeof(eff))&&
              !memcmp(saved_map,sys->efid,sizeof(saved_map)),"bad effect record changed live state");
        player.mlwP=&model; player.flg=1;
    }
    memcpy(records,original,sizeof(records));
    for (unsigned i=0;i<512;++i) eff[i].flg=3;
    check(rx_room_effects_prepare(&table)&&!table,"full pool accepted effect table");
    memset(eff,0,sizeof(eff));memset(ene,0,sizeof(ene));
    rom=NULL;plp=NULL;sys->obwp=sys->itwp=NULL;
    puts("PASS original effect constructors, all actor link selectors, native self pointers and first-fit mapping");
    puts("PASS effect hide/show, immutable preparation, atomic publication, stale/full pool rejection and unsupported-runtime gate");
}

void check_room_effects_file(const char *path)
{
    static NJS_MATRIX matrices[128];
    _Make_SinTable(); njInitMatrix(matrices,128,0);
    synthetic();
    FILE *file=fopen(path,"rb"); check(file&&!fseek(file,0,SEEK_END),"open RDT");
    long bytes=ftell(file); check(bytes>0&&!fseek(file,0,SEEK_SET),"RDT size");
    unsigned char *data=malloc((size_t)bytes);
    check(data&&fread(data,1,(size_t)bytes,file)==(size_t)bytes,"RDT read"); fclose(file);
    static ROM_WORK room;
    static O_WRK props[32],items[32];
    rom=&room;sys->obwp=props;sys->itwp=items;sys->rdtp=data;sys->rdtsz=(unsigned)bytes;
    const char *error=bhStageNativeRoom(); check(!error,error);
    check(rom->eff_n==14&&rom->effp[0].id==115&&rom->effp[3].lkflg==3&&rom->effp[5].lkflg==4,
          "retail effect table differs from disc reference");
    ML_WORK *models[]={props[6].mlwP,items[5].mlwP};
    for (unsigned i=0;i<2;++i) {
        check(models[i]&&models[i]->obj_num,"missing retail linked model");
        models[i]->owP=calloc(models[i]->obj_num,sizeof(O_WORK));
        check(models[i]->owP!=NULL,"native retail joints allocation"); bhCalcTree(NULL,models[i]);
    }
    EF_WRK original[14]; memcpy(original,rom->effp,sizeof(original));
    RxRoomEffects *table=NULL; error=rx_room_effects_prepare(&table); check(!error,error);
    check(rx_room_effects_count(table)==14&&!memcmp(original,rom->effp,sizeof(original)),
          "retail effect preparation changed the RDT");
    for (unsigned i=0;i<14;++i) {
        const O_WRK *work=rx_room_effects_work(table,i);
        check(work&&work->id==original[i].id&&work->type==original[i].type&&work->sx==original[i].sx&&
              work->sy==original[i].sy&&work->sz==original[i].sz&&work->ax==original[i].ax&&
              work->ay==original[i].ay&&work->mlwP==&sys->efm[original[i].id],"retail effect scalar handoff differs");
        if (!original[i].lkflg)
            check(work->px==original[i].px&&work->py==original[i].py&&work->pz==original[i].pz,
                  "unlinked retail effect moved");
    }
    check(rx_room_effects_work(table,3)->lkwkp==(unsigned char *)&props[6]&&
          rx_room_effects_work(table,5)->lkwkp==(unsigned char *)&items[5],"retail actor links differ");
    RxRoomEffectFault fault;
    check(rx_room_effects_check_live(table,&fault)&&fault.record==0&&fault.id==115,
          "wrong next retail effect runtime dependency");
    rx_room_effects_destroy(table);
    for (unsigned i=0;i<2;++i) { free(models[i]->owP);models[i]->owP=NULL; }
    bhInitRoom();free(data);rom=NULL;sys->rdtp=NULL;
    puts("PASS all 14 original RM_0000 effects and native prop/item links; effect 115 blocks live activation");
}
