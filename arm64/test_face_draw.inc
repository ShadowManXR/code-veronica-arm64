/* Shares the existing Quest GLES sink/shaders in test_texture_gpu.c. */
#include "face_pose.h"
#include "face_sampling.h"
typedef struct FacePacket {
    struct FacePacket *next;
    RxRoomStrip strip;
    NJS_MATRIX matrix;
    unsigned sequence;
    RxRoomVertex vertices[];
} FacePacket;
typedef struct {FacePacket *head,*tail;unsigned count;} FacePackets;
static int save_face_packet(const RxRoomStrip *strip,void *context)
{
    FacePackets *packets=context;FacePacket *p=malloc(sizeof(*p)+strip->count*sizeof(*strip->vertices));if(!p)return 0;
    p->next=NULL;p->strip=*strip;p->strip.model=NULL;p->sequence=packets->count++;memcpy(p->matrix,*strip->matrix,sizeof(NJS_MATRIX));
    memcpy(p->vertices,strip->vertices,strip->count*sizeof(*strip->vertices));p->strip.matrix=&p->matrix;p->strip.vertices=p->vertices;
    if(packets->tail)packets->tail->next=p;else packets->head=p;packets->tail=p;return 1;
}
static int face_packet_order(const void *a,const void *b)
{
    const FacePacket *p=*(FacePacket *const *)a,*q=*(FacePacket *const *)b;
    int pa=(p->strip.attributes&8)!=0,qa=(q->strip.attributes&8)!=0;if(pa!=qa)return pa-qa;
    if(pa&&p->strip.ot_bucket!=q->strip.ot_bucket)return p->strip.ot_bucket<q->strip.ot_bucket?-1:1;
    return p->sequence<q->sequence?-1:p->sequence!=q->sequence;
}
static unsigned face_half(const void *p){unsigned short n;memcpy(&n,p,2);return n;}
static size_t face_polygon_bytes(const RxRoomDrawPart *part)
{
    const unsigned char *begin=(const unsigned char *)part->model->plist,*p=begin;
    const unsigned char *end=(const unsigned char *)part->polygons.data+part->polygons.bytes;
    while(p<end){check((size_t)(end-p)>=2,"bounded reference polygon terminator");if(*p==255)return (size_t)(p-begin)+2;
        check((size_t)(end-p)>=4,"bounded reference polygon header");size_t n=*p==8?4:4+face_half(p+2)*2;
        check(n<=(size_t)(end-p),"bounded reference polygon payload");p+=n;}
    check(0,"reference polygon list has no terminator");return 0;
}
static unsigned char *face_first_index(unsigned char *p,size_t bytes)
{
    unsigned char *end=p+bytes;
    while(end-p>=4){if(*p==66){check(end-p>=10,"reference first index");return p+8;}
        if(*p==255)break;p+=*p==8?4:4+face_half(p+2)*2;}
    check(0,"reference lacks an indexed strip");return NULL;
}
static int reject_face_packet(const RxRoomStrip *strip,void *context)
{(void)strip;++*(unsigned *)context;return 0;}
static void face_portrait(RxFacePose *pose,NJS_MATRIX *base)
{
    const ML_WORK *model=rx_face_pose_model(pose);const float *v=(const float *)model->objP[1].model->vlist+16;
    size_t count;rx_face_pose_weights(pose,&count);float low[3]={v[0],v[1],v[2]},high[3]={v[0],v[1],v[2]};
    for(size_t i=0;i<count;++i)for(unsigned k=0;k<3;++k){low[k]=fminf(low[k],v[i*8+k]);high[k]=fmaxf(high[k],v[i*8+k]);}
    check(high[1]>low[1],"facial portrait height");njUnitMatrix(base);njTranslate(base,0,0,6);float scale=4/(high[1]-low[1]);
    njScale(base,scale,scale,scale);njTranslate(base,-(low[0]+high[0])*0.5f,-(low[1]+high[1])*0.5f,-(low[2]+high[2])*0.5f);
}
static void check_face_draw_gpu(RoomDraw *shared,const NJS_CNK_VIEW *view,const void *rdt,size_t bytes)
{
    enum {WIDTH=320,HEIGHT=240};
    glViewport(0,0,WIDTH,HEIGHT);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glClearColor(0,0,0,1);
    RxEngineRoom *room=NULL;const char *error=rx_engine_room_load(rdt,bytes,&room);check(!error,error);
    NJS_MATRIX before;njGetMatrix(&before);unsigned ambient=(unsigned)rom->amb_rom;
    njFogDisable();glUniform1i(shared->fog_enabled,0);njCnkSetEasyMultiAmbient(0.3f,0.3f,0.3f);
    uint32_t *reference=malloc(WIDTH*HEIGHT*4),*split=malloc(WIDTH*HEIGHT*4),*later=malloc(WIDTH*HEIGHT*4);
    check(reference&&split&&later,"facial GLES readback allocation");
    for(unsigned actor=0;actor<2;++actor){RxFaceMesh *mesh=rx_engine_enemy_face_mesh(room,actor);
        check(!rx_face_mesh_aux_init(mesh),"initialize GPU facial hierarchy");RxEngineModelPart *part=rx_engine_enemy_model(room,actor,0);
        NJS_TEXLIST *textures=ene[actor].mdl[0].texP;check(textures&&textures->nbTexture>0,"original registered actor textures");part->model.texP=textures;
        RxFacePose *pose=NULL;check(!rx_face_pose_create(mesh,part,&pose),"create GPU facial pose");
        NJS_MATRIX unposed;njUnitMatrix(&unposed);RxRoomDrawPart not_ready[31];size_t untouched=99;
        check(rx_face_pose_parts(pose,&unposed,not_ready,&untouched)&&untouched==99,"unevaluated facial model published draw parts");
        MN_WORK *motions=rx_engine_room_motions(room);PARAM_WORK expression={.eye={0,0,-6.2f},.tangz=1};
        check(!rx_face_pose_sample(pose,motions,512,0,0,0,&expression),"sample GPU facial body");
        NJS_MATRIX base;face_portrait(pose,&base);
        RxRoomDrawPart parts[31];size_t count=0;check(!rx_face_pose_parts(pose,&base,parts,&count)&&count==6,"all six original visible facial models");
        RoomDraw draw=*shared;draw.actor=1;draw.textured_slots=0;draw.textures=calloc(textures->nbTexture,sizeof(*draw.textures));check(draw.textures&&njSetTexture(textures),"facial GPU texture selection");
        set_room_projection(&draw,view);FacePackets packets={0};
        /* Independent old contiguous draw calls, then a test-owned global OT. */
        for(size_t i=0;i<count;++i){size_t vb=4+face_half((const char *)parts[i].model->vlist+2)*4+4,pb=face_polygon_bytes(parts+i);
            unsigned char *blob=malloc(vb+pb);check(blob!=NULL,"contiguous model reference allocation");
            memcpy(blob,parts[i].model->vlist,vb);memcpy(blob+vb,parts[i].model->plist,pb);
            NJS_CNK_MODEL contiguous=*parts[i].model;contiguous.vlist=(int *)blob;contiguous.plist=(short *)(blob+vb);
            NJS_CNK_OBJECT object={.evalflags=7,.model=&contiguous};ML_WORK old={.obj_num=1,.objP=&object,.datP=blob,.texP=textures};
            njSetMatrix(NULL,(NJS_MATRIX *)parts[i].matrix);error=rx_draw_room_chunks(&old,vb+pb,1,view,save_face_packet,&packets);check(!error,error);free(blob);
        }
        FacePacket **ordered=malloc(packets.count*sizeof(*ordered));check(ordered&&packets.count,"visible facial reference packets");
        unsigned n=0;for(FacePacket *p=packets.head;p;p=p->next)ordered[n++]=p;
        qsort(ordered,n,sizeof(*ordered),face_packet_order);clear_room_frame(&draw);
        for(unsigned i=0;i<n;++i)check(draw_room_strip(&ordered[i]->strip,&draw),"contiguous facial reference draw");
        glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,reference);unsigned coverage=0;for(unsigned i=0;i<WIDTH*HEIGHT;++i)coverage+=(reference[i]&0xffffff)!=0;
        check(coverage>1000,"original facial GPU reference must be visible");
        njSetMatrix(NULL,&before);clear_room_frame(&draw);error=rx_face_pose_draw(pose,&base,view,draw_room_strip,&draw);check(!error,error);
        glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,split);
        check(draw.strips==packets.count&&!memcmp(reference,split,WIDTH*HEIGHT*4),"split facial packets differ from old contiguous/global-OT GPU reference");
        NJS_MATRIX after;njGetMatrix(&after);check(!memcmp(&before,&after,sizeof(before)),"split facial draw changed caller matrix");
        for(unsigned bad=0;bad<8;++bad){RxRoomDrawPart malformed[31];memcpy(malformed,parts,count*sizeof(*parts));NJS_CNK_MODEL changed=*parts[count-1].model;malformed[count-1].model=&changed;
            unsigned char *copy=NULL;NJS_MATRIX invalid;memcpy(&invalid,parts[count-1].matrix,sizeof(invalid));
            if(bad==0)malformed[count-1].vertices.bytes=63;
            else if(bad==1)malformed[count-1].polygons=(RxDrawSpan){changed.plist,1};
            else if(bad==2)changed.vlist=(int *)(uintptr_t)1;
            else if(bad==3)malformed[count-1].vertices=(RxDrawSpan){(void *)(UINTPTR_MAX-15),64};
            else if(bad==4){invalid[0]=NAN;malformed[count-1].matrix=&invalid;}
            else {size_t pb=face_polygon_bytes(parts+count-1);copy=malloc(pb);check(copy!=NULL,"corrupt polygon allocation");memcpy(copy,changed.plist,pb);
                changed.plist=(short *)copy;malformed[count-1].polygons=(RxDrawSpan){copy,pb};
                if(bad==5){unsigned char *index=face_first_index(copy,pb);index[0]=index[1]=255;}
                else if(bad==6)malformed[count-1].polygons.bytes=pb-1;else copy[0]=254;}
            unsigned submitted=0;check(rx_draw_chunk_parts(malformed,count,textures,1,view,count_room_packets,&submitted)&&!submitted,"bad final split model submitted a partial frame");free(copy);
        }
        unsigned rejected=0;check(rx_face_pose_draw(pose,&base,view,reject_face_packet,&rejected)&&rejected==1,"split sink failure did not abort");
        clear_room_frame(&draw);check(!rx_face_pose_draw(pose,&base,view,draw_room_strip,&draw),"split renderer did not recover after rejected sink");
        glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,split);check(!memcmp(reference,split,WIDTH*HEIGHT*4),"split retry changed exact GPU frame");
        expression.jawang=30;expression.muscle[4]=0.65f;expression.eye.x=1;expression.tangx=20;
        check(!rx_face_pose_sample(pose,motions,512,0,60*65536,0,&expression),"sample animated facial GPU body");
        face_portrait(pose,&base);clear_room_frame(&draw);check(!rx_face_pose_draw(pose,&base,view,draw_room_strip,&draw),"draw animated facial GPU body");
        glReadPixels(0,0,WIDTH,HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,later);unsigned changed_pixels=0,later_coverage=0;for(unsigned i=0;i<WIDTH*HEIGHT;++i){changed_pixels+=reference[i]!=later[i];later_coverage+=(later[i]&0xffffff)!=0;}
        check(changed_pixels>10&&later_coverage>1000&&glGetError()==GL_NO_ERROR,"original facial/body animation did not change GPU frame");
        printf("PASS split facial Quest GPU actor %u: six original models/%u packets/%u visible pixels, byte-exact contiguous reference, global transparent order, %u animated pixels (%u visible); all malformed spans/indices rejected before submission and exact retry\n",actor,packets.count,coverage,changed_pixels,later_coverage);
        while(packets.head){FacePacket *next=packets.head->next;free(packets.head);packets.head=next;}free(ordered);
        for(unsigned i=0;i<textures->nbTexture;++i)glDeleteTextures(1,&draw.textures[i].name);free(draw.textures);rx_face_pose_destroy(pose);
    }
    free(reference);free(split);free(later);rx_engine_room_destroy(room);njSetMatrix(NULL,&before);njSetTexture(rom->mdl.texP);
    njCnkSetEasyMultiAmbient(rom->amb_r[ambient],rom->amb_g[ambient],rom->amb_b[ambient]);
}
