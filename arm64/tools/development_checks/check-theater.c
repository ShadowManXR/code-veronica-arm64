#include "../android/theater.c"
#include <stdio.h>
static unsigned char image[640*480*4];
int main(void)
{
    RxEgl egl={0};
    RxTheater theater={0};
    if(!rx_egl_open(&egl,NULL,640,480))return 1;
    if(!create_room(&theater)||!create_game_target(&theater))return 2;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,640,480);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(theater.room_program);
    glBindVertexArray(theater.room_vao);
    XrView eye={XR_TYPE_VIEW};
    eye.pose.orientation.w=1;
    eye.fov=(XrFovf){-.8f,.8f,.65f,-.65f};
    float matrix[16];
    eye_matrix(&eye,matrix);
    glUniformMatrix4fv(theater.room_matrix,1,GL_FALSE,matrix);
    glDrawArrays(GL_TRIANGLES,0,theater.room_count);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,image);
    if(glGetError())return 3;
    unsigned colored=0;
    for(unsigned i=0;i<sizeof(image);i+=4)colored+=(image[i]|image[i+1]|image[i+2])!=0;
    if(colored<100000)return 4;
    FILE *file=fopen("/data/local/tmp/recvx-theater-room.rgba","wb");
    if(!file||fwrite(image,1,sizeof(image),file)!=sizeof(image)||fclose(file))return 5;
    rx_theater_target(&theater);
    glClearColor(.25f,.5f,.75f,1);
    glClear(GL_COLOR_BUFFER_BIT);
    printf("source FBO %x err %x\n",glCheckFramebufferStatus(GL_FRAMEBUFFER),glGetError());
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,image);
    printf("source RGB %u %u %u err %x\n",image[0],image[1],image[2],glGetError());
    GLuint target,fbo;
    glGenTextures(1,&target);
    glBindTexture(GL_TEXTURE_2D,target);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_SRGB8_ALPHA8,SCREEN_W,SCREEN_H);
    if(!copy_game(&theater,target))return 8;
    printf("copy err %x\n",glGetError());
    glGenFramebuffers(1,&fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,image);
    printf("destination status %x err %x\n",glCheckFramebufferStatus(GL_FRAMEBUFFER),glGetError());
    if(glGetError()||abs(image[0]-64)>1||abs(image[1]-128)>1||abs(image[2]-191)>1){
        printf("copy RGB %u %u %u\n",image[0],image[1],image[2]);return 6;
    }
    eye.pose.position.x=-.032f;
    float left[16],right[16];
    eye_matrix(&eye,left);
    eye.pose.position.x=.032f;
    eye_matrix(&eye,right);
    if(left[12]<=0||right[12]>=0)return 7;
    printf("PASS stereo room renderer with %u vertices, %u visible pixels\n",theater.room_count,colored);
    puts("PASS game display RGB preserved in sRGB compositor texture; stereo eye transforms");
    glDeleteFramebuffers(1,&fbo);
    glDeleteTextures(1,&target);
    // The stack-owned test object has no XR handles.
    glDeleteFramebuffers(1,&theater.game_fbo);
    glDeleteFramebuffers(1,&theater.eye_fbo);
    glDeleteRenderbuffers(1,&theater.game_depth);
    glDeleteTextures(1,&theater.game_texture);
    glDeleteBuffers(1,&theater.room_buffer);
    glDeleteVertexArrays(1,&theater.room_vao);
    glDeleteProgram(theater.room_program);
    rx_egl_close(&egl);
    return 0;
}



