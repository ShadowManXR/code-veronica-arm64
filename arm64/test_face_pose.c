#include "face_pose.h"
#include "face_sampling.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaMath.h"
#include "ps2/vu0.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern NJS_MATRIX *pNaMatMatrixStuckPtr,*pNaMatMatrixStuckTop;
extern int lNaMatMatrixStuckCnt,lNaMatMatrixStuckMax,lNaMatIsUnitMatrix;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL facial pose: %s\n",why);exit(1);}}
typedef double Matrix[16];
static void identity(Matrix m){memset(m,0,sizeof(Matrix));m[0]=m[5]=m[10]=m[15]=1;}
static float sine(int angle){unsigned n=(unsigned)angle&65535u,index=n&16383u;if(n&16384u)index=16383-index;
    float s=sinf(0.0000958738f*index);return n&32768u?-s:s;}
static void product(Matrix a,const Matrix b)
{Matrix r;for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row){double s=0;for(unsigned k=0;k<4;++k)s+=a[k*4+row]*b[c*4+k];r[c*4+row]=s;}memcpy(a,r,sizeof(r));}
static void rotate(Matrix m,unsigned axis,int angle)
{Matrix r;identity(r);unsigned p=(axis+1)%3,q=(axis+2)%3;double s=sine(angle),c=sine(angle+16384);
    r[p*4+p]=r[q*4+q]=c;r[p*4+q]=s;r[q*4+p]=-s;product(m,r);}
static unsigned order_count,parents[31];static const NJS_CNK_OBJECT *order[31];
static void collect(const NJS_CNK_OBJECT *n,unsigned parent)
{while(n){check(order_count<31,"reference tree extent");unsigned i=order_count++;order[i]=n;parents[i]=parent;if(n->child)collect(n->child,i);n=n->sibling;}}
static double max_matrix,max_vertex;static unsigned channels,samples,levels_seen,shifted;
static void compare(RxFacePose *pose,RxFaceMesh *mesh,MN_WORK *bank,unsigned slot,int fixed,int step,const PARAM_WORK *expression)
{
    const ML_WORK *model=rx_face_pose_model(pose);const MASK_WORK *fm=rx_face_mesh_work(mesh);const MN_WORK *motion=bank+slot;
    NJS_CNK_OBJECT expected[31];memcpy(expected,model->objP,sizeof(expected));
    check(!rx_face_mesh_animate(mesh,expression),"prepare facial reference");
    expected[7].ang[0]=fm->toothsrc->ang[0];expected[11].ang[0]=fm->tangorg->ang[0];
    for(unsigned i=0;i<2;++i){expected[9+4*i].ang[0]=-fm->eyesrc[i]->ang[0];expected[9+4*i].ang[1]=-fm->eyesrc[i]->ang[1];}
    unsigned current=(unsigned)fixed/65536,next=current+1<motion->frm_num?current+1:0;
    float fraction=(fixed&65535)*0.000015258789f;
    for(unsigned track=0;track<22;++track){unsigned index=track<6?track:track+9;
        if(motion->md2P[track].p[0]&&((motion->flg&0x30)||!track)){
            const float *key=motion->md2P[track].p[0];for(unsigned k=0;k<3;++k){float value=key[current*3+k];
                if(motion->flg&0x20)value+=fraction*(key[next*3+k]-value);expected[index].pos[k]=value;}}
        const short *angle=motion->md2P[track].p[1];
        if(angle){for(unsigned k=0;k<3;++k){float value=(motion->flg&0x20)?angle[current*3+k]:((const unsigned short *)angle)[current*3+k];
            if(motion->flg&0x20)value+=fraction*(short)(angle[next*3+k]-angle[current*3+k]);expected[index].ang[k]=(int)value;}}
        else {expected[index].ang[0]=0;if(!(motion->flg&0x30))expected[index].ang[1]=expected[index].ang[2]=0;}
    }
    NJS_CNK_OBJECT private_before[31];for(unsigned i=0;i<31;++i)private_before[i]=*order[i];
    unsigned previous_attr=rx_face_pose_actor(pose)->mtn_attr;
    check(!rx_face_pose_sample(pose,bank,512,slot,fixed,step,expression),"native original motion/face/skin frame rejected");
    const BH_PWORK *actor=rx_face_pose_actor(pose);int after=fixed+step,period=(int)motion->frm_num*65536;
    if(after<0)after+=period;if(after>=period)after-=period;
    check(actor->frm_no==after&&actor->mtn_attr==(motion->atrP?motion->atrP[current]:previous_attr),"original motion frame/attribute advancement");
    Matrix full[31],partial[31];
    for(unsigned i=0;i<31;++i){
        if(memcmp(model->objP[i].pos,expected[i].pos,12)||memcmp(model->objP[i].ang,expected[i].ang,12)){
            fprintf(stderr,"slot %u frame %d node %u pos %.9g %.9g %.9g / %.9g %.9g %.9g ang %d %d %d / %d %d %d\n",slot,fixed,i,
                model->objP[i].pos[0],model->objP[i].pos[1],model->objP[i].pos[2],expected[i].pos[0],expected[i].pos[1],expected[i].pos[2],
                model->objP[i].ang[0],model->objP[i].ang[1],model->objP[i].ang[2],expected[i].ang[0],expected[i].ang[1],expected[i].ang[2]);
            check(0,"original 22-track remapping or protected facial pose differs");}
        check(!memcmp(order[i],private_before+i,sizeof(*order[i])),"body motion wrote the private facial bind tree");
        if(parents[i]==UINT_MAX)identity(full[i]);else memcpy(full[i],full[parents[i]],sizeof(Matrix));
        Matrix translation;identity(translation);for(unsigned k=0;k<3;++k)translation[12+k]=expected[i].pos[k];product(full[i],translation);
        rotate(full[i],2,expected[i].ang[2]);rotate(full[i],1,expected[i].ang[1]);rotate(full[i],0,expected[i].ang[0]);
        for(unsigned k=0;k<16;++k){double error=fabs(((const float *)model->owP[i].mtx)[k]-full[i][k]);if(error>max_matrix)max_matrix=error;
            check(error<0.00002,"original bhCalcTree matrix differs from independent hierarchy");}
        memcpy(partial[i],full[i],sizeof(Matrix));rotate(partial[i],0,-expected[i].ang[0]);rotate(partial[i],1,-expected[i].ang[1]);rotate(partial[i],2,-expected[i].ang[2]);
    }
    size_t count;const unsigned *weights=rx_face_pose_weights(pose,&count);const float *rest=(const float *)fm->dst->vlist+16,*got=(const float *)model->objP[1].model->vlist+16;
    check(count==fm->head->nAttr&&got!=rest&&(uintptr_t)got>UINT32_MAX,"native skin output ownership");
    const float levels[]={0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1};
    for(size_t v=0;v<count;++v){unsigned joint=weights[v]>>24,level=(weights[v]>>16)&255;levels_seen|=1u<<level;if(joint>10){joint+=4;shifted++;}
        for(unsigned normal=0;normal<2;++normal)for(unsigned k=0;k<3;++k){const float *p=rest+v*8+normal*4;
            double a=partial[joint][k]*p[0]+partial[joint][4+k]*p[1]+partial[joint][8+k]*p[2]+(normal?0:partial[joint][12+k]);
            double b=full[joint][k]*p[0]+full[joint][4+k]*p[1]+full[joint][8+k]*p[2]+(normal?0:full[joint][12+k]);
            double wanted=level==9?b:a+(b-a)*levels[level],error=fabs(got[v*8+normal*4+k]-wanted);if(error>max_vertex)max_vertex=error;
            check(isfinite(got[v*8+normal*4+k])&&error<0.00002,"native facial skin position/normal differs from independent blend");channels++;}
        check(!memcmp(got+v*8+3,rest+v*8+3,4)&&!memcmp(got+v*8+7,rest+v*8+7,4),"skin output overwrote metadata lanes");}
    size_t bytes=rx_face_mesh_bytes(mesh);unsigned char *snapshot=malloc(bytes);check(snapshot!=NULL,"repeat skin snapshot");memcpy(snapshot,model->objP[1].model->vlist,bytes);
    check(!rx_face_pose_sample(pose,bank,512,slot,fixed,step,expression)&&!memcmp(snapshot,model->objP[1].model->vlist,bytes),"repeated skin frame accumulated deformation");free(snapshot);samples++;
}
void rx_check_face_pose(RxEngineRoom *room,unsigned index)
{
    RxFaceMesh *mesh=rx_engine_enemy_face_mesh(room,index);if(!mesh)return;
    RxEngineModelPart *part=rx_engine_enemy_model(room,index,0);RxFacePose *pose=NULL;
    const char *error=rx_face_pose_create(mesh,part,&pose);check(!error,error?error:"create original flattened facial model");
    const ML_WORK *model=rx_face_pose_model(pose);order_count=0;collect(rx_face_mesh_root(mesh),UINT_MAX);check(order_count==31&&model->obj_num==31,"original facial node count");
    for(unsigned i=0;i<31;++i){NJS_CNK_OBJECT expected=*order[i];if(i==1)expected.model=model->objP[1].model;
        check(!memcmp(model->objP+i,&expected,sizeof(expected))&&model->owP[i].flg==(i>=6&&i<=14?4u:0u),"original flatten copy/face motion-protection flags");}
    size_t vertices;const unsigned *weights=rx_face_pose_weights(pose,&vertices);
    check((uintptr_t)model->objP>UINT32_MAX&&(uintptr_t)model->owP>UINT32_MAX&&vertices==(unsigned)part->skin[3]&&
          !memcmp(weights,part->skin+4,vertices*4),"native facial weights/native work stride");
    NJS_MATRIX *old_ptr=pNaMatMatrixStuckPtr,*old_top=pNaMatMatrixStuckTop;int old_cnt=lNaMatMatrixStuckCnt,old_max=lNaMatMatrixStuckMax,old_unit=lNaMatIsUnitMatrix;VuState old_vu=g_vu0;
    static NJS_MATRIX stack[128];_Make_SinTable();njInitMatrix(stack,128,0);njUnitMatrix(NULL);njTranslate(NULL,2,3,4);NJS_MATRIX old_matrix;memcpy(&old_matrix,stack,sizeof(old_matrix));
    MN_WORK *bank=rx_engine_room_motions(room);MN_WORK before[512];memcpy(before,bank,sizeof(before));
    const unsigned clips[]={0,1,2,5,6,7,8,9};PARAM_WORK expression={.jawang=12,.eye={0.4f,0.2f,-6.2f},.tangx=10,.tangy=-5,.tangz=0.8f};expression.muscle[4]=0.5f;
    for(unsigned clip=0;clip<8;++clip){unsigned slot=clips[clip],last=bank[slot].frm_num-1;
        const int frames[]={0,32768,65536,(int)(last/2)*65536+32768,(int)last*65536,(int)last*65536+32768};
        for(unsigned f=0;f<6;++f)compare(pose,mesh,bank,slot,frames[f],f==0?-65536:(f==4?65536:0),&expression);}
    MN_WORK normal[512];memcpy(normal,bank,sizeof(normal));normal[0].flg=(normal[0].flg&~0x20u)|0x10u;
    compare(pose,mesh,normal,0,32768,0,&expression);
    compare(pose,mesh,normal,0,(int)(normal[0].frm_num-1)*65536+32768,0,&expression);
    check(levels_seen==1023&&shifted&&max_vertex<0.00002,"all original skin weights and four-node offsets must be covered");
    check(!memcmp(before,bank,sizeof(before))&&pNaMatMatrixStuckPtr==stack&&lNaMatMatrixStuckCnt==0&&!memcmp(&old_matrix,stack,sizeof(old_matrix)),"motion bank or caller matrix stack changed");
    size_t bytes=rx_face_mesh_bytes(mesh);unsigned char *saved=malloc(bytes);check(saved!=NULL,"invalid frame snapshot");memcpy(saved,model->objP[1].model->vlist,bytes);
    NJS_CNK_OBJECT objects[31];O_WORK matrices[31];memcpy(objects,model->objP,sizeof(objects));memcpy(matrices,model->owP,sizeof(matrices));BH_PWORK saved_actor=*rx_face_pose_actor(pose);
    const MASK_WORK *fm=rx_face_mesh_work(mesh);MASK_WORK saved_face=*fm;
    unsigned char *facial=malloc(bytes),*tongue=malloc(rx_face_mesh_tongue_bytes(mesh));check(facial&&tongue,"invalid facial source snapshot");
    memcpy(facial,fm->dst->vlist,bytes);memcpy(tongue,fm->tangdst->vlist,rx_face_mesh_tongue_bytes(mesh));
    PARAM_WORK other=expression;other.jawang=33;other.eye.x=-1;other.tangx=30;other.muscle[4]=-0.5f;
    MN_WORK fake=bank[0];NJS_MDATA2_MOD tracks[22];memcpy(tracks,fake.md2P,sizeof(tracks));float bad_keys[6]={NAN};tracks[0].p[0]=bad_keys;fake.md2P=tracks;
    for(unsigned bad=0;bad<9;++bad){unsigned slot=0;int fixed=0,step=0;MN_WORK *motions=bank;size_t slots=512;
        if(bad==0)slot=512;else if(bad==1)slot=3;else if(bad==2)fixed=-1;else if(bad==3)fixed=(int)bank[0].frm_num*65536;else if(bad==4)step=INT_MAX;else motions=NULL;
        if(bad==6){motions=bank;model->owP[30].flg=4;}
        if(bad==7){motions=&fake;slots=1;}
        if(bad==8){motions=bank;lNaMatMatrixStuckMax=lNaMatMatrixStuckCnt;}
        check(rx_face_pose_sample(pose,motions,slots,slot,fixed,step,&other)!=NULL,"unsupported facial motion accepted");
        if(bad==6){check(model->owP[30].flg==4,"rejected motion changed protection flags");model->owP[30].flg=0;}
        if(bad==8)lNaMatMatrixStuckMax=128;
        check(!memcmp(saved,model->objP[1].model->vlist,bytes)&&!memcmp(objects,model->objP,sizeof(objects))&&!memcmp(matrices,model->owP,sizeof(matrices))&&
              !memcmp(&saved_actor,rx_face_pose_actor(pose),sizeof(saved_actor)),"invalid facial motion committed pose state");}
    check(!memcmp(&saved_face,fm,sizeof(saved_face))&&!memcmp(facial,fm->dst->vlist,bytes)&&
          !memcmp(tongue,fm->tangdst->vlist,rx_face_mesh_tongue_bytes(mesh)),"invalid body motion committed facial deformation first");
    for(unsigned bad=0;bad<4;++bad){RxEngineModelPart corrupt=*part;int *skin=malloc(part->skin_bytes);check(skin!=NULL,"bad skin allocation");memcpy(skin,part->skin,part->skin_bytes);corrupt.skin=skin;
        if(bad==0)skin[4]=(skin[4]&0x00ffffff)|(127<<24);else if(bad==1)skin[4]=(skin[4]&~0xff0000)|0xa0000;else if(bad==2)corrupt.skin_bytes-=4;else corrupt.model.obj_num=28;
        RxFacePose *out=NULL;check(rx_face_pose_create(mesh,&corrupt,&out)&&!out,"invalid facial skin committed output");free(skin);}
    printf("PASS original native facial motion/skin actor %u: 31 flattened nodes/22 tracks, eight clips, %u frames/%u position-normal channels, all ten weights/four-node offset; maximum matrix/skin error %.9g/%.9g, protected face joints, exact repeats, unchanged bank/stack and malformed rejection; live actor remains detached\n",index,samples,channels,max_matrix,max_vertex);
    free(facial);free(tongue);free(saved);rx_face_pose_destroy(pose);pNaMatMatrixStuckPtr=old_ptr;pNaMatMatrixStuckTop=old_top;lNaMatMatrixStuckCnt=old_cnt;lNaMatMatrixStuckMax=old_max;lNaMatIsUnitMatrix=old_unit;g_vu0=old_vu;
}
