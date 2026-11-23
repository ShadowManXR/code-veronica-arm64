#ifndef _ROOM_H_
#define _ROOM_H_

#include "types.h"

void bhInitRoom();
void bhInitReadRDT();
void bhSetRDT();
void bhSetRoom();
void bhFinishRoom();
void bhPlaceRoomPlayer(void);
#ifdef RECVX_NATIVE_ROOM
#include "room_draw.h"
const char* bhStageNativeRoom(void);
const char* bhCheckNativeCameraInit(void);
const char* bhCheckNativeEventSound(unsigned,unsigned,unsigned);
const char* bhRetainNativeRoomActorWork(void);
const char* bhCheckNativeItemSpeed(unsigned);
const char* bhStepNativeRoomActors(void);
struct RxFacePose;
struct RxFacePose *bhNativeRoomActorPose(size_t);
const char *bhCheckNativePlayerFloor(void);
const char *bhCheckNativePlayerWalkWalls(void);
const char *bhCheckNativePlayerSpawn(void);
size_t bhNativeRoomActorCount(void);
NJS_TEXLIST *bhNativeRoomActorTextures(size_t);
const char *bhPublishNativeRoomActors(void);
const char *bhDrawNativeRoomActor(size_t,const NJS_MATRIX*,const NJS_CNK_VIEW*,RxRoomSubmit,void*);
const char* bhCheckNativeSubControl(unsigned,BH_PWORK*);
const char* bhCheckNativeRoomMotion(const unsigned char*,BH_PWORK*);
const char* bhCheckNativeActorModel(BH_PWORK*);
const char* bhCheckNativeInteractionLight(void);
void bhBindNativeRoomMotions(void);
#endif
void bhSetEneMdl(unsigned char* datp, ETTY_WORK* ep, int mdlno, int eno);
void bhSetEneMtn(unsigned char* datp, BH_PWORK* ep, int id);
void bhSetRoomMtn(unsigned char* datp);
void bhSetObjMdl(unsigned char* datp, ETTY_WORK* ep, int mdlno, int eno);
void bhSetItmMdl(unsigned char* datp, ETTY_WORK* ep, int mdlno, int eno);
void bhSetEffectTable();
void bhSetEffectLink(EF_WRK* efp, int efid);
void bhSetDoorDemo(unsigned int attr, int stg_no, int rom_no, unsigned int pos_no, unsigned int dor_tp);
void bhStartDoorDemo();
void bhPushGameData();
void bhPopGameData();

#endif
