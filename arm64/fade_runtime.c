#include "fade_runtime.h"
#include "screen.h"
#include "main.h"
#include "ps2_dummy.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
static RxFadeFrame frame,next;
static int collecting;
static const char *capture_error;
static int channel(float value){return isfinite(value)&&value>=0&&value<=255;}
const char *rx_fade_check(void)
{
    const float values[]={sys->fade_an,sys->fade_rn,sys->fade_gn,sys->fade_bn,sys->fade_ap,sys->fade_rp,sys->fade_gp,sys->fade_bp};
    for(unsigned i=0;i<8;++i)if(!channel(values[i]))return "Invalid original screen fade channel";
    return NULL;
}
void rx_fade_clear(void){frame.visible=next.visible=0;}
const RxFadeFrame *rx_fade_frame(void){return &frame;}
int rx_fade_collecting(void){return collecting;}
int rx_fade_capture(uint64_t primitive,const void *data,unsigned count)
{
    if(!collecting||capture_error)return -1;
    if(primitive!=UINT64_C(0x26800000000000)||count!=4||!data||next.visible||Ps2_gs_save.ALPHA!=0x44){capture_error="Unsupported original screen fade polygon";return -1;}
    const unsigned char *source=data;RxScreenPolygon polygon={0};const float x[]={1728,2368,2368,1728},y[]={1808,1808,2288,2288};
    for(unsigned i=0;i<4;++i){memcpy(polygon.color[i],source+i*48+16,16);memcpy(polygon.position[i],source+i*48+32,12);
        if(polygon.position[i][0]!=x[i]||polygon.position[i][1]!=y[i]||polygon.position[i][2]!=-1){capture_error="Original screen fade extent exceeds native viewport";return -1;}
        for(unsigned j=0;j<4;++j)if(polygon.color[i][j]>(j==3?128u:255u)){capture_error="Invalid original screen fade packet color";return -1;}
    }
    next.polygon=polygon;next.visible=1;return 0;
}
typedef struct {unsigned cb,pbk;float ct,hkn,current[4],old[4],target[4];} State;
static State save(void){return (State){sys->cb_flg,sys->fade_pbk,sys->fade_ct,sys->fade_hkn,
    {sys->fade_an,sys->fade_rn,sys->fade_gn,sys->fade_bn},{sys->fade_ao,sys->fade_ro,sys->fade_go,sys->fade_bo},{sys->fade_ap,sys->fade_rp,sys->fade_gp,sys->fade_bp}};}
static void restore(State s){sys->cb_flg=s.cb;sys->fade_pbk=s.pbk;sys->fade_ct=s.ct;sys->fade_hkn=s.hkn;
    sys->fade_an=s.current[0];sys->fade_rn=s.current[1];sys->fade_gn=s.current[2];sys->fade_bn=s.current[3];
    sys->fade_ao=s.old[0];sys->fade_ro=s.old[1];sys->fade_go=s.old[2];sys->fade_bo=s.old[3];
    sys->fade_ap=s.target[0];sys->fade_rp=s.target[1];sys->fade_gp=s.target[2];sys->fade_bp=s.target[3];}
const char *rx_fade_tick(void)
{
    if(collecting||!channel(sys->fade_an))return "Invalid native screen fade context";
    State state=save();
    if(sys->cb_flg&2){const char *error=rx_fade_check();if(error)return error;
        if(!isfinite(state.hkn)||state.hkn<1||state.hkn>256||!isfinite(state.ct)||state.ct<0||state.ct>state.hkn)return "Invalid original screen fade countdown";
        for(unsigned i=0;i<4;++i){if(!channel(state.old[i]))return "Invalid original screen fade origin";
            float value=state.current[i];
            if(state.hkn==1||state.ct+1>=state.hkn-1)value=state.target[i];
            else if(state.ct!=0)value+=(state.target[i]-state.old[i])/(state.hkn-1);
            if(!channel(value))return "Original screen fade step exceeds native channel bounds";
        }
        bhControlScreenFade();
    }
    next.visible=0;
    if(sys->fade_an>0){const char *error=rx_fade_check();if(error){restore(state);return error;}
        capture_error=NULL;collecting=1;bhDrawScreenFade();collecting=0;
        if(capture_error){restore(state);return capture_error;}
        if(!next.visible){restore(state);return "Original screen fade produced no native polygon";}
    }
    frame=next;return NULL;
}
