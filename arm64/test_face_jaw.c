#include "face_mesh.h"
#include "face_sampling.h"
#include "face_bh.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaMath.h"
#include "ps2/vu0.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern NJS_MATRIX *pNaMatMatrixStuckPtr,*pNaMatMatrixStuckTop;
extern int lNaMatMatrixStuckCnt,lNaMatMatrixStuckMax,lNaMatIsUnitMatrix;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL native jaw: %s\n",why);exit(1);}}
typedef double Matrix[16];
static void identity(Matrix m){memset(m,0,sizeof(Matrix));m[0]=m[5]=m[10]=m[15]=1;}
static float sine(int angle){unsigned n=(unsigned)angle&65535u;unsigned index=n&16383u;if(n&16384u)index=16383-index;
    float s=sinf(0.0000958738f*index);return n&32768u?-s:s;}
static void product(Matrix a,const Matrix b)
{Matrix r;for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row){double sum=0;for(unsigned k=0;k<4;++k)sum+=a[k*4+row]*b[c*4+k];r[c*4+row]=sum;}memcpy(a,r,sizeof(r));}
static void rotate(Matrix m,unsigned axis,int angle)
{Matrix r;identity(r);unsigned p=(axis+1)%3,q=(axis+2)%3;double s=sine(angle),c=sine(angle+16384);
    r[p*4+p]=r[q*4+q]=c;r[p*4+q]=s;r[q*4+p]=-s;product(m,r);}
static void translate(Matrix m,double x,double y,double z)
{Matrix r;identity(r);r[12]=x;r[13]=y;r[14]=z;product(m,r);}
static double max_matrix,max_vertex;
static unsigned channels,full_weight,partial_weight;
static void reference_axes(const MASK_WORK *fm,Matrix in,Matrix out)
{
    const float *v=(const float *)fm->src->vlist+16,*a=v+fm->head->jawId[0]*8,*b=v+fm->head->jawId[1]*8;
    float x=b[0]-a[0],y=b[1]-a[1],z=b[2]-a[2];
    int ry=(int)((65536.0f/(3.141592f*2))*atanf(y/x));
    int rz=(int)((65536.0f/(3.141592f*2))*atanf(z*(1.0f/sqrtf(x*x+y*y))));
    identity(in);rotate(in,1,-rz);rotate(in,2,-ry);translate(in,-a[0],-a[1],-a[2]);
    identity(out);translate(out,a[0],a[1],a[2]);rotate(out,2,ry);rotate(out,1,rz);
}
static void compare(RxFaceMesh *mesh,const PARAM_WORK *param,const Matrix in,const Matrix out)
{
    const MASK_WORK *fm=rx_face_mesh_work(mesh);size_t bytes=rx_face_mesh_bytes(mesh);
    check(!rx_face_mesh_muscles(mesh,param),"prepare original muscle pose");
    unsigned char *expected=malloc(bytes);check(expected!=NULL,"reference allocation");memcpy(expected,fm->dst->vlist,bytes);
    float *v=(float *)expected+16;
    for(int i=0;i<fm->jnum;++i){const TANG_WORK *jaw=fm->jaw+i;float *p=v+jaw->id*8;Matrix m;memcpy(m,out,sizeof(m));
        float weighted=-param->jawang*jaw->rate;
        rotate(m,0,(int)(182.04445f*weighted)&65535);translate(m,param->jawtrans,0,0);product(m,in);
        double original[3]={p[0],p[1],p[2]};
        for(unsigned j=0;j<3;++j)p[j]=(float)(m[j]*original[0]+m[4+j]*original[1]+m[8+j]*original[2]+m[12+j]);
        full_weight+=jaw->rate==1;partial_weight+=jaw->rate!=1;
    }
    PARAM_WORK saved=*param;
    check(!rx_face_mesh_deform(mesh,param)&&!memcmp(param,&saved,sizeof(saved)),"muscle/jaw chain failed or changed sample");
    const float *got=(const float *)fm->dst->vlist+16;
    check(!memcmp(expected,fm->dst->vlist,64)&&!memcmp(expected+bytes-4,(const char *)fm->dst->vlist+bytes-4,4),"jaw changed packet framing");
    for(unsigned i=0;i<fm->head->nAttr;++i){for(unsigned j=0;j<3;++j){double error=fabs((double)got[i*8+j]-v[i*8+j]);
        if(error>max_vertex)max_vertex=error;
        if(!isfinite(got[i*8+j])||error>0.00001){fprintf(stderr,"vertex %u axis %u got %.9g expected %.9g error %.9g\n",i,j,got[i*8+j],v[i*8+j],error);check(0,"jaw position differs from independent matrix reference");}channels++;}
        check(!memcmp(got+i*8+3,v+i*8+3,20),"jaw overwrote skin metadata/normal/weight lanes");}
    unsigned char *once=malloc(bytes);check(once!=NULL,"repeat snapshot");memcpy(once,fm->dst->vlist,bytes);
    check(!rx_face_mesh_deform(mesh,param)&&!memcmp(once,fm->dst->vlist,bytes),"repeated jaw update accumulated deformation");
    free(once);free(expected);
}
void rx_check_face_jaw(RxFaceMesh *mesh,unsigned actor,const void *mouth,size_t mouth_bytes)
{
    const MASK_WORK *fm=rx_face_mesh_work(mesh);size_t bytes=rx_face_mesh_bytes(mesh);PARAM_WORK param={0};
    unsigned char *before=malloc(bytes);check(before!=NULL,"jaw snapshot");memcpy(before,fm->dst->vlist,bytes);
    check(rx_face_mesh_deform(mesh,&param)&&!memcmp(before,fm->dst->vlist,bytes),"uninitialized jaw work executed");
    NJS_MATRIX *old_ptr=pNaMatMatrixStuckPtr,*old_top=pNaMatMatrixStuckTop;
    int old_cnt=lNaMatMatrixStuckCnt,old_max=lNaMatMatrixStuckMax,old_unit=lNaMatIsUnitMatrix;VuState old_vu=g_vu0;
    static NJS_MATRIX stack[8];_Make_SinTable();njInitMatrix(stack,8,0);njUnitMatrix(NULL);njTranslate(NULL,7,8,9);
    _Alignas(16) unsigned overlap[20],expected_copy[20];
    for(unsigned i=0;i<20;++i)overlap[i]=expected_copy[i]=i*0x1020101u;
    memmove(expected_copy+4,expected_copy,64);
    njCopyMatrix((NJS_MATRIX *)(overlap+4),(NJS_MATRIX *)overlap);
    check((uintptr_t)overlap>UINT32_MAX&&!memcmp(overlap,expected_copy,sizeof(overlap)),"native high-address overlapping matrix copy");
    NJS_MATRIX saved;memcpy(&saved,stack,sizeof(saved));
    g_vu0.vf[0].f[3]=0;check(rx_face_mesh_jaw_init(mesh)!=NULL,"uninitialized VU accepted");g_vu0.vf[0].f[3]=1;
    check(!rx_face_mesh_jaw_init(mesh),"original pivot matrix setup");
    /* Degenerate authored pivots must fail before original atan/rsqrt setup. */
    size_t mask_bytes=64+(size_t)fm->head->nList*4;
    for(int i=0;i<fm->lnum;++i)mask_bytes+=(unsigned)fm->vlist[i].nvnum*4;
    mask_bytes+=(size_t)fm->head->nConnect*20+(size_t)fm->head->nJaw*8+(size_t)fm->head->nTang*8+(size_t)fm->head->nFace*164;
    unsigned char *bad_mask=malloc(mask_bytes);check(bad_mask!=NULL,"invalid pivot MASK allocation");memcpy(bad_mask,fm->head,mask_bytes);
    memcpy(bad_mask+10,bad_mask+8,2);RxFaceMesh *invalid=NULL;
    RxChunk source={.vertices=(const unsigned char *)fm->src->vlist,.vertex_extent=bytes};
    check(!rx_face_mesh_create(bad_mask,mask_bytes,&source,&invalid),"stage scalar degenerate pivot fixture");
    MASK_WORK old_work=*rx_face_mesh_work(invalid);
    check(rx_face_mesh_jaw_init(invalid)&&!memcmp(&old_work,rx_face_mesh_work(invalid),sizeof(old_work)),"degenerate pivot setup committed work");
    rx_face_mesh_destroy(invalid);free(bad_mask);
    check(pNaMatMatrixStuckPtr==stack&&lNaMatMatrixStuckCnt==0&&!memcmp(&saved,stack,sizeof(saved)),"pivot setup changed caller matrix stack");
    Matrix in,out;reference_axes(fm,in,out);
    for(unsigned i=0;i<16;++i){double a=fabs(((const float *)&fm->jmat1)[i]-in[i]),b=fabs(((const float *)&fm->jmat2)[i]-out[i]);
        if(a>max_matrix)max_matrix=a;if(b>max_matrix)max_matrix=b;check(a<0.000001&&b<0.000001,"original pivot matrices differ");}
    const float axes[][3]={{0.25f,0.5f,-0.3f},{-0.25f,0.5f,0.3f},{0.25f,-0.5f,0.3f},{-0.25f,-0.5f,-0.3f},{0,0.5f,0.3f}};
    for(unsigned axis=0;axis<5;++axis){
        unsigned char *vertices=malloc(bytes);check(vertices!=NULL,"rotated pivot vertex allocation");memcpy(vertices,fm->src->vlist,bytes);
        float *a=(float *)vertices+16+fm->head->jawId[0]*8,*b=(float *)vertices+16+fm->head->jawId[1]*8;
        for(unsigned k=0;k<3;++k)b[k]=a[k]+axes[axis][k];
        RxChunk rotated={.vertices=vertices,.vertex_extent=bytes};RxFaceMesh *sample=NULL;
        check(!rx_face_mesh_create(fm->head,mask_bytes,&rotated,&sample)&&!rx_face_mesh_jaw_init(sample),"rotated pivot setup");
        const MASK_WORK *w=rx_face_mesh_work(sample);Matrix ri,ro;reference_axes(w,ri,ro);
        for(unsigned k=0;k<16;++k){double e=fabs(((const float *)&w->jmat1)[k]-ri[k]),f=fabs(((const float *)&w->jmat2)[k]-ro[k]);
            if(e>max_matrix)max_matrix=e;if(f>max_matrix)max_matrix=f;check(e<0.000001&&f<0.000001,"rotated pivot matrix differs");}
        PARAM_WORK pose={.jawang=23.5f,.jawtrans=0.025f};pose.muscle[4]=-0.25f;pose.muscle[9]=0.5f;
        compare(sample,&pose,ri,ro);rx_face_mesh_destroy(sample);free(vertices);
    }
    const float angles[]={-35,-12,-1,0,1,12,35},translations[]={-0.05f,0,0.05f};
    for(unsigned a=0;a<7;++a)for(unsigned t=0;t<3;++t){param.jawang=angles[a];param.jawtrans=translations[t];
        for(unsigned i=0;i<32;++i)param.muscle[i]=((int)(i%7)-3)*0.125f;compare(mesh,&param,in,out);}
    for(unsigned i=0;i<20;++i){RxFaceClip clip;check(!rx_face_clip(mouth,mouth_bytes,i,&clip),"jaw expression view");
        for(unsigned j=0;j<3;++j){check(!rx_face_sample(&clip,actor,clip.keys[clip.count-1].frame*j*0.5f,&param),"jaw expression sample");compare(mesh,&param,in,out);}}
    /* Actual phoneme parameters use integer angle bits. Sample them through
       the original lip converter before passing them to the jaw evaluator. */
    unsigned char lip_data[]={0,4,1,4,2,4,3,4,4,4,5,4,255};RxLipClip lip={lip_data,sizeof(lip_data)};
    for(int group=0;group+6<=fm->fnum;group+=6){LIP_WORK state;check(!rx_lip_begin(&lip,&state),"jaw phoneme sequence");
        memset(&param,0,sizeof(param));for(unsigned tick=0;state.flag&&tick<100;++tick){
            check(!rx_lip_sample(&lip,fm->face+group,&state,&param),"jaw original phoneme conversion");compare(mesh,&param,in,out);}
        check(!state.flag,"jaw phoneme sequence did not end");}
    memcpy(before,fm->dst->vlist,bytes);PARAM_WORK previous=fm->param;
    const float bad[]={NAN,INFINITY,-INFINITY,FLT_MAX,-FLT_MAX};
    for(unsigned i=0;i<5;++i){memset(&param,0,sizeof(param));param.jawang=bad[i];
        check(rx_face_mesh_deform(mesh,&param)&&!memcmp(before,fm->dst->vlist,bytes)&&!memcmp(&previous,&fm->param,sizeof(previous)),"invalid jaw angle committed");}
    memset(&param,0,sizeof(param));param.jawtrans=NAN;
    check(rx_face_mesh_deform(mesh,&param)&&!memcmp(before,fm->dst->vlist,bytes),"nonfinite jaw translation committed");
    check(full_weight&&partial_weight,"both original weighted jaw branches must execute");
    printf("PASS original native jaw actor %u: pivot matrices, %u full/%u partial-weight transforms, %u position channels, maximum matrix/vertex error %.9g/%.9g, expression/lip chain, repeat stability, metadata and guarded samples; actor context remains detached\n",actor,full_weight,partial_weight,channels,max_matrix,max_vertex);
    check(!rx_face_mesh_muscles(mesh,&(PARAM_WORK){0}),"restore muscle bind pose");
    pNaMatMatrixStuckPtr=old_ptr;pNaMatMatrixStuckTop=old_top;lNaMatMatrixStuckCnt=old_cnt;lNaMatMatrixStuckMax=old_max;lNaMatIsUnitMatrix=old_unit;g_vu0=old_vu;free(before);
}
