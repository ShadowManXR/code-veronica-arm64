#include "opening.h"
#include "boot_disc.h"
#include "event_audio.h"
#include "main.h"
#include "sdfunc.h"
#include "ps2_NaMatrix.h"
#include "build/retail_pan_reference.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
extern char PanTbl360[72],PanTbl360Vol[72],ThreeDVolTbl[510];
extern NJS_POINT3 CameraPos;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL retail pan: %s\n",why);exit(1);}}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");
 RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);
 check(!memcmp(PanTbl360,retail_PanTbl360,72)&&!memcmp(PanTbl360Vol,retail_PanTbl360Vol,72),"native tables differ from independently captured retail bytes");
 NJS_POINT3 saved_camera=CameraPos;int yaw=cam.ay;NJS_MATRIX saved_matrix;memcpy(saved_matrix,*cam.mtx,64);CameraPos=(NJS_POINT3){0};njUnitMatrix(cam.mtx);
 unsigned cases=0,tail=0;const unsigned radii[]={1,64,512};
 for(unsigned r=0;r<3;++r){NJS_POINT3 position={0,0,-(float)radii[r]};for(unsigned angle=0;angle<65536;++angle){
  cam.ay=(short)angle;unsigned index=(angle*72u)>>16;check(index<72,"independent retail angle bin");e=rx_event_step_position_check(&position);check(!e,e);
  char pan=99,volume=99;float distance=-1;Get3DSoundParameter(&CameraPos,&position,&pan,&volume,&distance,0);
  unsigned at=radii[r]>510?509:radii[r];char expected_pan=(char)retail_PanTbl360[index];char expected_volume=(char)(ThreeDVolTbl[at]+(char)retail_PanTbl360Vol[index]);
  check(distance==radii[r]&&pan==expected_pan&&volume==expected_volume,"original 3D sound pan/volume differs from retail angle and attenuation reference");++cases;if(index>=68)++tail;
 }}
 NJS_POINT3 bad={0,0,-510};check(rx_event_step_position_check(&bad)!=NULL,"unported attenuation index 510 admitted");
 printf("PASS %u original 3D pan/volume samples, every 16-bit listening angle/three distances; %u tail-bin samples match retail padding without out-of-bounds table reads\n",cases,tail);
 cam.ay=yaw;CameraPos=saved_camera;memcpy(*cam.mtx,saved_matrix,64);check(!o.events_ready,"full opening unexpectedly enabled");rx_opening_end(&o);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
