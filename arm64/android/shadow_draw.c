#include "shadow_draw.h"
#include "main.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaFog.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdlib.h>

struct RxShadowDraw {
    GLuint volume, composite, vao, vertices, indices, fbo, mask, depth;
    GLint matrix, projection, tint;
    int width,height;
    GLenum depth_format;
};

static GLuint program(const char *vs,const char *fs)
{
    GLuint shader[2]={glCreateShader(GL_VERTEX_SHADER),glCreateShader(GL_FRAGMENT_SHADER)};
    const char *source[]={vs,fs};
    int ok=1;
    for (unsigned i=0;i<2;++i) {
        GLint status=0;
        glShaderSource(shader[i],1,&source[i],NULL); glCompileShader(shader[i]);
        glGetShaderiv(shader[i],GL_COMPILE_STATUS,&status); ok&=status;
    }
    GLuint result=ok?glCreateProgram():0;
    if (result) {
        GLint status=0;
        glAttachShader(result,shader[0]); glAttachShader(result,shader[1]); glLinkProgram(result);
        glGetProgramiv(result,GL_LINK_STATUS,&status);
        if (!status) { glDeleteProgram(result); result=0; }
    }
    glDeleteShader(shader[0]); glDeleteShader(shader[1]);
    return result;
}

void rx_shadow_close(RxShadowDraw **out)
{
    RxShadowDraw *draw=*out;
    if (!draw) return;
    glDeleteProgram(draw->volume); glDeleteProgram(draw->composite);
    glDeleteVertexArrays(1,&draw->vao);
    glDeleteBuffers(1,&draw->vertices); glDeleteBuffers(1,&draw->indices);
    glDeleteFramebuffers(1,&draw->fbo);
    glDeleteTextures(1,&draw->mask); glDeleteRenderbuffers(1,&draw->depth);
    free(draw); *out=NULL;
}

static RxShadowDraw *open_draw(void)
{
    RxShadowDraw *draw=calloc(1,sizeof(*draw));
    if (!draw) return NULL;
    draw->volume=program(
        "#version 300 es\nlayout(location=0) in vec3 position; uniform mat4 modelView; uniform vec4 projection;"
        "void main(){vec4 p=modelView*vec4(position,1);"
        "gl_Position=vec4(projection.xy*vec2(p.x,-p.y),projection.z*p.z+projection.w,p.z);}",
        "#version 300 es\nprecision highp float;out vec4 color;"
        "void main(){color=vec4(32.0/255.0,0,0,1);}");
    draw->composite=program(
        "#version 300 es\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}",
        "#version 300 es\nprecision highp float;in vec2 uv;uniform highp sampler2D image;"
        "uniform float tint;out vec4 color;void main(){float mask=floor(texture(image,uv).r*255.0+0.5);"
        "if(mask==0.0)discard;color=vec4(vec3(floor(mask*tint/128.0)/255.0),0);}");
    if (!draw->volume || !draw->composite) { rx_shadow_close(&draw); return NULL; }
    draw->matrix=glGetUniformLocation(draw->volume,"modelView");
    draw->projection=glGetUniformLocation(draw->volume,"projection");
    draw->tint=glGetUniformLocation(draw->composite,"tint");
    glGenVertexArrays(1,&draw->vao); glBindVertexArray(draw->vao);
    glGenBuffers(1,&draw->vertices); glGenBuffers(1,&draw->indices);
    glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,draw->indices);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),NULL);
    glEnableVertexAttribArray(0);
    glGenFramebuffers(1,&draw->fbo); glGenTextures(1,&draw->mask);
    glGenRenderbuffers(1,&draw->depth);
    return draw;
}

static int resize(RxShadowDraw *draw,int width,int height,GLenum depth_format)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw->fbo);
    if (draw->width==width && draw->height==height && draw->depth_format==depth_format) return 1;
    glBindTexture(GL_TEXTURE_2D,draw->mask);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,width,height,0,GL_RED,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,draw->mask,0);
    glBindRenderbuffer(GL_RENDERBUFFER,draw->depth);
    glRenderbufferStorage(GL_RENDERBUFFER,depth_format,width,height);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,0);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,draw->depth);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return 0;
    draw->width=width; draw->height=height;
    draw->depth_format=depth_format;
    return 1;
}

const char *rx_shadow_present(RxEngineEffects *effects,RxShadowDraw **out,const NJS_CNK_VIEW *view)
{
    if (!sys->ef_mdfn) return NULL;
    if (sys->ef_mdfn<0 || sys->ef_mdfn>128) return "Invalid original shadow draw queue count";
    if (!cam.mtx) return "Original shadow camera is not initialized";
    GLint viewport[4],target,read_target,depth_bits,stencil_bits;
    glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&target);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_target);
    glGetIntegerv(GL_DEPTH_BITS,&depth_bits); glGetIntegerv(GL_STENCIL_BITS,&stencil_bits);
    GLenum depth_format=depth_bits>24 ? (stencil_bits?GL_DEPTH32F_STENCIL8:GL_DEPTH_COMPONENT32F) :
                                      (stencil_bits?GL_DEPTH24_STENCIL8:GL_DEPTH_COMPONENT24);
    if (!*out) *out=open_draw();
    RxShadowDraw *draw=*out;
    const char *error=NULL;
    if (!draw || !resize(draw,viewport[2],viewport[3],depth_format))
        error="Could not create the original shadow mask target";
    if (!error) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)target);
        glBlitFramebuffer(viewport[0],viewport[1],viewport[0]+viewport[2],viewport[1]+viewport[3],
                          0,0,viewport[2],viewport[3],GL_DEPTH_BUFFER_BIT,GL_NEAREST);
        if (glGetError()!=GL_NO_ERROR) error="Could not copy scene depth for original shadow volumes";
    }
    for (int i=0;!error && i<sys->ef_mdfn;++i) {
        O_WRK *op=sys->ef_mdf[i];
        const RxShadowMesh *mesh=rx_engine_shadow_mesh(effects,op->mdlver);
        if (!mesh) { error="Absent original shadow modifier mesh"; break; }
        if ((op->flg&0x1000000) || (op->stflg&0x1000000) ||
            ((sys->gm_flg&0x4000)&&(op->mdflg&0x40))) continue;
        NJS_MATRIX matrix;
        njPushMatrixEx(); njSetMatrix(NULL,cam.mtx);
        njTranslate(NULL,op->px,op->py,op->pz); njRotateXYZ(NULL,op->ax,op->ay,op->az);
        njScale(NULL,op->sx,op->sy,op->sz); njGetMatrix(&matrix); njPopMatrixEx();
        float minimum[2]={65536,65536},maximum[2]={-65536,-65536};
        int near_crossing=0;
        for (size_t j=0;j<mesh->vertex_count;++j) {
            NJS_POINT3 p;
            njCalcPoint(&matrix,(NJS_POINT3 *)mesh->positions[j],&p);
            if (p.z<1) { near_crossing=1; break; }
            float projected[]={view->distance*view->aspect_x*p.x/p.z,
                               view->distance*view->aspect_y*p.y/p.z};
            for (unsigned k=0;k<2;++k) {
                minimum[k]=fminf(minimum[k],projected[k]); maximum[k]=fmaxf(maximum[k],projected[k]);
            }
        }
        /* Ps2ShadowDraw rejects modifier bounds outside its 1024x1024 work
           region, and suppresses a volume if any vertex lies behind z=1. */
        if (near_crossing || minimum[0]<-512 || minimum[1]<-512 || maximum[0]>=512 || maximum[1]>=512)
            continue;
        NJS_POINT3 last;
        unsigned last_index=mesh->indices[mesh->triangle_count*3-1];
        njCalcPoint(&matrix,(NJS_POINT3 *)mesh->positions[last_index],&last);
        unsigned tint=(unsigned)njCalcFogPower(last.z)*3/16;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw->fbo);
        glViewport(0,0,draw->width,draw->height);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        const GLfloat clear[]={0,0,0,0}; glClearBufferfv(GL_COLOR,0,clear);
        glUseProgram(draw->volume); glBindVertexArray(draw->vao);
        glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,draw->indices);
        glBufferData(GL_ARRAY_BUFFER,mesh->vertex_count*sizeof(*mesh->positions),mesh->positions,GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,mesh->triangle_count*3*sizeof(*mesh->indices),mesh->indices,GL_STREAM_DRAW);
        glUniformMatrix4fv(draw->matrix,1,GL_FALSE,matrix);
        float range=view->far_z-view->near_z;
        glUniform4f(draw->projection,view->distance*view->aspect_x/view->half_width,
                    view->distance*view->aspect_y/view->half_height,
                    (view->far_z+view->near_z)/range,-2*view->far_z*view->near_z/range);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_ONE);
        glEnable(GL_CULL_FACE); glFrontFace(GL_CCW);
        glCullFace(GL_BACK); glBlendEquation(GL_FUNC_ADD);
        glDrawElements(GL_TRIANGLES,(GLsizei)(mesh->triangle_count*3),GL_UNSIGNED_SHORT,NULL);
        glCullFace(GL_FRONT); glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glDrawElements(GL_TRIANGLES,(GLsizei)(mesh->triangle_count*3),GL_UNSIGNED_SHORT,NULL);
        glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)target);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        glUseProgram(draw->composite); glUniform1f(draw->tint,(float)tint);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,draw->mask);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
        glDrawArrays(GL_TRIANGLES,0,3);
        if (glGetError()!=GL_NO_ERROR) error="Original shadow modifier draw failed";
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)target);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,(GLuint)read_target);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE); glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    return error;
}
