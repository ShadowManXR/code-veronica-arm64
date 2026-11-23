#include "particle_draw.h"
#include "effect_runtime.h"
#include "effect_callbacks.h"
#include "message_runtime.h"
#include "fade_runtime.h"
#include "main.h"
#include "gs/gs.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaFog.h"
#include "ps2_NaDraw2D.h"
#include "ps2_NaTextureFunction.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float position[3],uv[2],rgba[4],fog; } ParticleVertex;
struct RxParticleDraw {
    GLuint program,vao,vertices,texture;
    GLint matrix,projection,fog_color,texture_alpha,screen_space,solid_color;
    uintptr_t texture_identity;
    uint64_t tex0,texa,palette_generation;
};

void rx_particle_close(RxParticleDraw **out)
{
    RxParticleDraw *draw=*out;
    if (!draw) return;
    glDeleteProgram(draw->program);glDeleteVertexArrays(1,&draw->vao);
    glDeleteBuffers(1,&draw->vertices);glDeleteTextures(1,&draw->texture);
    free(draw);*out=NULL;
}

static RxParticleDraw *open_draw(void)
{
    const char *sources[]={
        "#version 300 es\nlayout(location=0) in vec3 position;layout(location=1) in vec2 coords;"
        "layout(location=2) in vec4 rgba;layout(location=3) in float fog;"
        "uniform mat4 modelView;uniform vec4 projection;uniform bool screenSpace;out vec2 uv;out vec4 shade;out float visibility;"
        "void main(){if(screenSpace){gl_Position=vec4(position.x/320.0-1.0,1.0-position.y/240.0,position.z,1.0);}"
        "else{vec4 p=modelView*vec4(position,1);"
        "gl_Position=vec4(projection.xy*vec2(p.x,-p.y),projection.z*p.z+projection.w,p.z);"
        "}uv=coords;shade=rgba/128.0;visibility=fog;}",
        "#version 300 es\nprecision highp float;uniform highp sampler2D image;uniform vec3 fogColor;"
        "uniform bool textureAlpha;uniform bool solidColor;in vec2 uv;in vec4 shade;in float visibility;out vec4 color;"
        "void main(){vec4 t=solidColor?vec4(1):texture(image,uv);float a=solidColor?floor(shade.a*128.0+0.0001):floor((textureAlpha?t.a*255.0:128.0)*shade.a+0.0001);"
        "if(a<=0.0)discard;vec3 rgb=floor((solidColor?vec3(128):t.rgb*255.0)*shade.rgb+0.0001);"
        "rgb=floor(mix(fogColor,rgb,visibility/255.0)+0.0001);color=vec4(rgb/255.0,a/128.0);}"};
    RxParticleDraw *draw=calloc(1,sizeof(*draw));
    if (!draw) return NULL;
    GLuint shaders[]={glCreateShader(GL_VERTEX_SHADER),glCreateShader(GL_FRAGMENT_SHADER)};
    GLint linked=0;
    int ok=1;
    for (unsigned i=0;i<2;++i) {
        GLint compiled=0;
        glShaderSource(shaders[i],1,&sources[i],NULL);glCompileShader(shaders[i]);
        glGetShaderiv(shaders[i],GL_COMPILE_STATUS,&compiled);ok&=compiled;
    }
    if (ok) {
        draw->program=glCreateProgram();glAttachShader(draw->program,shaders[0]);
        glAttachShader(draw->program,shaders[1]);glLinkProgram(draw->program);
        glGetProgramiv(draw->program,GL_LINK_STATUS,&linked);
    }
    glDeleteShader(shaders[0]);glDeleteShader(shaders[1]);
    if (!linked) { rx_particle_close(&draw);return NULL; }
    draw->solid_color=glGetUniformLocation(draw->program,"solidColor");
    draw->screen_space=glGetUniformLocation(draw->program,"screenSpace");
    draw->matrix=glGetUniformLocation(draw->program,"modelView");
    draw->projection=glGetUniformLocation(draw->program,"projection");
    draw->fog_color=glGetUniformLocation(draw->program,"fogColor");
    draw->texture_alpha=glGetUniformLocation(draw->program,"textureAlpha");
    glGenVertexArrays(1,&draw->vao);glBindVertexArray(draw->vao);
    glGenBuffers(1,&draw->vertices);glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    const unsigned counts[]={3,2,4,1};const size_t offsets[]={0,offsetof(ParticleVertex,uv),
        offsetof(ParticleVertex,rgba),offsetof(ParticleVertex,fog)};
    for (unsigned i=0;i<4;++i) {
        glVertexAttribPointer(i,(GLint)counts[i],GL_FLOAT,GL_FALSE,sizeof(ParticleVertex),(void *)offsets[i]);
        glEnableVertexAttribArray(i);
    }
    glGenTextures(1,&draw->texture);
    return draw;
}

static const char *select_texture(RxParticleDraw *draw,unsigned id,unsigned frame)
{
    const char *palette_error=rx_effects_sync_palette();
    if (palette_error) return palette_error;
    if (!njSetTexture(&sys->ef_tlist) || njSetTextureNum((sys->ef_tn[id]+frame))<0)
        return "Could not select the original effect atlas";
    uint64_t tex0=gs.ctx[0].tex0;
    uintptr_t identity=sys->ef_tlist.textures[(sys->ef_tn[id]+frame)].texaddr;
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,draw->texture);
    if (!draw->texture_identity || draw->texture_identity!=identity || draw->tex0!=tex0 || draw->texa!=gs.texa ||
        draw->palette_generation!=rx_effects_palette_generation()) {
        unsigned width=1u<<((tex0>>26)&15),height=1u<<((tex0>>30)&15);
        if (width>1024 || height>1024) return "Original particle atlas dimensions exceed native bounds";
        uint32_t *pixels=malloc((size_t)width*height*sizeof(*pixels));
        if (!pixels) return "Cannot allocate the original particle atlas upload";
        gs_decode_texture(tex0,gs.texa,width,height,pixels);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        free(pixels);
        draw->texture_identity=identity;draw->tex0=tex0;draw->texa=gs.texa;
        draw->palette_generation=rx_effects_palette_generation();
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,gs.ctx[0].clamp&3?GL_CLAMP_TO_EDGE:GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,(gs.ctx[0].clamp>>2)&3?GL_CLAMP_TO_EDGE:GL_REPEAT);
    glUniform1i(draw->texture_alpha,(tex0>>34)&1);
    return NULL;
}

const char *rx_particle_present(RxParticleDraw **out,const NJS_CNK_VIEW *view)
{
    if (!sys->ef_trsn) return NULL;
    if (sys->ef_trsn<0 || (unsigned)sys->ef_trsn>sizeof(sys->ef_trs)/sizeof(*sys->ef_trs) || !cam.mtx)
        return "Invalid original particle draw context";
    if (!*out) *out=open_draw();
    RxParticleDraw *draw=*out;
    if (!draw) return "Could not create the original particle renderer";
    glUseProgram(draw->program);glBindVertexArray(draw->vao);glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    glUniform1i(draw->screen_space,0);glUniform1i(draw->solid_color,0);
    float range=view->far_z-view->near_z;
    glUniform4f(draw->projection,view->distance*view->aspect_x/view->half_width,
                view->distance*view->aspect_y/view->half_height,(view->far_z+view->near_z)/range,
                -2*view->far_z*view->near_z/range);
    glUniform3f(draw->fog_color,gs.fogcol&255,(gs.fogcol>>8)&255,(gs.fogcol>>16)&255);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    const char *error=NULL;
    for (int i=0;!error && i<sys->ef_trsn;++i) {
        O_WRK *op=sys->ef_trs[i];
        uintptr_t address=(uintptr_t)op,start=(uintptr_t)eff;
        if (address<start || address-start>=sizeof(eff) || (address-start)%sizeof(*eff)) {
            error="Particle queue references absent native effect work";break;
        }
        error=rx_effect_validate(op);if (error) break;
        if ((op->flg&0x1000000) || (op->stflg&0x1000000) ||
            ((sys->gm_flg&0x4000)&&(op->mdflg&0x40))) continue;
        unsigned atlas=op->id==9?8:op->id==126?35:33;
        int frame=op->id==126?(op->type<2?0:op->type<4?1:2):0;
        if ((op->id!=124 && op->id!=9 && op->id!=126) || op->pn!=4 || op->tvp!=op->tv ||
            op->tex_id!=(int)atlas || op->ani_ct!=frame || op->bl_src!=8 ||
            op->bl_dst!=(op->id==124?10:6)) {
            error="Original particle draw variant is not ported yet";break;
        }
        NJS_MATRIX matrix;
        njPushMatrixEx();njSetMatrix(NULL,cam.mtx);njTranslate(NULL,op->px,op->py,op->pz);
        njUnitRotPortion(NULL);njRotateZ(NULL,op->az);njScale(NULL,op->sx,op->sy,op->sz);
        njGetMatrix(&matrix);njPopMatrixEx();
        ParticleVertex vertices[4];int behind=0;
        for (unsigned j=0;j<4;++j) {
            const NJS_TEXTURE_VTX *v=&op->tv[j];NJS_POINT3 p;
            njCalcPoint(&matrix,(NJS_POINT3 *)&v->x,&p);
            if (p.z<=0) behind=1;
            vertices[j]=(ParticleVertex){.position={v->x,v->y,v->z},.uv={v->u,v->v},
                .rgba={((v->col>>16&255)+1)>>1,((v->col>>8&255)+1)>>1,
                       ((v->col&255)+1)>>1,((v->col>>24&255)+1)>>1},.fog=floorf(njCalcFogPower(p.z))};
        }
        if (behind) continue;
        glBlendFunc(GL_SRC_ALPHA,op->bl_dst==6?GL_ONE_MINUS_SRC_ALPHA:GL_ONE);
        error=select_texture(draw,(unsigned)op->tex_id,(unsigned)op->ani_ct);if (error) break;
        glUniformMatrix4fv(draw->matrix,1,GL_FALSE,matrix);
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        if (glGetError()!=GL_NO_ERROR) error="Original particle billboard draw failed";
    }
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glBindVertexArray(0);
    return error;
}

/* Screen vertices use the original Ninja 2D origin and 12.4 GS coordinates.
   The original ordering table uses the final vertex depth and appends ties. */
const char *rx_overlay_present(RxParticleDraw **out,const NJS_CNK_VIEW *view)
{
    if (!sys->ef_trs2dn) return NULL;
    if (sys->ef_trs2dn<0 || sys->ef_trs2dn>128 || !view ||
        !isfinite(view->near_z) || !isfinite(view->far_z) || view->near_z<=0 || view->far_z<=view->near_z)
        return "Invalid original screen-overlay draw context";
    struct { O_WRK *work;ParticleVertex vertices[4];unsigned bucket; } entries[128];
    unsigned count=0;float a,b;Ps2DepthCoefficients(-view->near_z,-view->far_z,&a,&b);
    if (!isfinite(a) || !isfinite(b)) return "Invalid original overlay depth coefficients";
    for (int i=0;i<sys->ef_trs2dn;++i) {
        O_WRK *op=sys->ef_trs2d[i];uintptr_t address=(uintptr_t)op,start=(uintptr_t)eff;
        if (address<start || address-start>=sizeof(eff) || (address-start)%sizeof(*eff))
            return "Screen-overlay queue references absent native effect work";
        const char *error=rx_effect_validate(op);if (error) return error;
        if ((op->flg&0x1000000) || (op->stflg&0x1000000) ||
            ((sys->gm_flg&0x4000)&&(op->mdflg&0x40))) continue;
        if ((op->id!=20 && op->id!=92) || op->pn!=4 || op->tvp!=op->tv || op->bl_src!=8 || op->bl_dst!=6)
            return "Original screen-overlay draw variant is not ported yet";
        entries[count].work=op;
        for (unsigned j=0;j<4;++j) {
            NJS_TEXTURE_VTX *v=&op->tv[j];
            if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->z) ||
                v->x < -1728 || v->x>2367 || v->y < -1808 || v->y>2287)
                return "Original screen-overlay vertex exceeds native GS bounds";
            if (v->z<1) v->z=1; // Original njDrawTextureSub writes this clamp back.
            float z=-1.0f/v->z,depth=fminf(fmaxf(a+b/z,0),65534);
            entries[count].bucket=(unsigned)depth>>4;
            entries[count].vertices[j]=(ParticleVertex){
                .position={truncf((1728+v->x)*16)/16-1728,truncf((1808+v->y)*16)/16-1808,
                           1-2*(truncf(depth*16)/16)/65534},.uv={v->u,v->v},
                .rgba={((v->col>>16&255)+1)>>1,((v->col>>8&255)+1)>>1,
                       ((v->col&255)+1)>>1,((v->col>>24&255)+1)>>1},.fog=255};
        }
        ++count;
    }
    /* Stable insertion sort preserves the original order for equal buckets. */
    for (unsigned i=1;i<count;++i) for (unsigned j=i;j && entries[j-1].bucket>entries[j].bucket;--j) {
        unsigned char saved[sizeof(entries[0])];
        memcpy(saved,&entries[j],sizeof(saved));entries[j]=entries[j-1];memcpy(&entries[j-1],saved,sizeof(saved));
    }
    if (!count) return NULL;
    if (!*out) *out=open_draw();RxParticleDraw *draw=*out;
    if (!draw) return "Could not create the original screen-overlay renderer";
    glUseProgram(draw->program);glUniform1i(draw->screen_space,1);glUniform1i(draw->solid_color,0);
    glUniform3f(draw->fog_color,0,0,0);glBindVertexArray(draw->vao);glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    const char *error=NULL;
    for (unsigned i=0;!error && i<count;++i) {
        O_WRK *op=entries[i].work;
        error=select_texture(draw,op->id==92?92:45,(unsigned)op->ani_ct);if (error) break;
        GLint filter=op->flg&0x20000000?GL_NEAREST:GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
        glBufferData(GL_ARRAY_BUFFER,sizeof(entries[i].vertices),entries[i].vertices,GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        if (glGetError()!=GL_NO_ERROR) error="Original screen-overlay draw failed";
    }
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glBindVertexArray(0);
    return error;
}

const char *rx_callback_present(RxParticleDraw **out,const NJS_CNK_VIEW *view)
{
    RxEffectCallbacks batch={0};const char *error=rx_effect_callbacks_collect(&batch);
    if (error) return error;
    if (!batch.count) { rx_effect_callbacks_destroy(&batch);return NULL; }
    float a,b;
    if (!view || !isfinite(view->near_z) || !isfinite(view->far_z) ||
        view->near_z<=0 || view->far_z<=view->near_z) {
        rx_effect_callbacks_destroy(&batch);return "Invalid original cinema draw context";
    }
    Ps2DepthCoefficients(-view->near_z,-view->far_z,&a,&b);
    if (!isfinite(a) || !isfinite(b)) {
        rx_effect_callbacks_destroy(&batch);return "Invalid original cinema depth coefficients";
    }
    if (!*out) *out=open_draw();RxParticleDraw *draw=*out;
    if (!draw) { rx_effect_callbacks_destroy(&batch);return "Could not create the original cinema renderer"; }
    glUseProgram(draw->program);glUniform1i(draw->screen_space,1);glUniform1i(draw->solid_color,1);
    glUniform3f(draw->fog_color,0,0,0);glBindVertexArray(draw->vao);glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,draw->texture);
    if (!draw->texture_identity) {
        const uint32_t white=UINT32_MAX;
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,&white);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    }
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    for (size_t i=0;!error && i<batch.count;++i) {
        ParticleVertex vertices[4];const RxScreenPolygon *polygon=&batch.polygons[i];
        for (unsigned j=0;j<4;++j) {
            float depth=fminf(fmaxf(a+b/polygon->position[j][2],0),65534);
            vertices[j]=(ParticleVertex){
                .position={truncf(polygon->position[j][0]*16)/16-1728,
                           truncf(polygon->position[j][1]*16)/16-1808,
                           1-2*(truncf(depth*16)/16)/65534},
                .rgba={polygon->color[j][0],polygon->color[j][1],polygon->color[j][2],polygon->color[j][3]},.fog=255};
        }
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLE_FAN,0,4);
        if (glGetError()!=GL_NO_ERROR) error="Original cinema polygon draw failed";
    }
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glBindVertexArray(0);
    rx_effect_callbacks_destroy(&batch);return error;
}

const char *rx_message_frame_present(RxParticleDraw **out,const RxMessageFrame *batch)
{
    if(!out||!batch)return "Invalid original font frame";
    if(!batch->count)return NULL;
    if(!Ps2_tex_buff||batch->count>RX_MESSAGE_GLYPHS||sys->ef_tlist.textures!=sys->ef_tex||batch->texture!=sys->ef_tex[0].texaddr)
        return "Original message draw bank was retired";
    if(!*out)*out=open_draw();RxParticleDraw *draw=*out;
    if(!draw)return "Cannot create original message renderer";
    glUseProgram(draw->program);glUniform1i(draw->screen_space,1);glUniform1i(draw->solid_color,0);
    glUniform3f(draw->fog_color,0,0,0);glBindVertexArray(draw->vao);glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    const char *error=select_texture(draw,0,0);
    if(!error&&(gs.ctx[0].tex0!=batch->tex0||gs.texa!=batch->texa))error="Original message atlas state changed before presentation";
    /* bhDispFont selects original filter mode 0 (point sampling). */
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    for(unsigned i=0;!error&&i<batch->count;++i){ParticleVertex vertices[4];const RxMessagePolygon *polygon=batch->polygons+i;
        for(unsigned j=0;j<4;++j){vertices[j]=(ParticleVertex){
            .position={truncf(polygon->position[j][0]*16)/16-1728,truncf(polygon->position[j][1]*16)/16-1808,-1},
            .uv={polygon->uv[j][0],polygon->uv[j][1]},
            .rgba={polygon->color[j][0],polygon->color[j][1],polygon->color[j][2],polygon->color[j][3]},.fog=255};}
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLE_FAN,0,4);
        if(glGetError()!=GL_NO_ERROR)error="Original message glyph draw failed";
    }
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glBindVertexArray(0);return error;
}

const char *rx_message_present(RxParticleDraw **out)
{return rx_message_frame_present(out,rx_message_frame());}

const char *rx_fade_present(RxParticleDraw **out)
{
    const RxFadeFrame *frame=rx_fade_frame();if(!frame->visible)return NULL;
    if(!*out)*out=open_draw();RxParticleDraw *draw=*out;
    if(!draw)return "Cannot create original screen fade renderer";
    glUseProgram(draw->program);glUniform1i(draw->screen_space,1);glUniform1i(draw->solid_color,1);
    glUniform3f(draw->fog_color,0,0,0);glBindVertexArray(draw->vao);glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,draw->texture);
    if(!draw->texture_identity){const uint32_t white=UINT32_MAX;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,&white);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
    ParticleVertex vertices[4];const RxScreenPolygon *polygon=&frame->polygon;
    for(unsigned j=0;j<4;++j)vertices[j]=(ParticleVertex){
        .position={polygon->position[j][0]-1728,polygon->position[j][1]-1808,-1},
        .rgba={polygon->color[j][0],polygon->color[j][1],polygon->color[j][2],polygon->color[j][3]},.fog=255};
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLE_FAN,0,4);
    GLenum error=glGetError();glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glBindVertexArray(0);
    return error==GL_NO_ERROR?NULL:"Original screen fade draw failed";
}

#ifdef RECVX_NATIVE_INVENTORY_RENDER
const char *rx_inventory_sprite_present(RxParticleDraw **out,const RxInventorySpriteFrame *frame)
{
    if(!out||!frame||frame->count>RX_INVENTORY_SPRITE_LIMIT||frame->image_count>RX_INVENTORY_SPRITE_LIMIT)
        return "Invalid original inventory frame";
    for(unsigned i=0;i<frame->count;++i)if(frame->polygons[i].image>=frame->image_count)return "Invalid inventory polygon image";
    for(unsigned i=0;i<frame->image_count;++i)if(!frame->images[i].rgba||!frame->images[i].width||!frame->images[i].height||frame->images[i].width>512||frame->images[i].height>512)return "Invalid inventory image snapshot";
    if(!frame->count)return NULL;
    if(!*out)*out=open_draw();RxParticleDraw *draw=*out;if(!draw)return "Could not open inventory sprite renderer";
    glUseProgram(draw->program);glUniform1i(draw->screen_space,1);glUniform1i(draw->solid_color,0);
    glUniform1i(draw->texture_alpha,1);glUniform3f(draw->fog_color,0,0,0);
    glBindVertexArray(draw->vao);glBindBuffer(GL_ARRAY_BUFFER,draw->vertices);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,draw->texture);
    /* Snapshot uploads must invalidate the particle/message texture cache. */
    draw->texture_identity=0;
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    unsigned selected=UINT32_MAX;
    for(unsigned i=0;i<frame->count;++i){
        const RxInventorySpritePolygon *p=frame->polygons+i;
        const RxInventorySpriteImage *image=frame->images+p->image;
        if(selected!=p->image){
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image->width,image->height,0,GL_RGBA,GL_UNSIGNED_BYTE,image->rgba);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,(image->tex1&(1u<<6))?GL_LINEAR:GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,(image->tex1&(1u<<5))?GL_LINEAR:GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,(image->clamp&3)?GL_CLAMP_TO_EDGE:GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,((image->clamp>>2)&3)?GL_CLAMP_TO_EDGE:GL_REPEAT);
            selected=p->image;
        }
        ParticleVertex vertices[4];
        for(unsigned j=0;j<4;++j)vertices[j]=(ParticleVertex){
            .position={truncf(p->position[j][0]*16)/16-1728,truncf(p->position[j][1]*16)/16-1808,-1},
            .uv={p->uv[j][0],p->uv[j][1]},.rgba={p->color[j][0],p->color[j][1],p->color[j][2],p->color[j][3]},.fog=255};
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
    GLenum error=glGetError();glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);glBindVertexArray(0);
    return error==GL_NO_ERROR?NULL:"Original inventory sprite presentation failed";
}
#endif
