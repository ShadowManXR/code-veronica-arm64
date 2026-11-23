#include "adx_native.h"
#include "types.h"
#include "../../../cri/mwlib/ee/lib/libadxe/adx_dcd.h"
#include "../../../cri/mwlib/ee/lib/libadxe/adx_dcd3.h"
#include "audio/audio.h"
#include "ps2/cdvd_image.h"
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
/* Android ADXT transport. The original CRI coefficient and float decoder
   bodies prepare immutable PCM; callback does no disc I/O or allocation. */
static pthread_mutex_t mutex=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
void ADXPS2_Lock(void){pthread_mutex_lock(&mutex);}
void ADXPS2_Unlock(void){pthread_mutex_unlock(&mutex);}
static unsigned be16(const unsigned char *p){return (unsigned)p[0]*256+p[1];}
static unsigned be32(const unsigned char *p){return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];}
const char *rx_adx_header(const void *data,size_t bytes,RxAdxHeader *out)
{
    const unsigned char *p=data;RxAdxHeader h={0};
    if(!p||!out||bytes<36||p[0]!=0x80||p[1]||p[4]!=3||p[5]!=18||p[6]!=4||p[0x13])return "Unsupported or truncated original ADX header";
    h.data_offset=be16(p+2)+4;h.channels=p[7];h.rate=be32(p+8);h.total=be32(p+12);
    if((h.channels!=1&&h.channels!=2)||h.rate<8000||h.rate>48000||!h.total||h.total>INT_MAX||
       h.data_offset<36||h.data_offset%2||h.data_offset>bytes||memcmp(p+h.data_offset-6,"(c)CRI",6))return "Invalid original ADX format or signature";
    if(((uint64_t)h.total+31)/32*h.channels*18>bytes-h.data_offset)return "ADX samples exceed encoded data";
    if(p[0x12]!=3)return "Original ADX header version is not ported yet";
    if(h.data_offset>=44&&be32(p+24)){h.loop=1;h.start_sample=be32(p+28);h.start_byte=be32(p+32);h.end_sample=be32(p+36);}
    if(h.loop&&(h.start_sample>=h.end_sample||h.end_sample>h.total||
       h.start_byte!=(uint64_t)h.data_offset+(uint64_t)h.start_sample/32*h.channels*18))return "ADX loop exceeds original sample data";
    unsigned cutoff=be16(p+16);if(cutoff>=h.rate/2)return "Invalid ADX predictor cutoff";
    ADX_GetCoefficient(cutoff,h.rate,&h.coefficient[0],&h.coefficient[1]);*out=h;return NULL;
}
typedef struct {unsigned partition,member;int16_t *pcm;size_t bytes;RxAdxHeader header;} Bank;
static Bank banks[256];static size_t bank_count,cache_bytes;
typedef struct {
    ADX_TALK handle;const Bank *bank;int volume,frequency;
    float gain;unsigned decoded;uint64_t played,phase;
    int16_t current[2],next[2];int primed,next_valid;
} Voice;
static Voice voices[2];
static Voice *voice(ADXT h){for(unsigned i=0;i<2;++i)if(h==&voices[i].handle&&h->used)return voices+i;return NULL;}
int rx_adx_handle(const void *h){int ok;ADXPS2_Lock();ok=voice((ADXT)h)!=NULL;ADXPS2_Unlock();return ok;}
int rx_adx_slot_handle(unsigned slot,const void *h){int ok;ADXPS2_Lock();ok=slot<2&&h==&voices[slot].handle&&voices[slot].handle.used;ADXPS2_Unlock();return ok;}
const char *rx_adx_prepare(unsigned partition,unsigned member)
{
    for(size_t i=0;i<bank_count;++i)if(banks[i].partition==partition&&banks[i].member==member)return NULL;
    uint32_t lsn,size;if(!adx_afs_locate(partition,member,&lsn,&size))return "ADX request references an absent original archive member";
    if(size<36||size>32*1024*1024||bank_count==256||cache_bytes+size>128*1024*1024)return "ADX archive exceeds native cache bounds";
    size_t sectors=((size_t)size+2047)/2048;unsigned char *data=malloc(sectors*2048);if(!data)return "Cannot allocate original ADX data";
    if(cdvd_read_sectors(lsn,(uint32_t)sectors,data)!=(int)sectors){free(data);return "Cannot read original ADX archive member";}
    RxAdxHeader h;const char *error=rx_adx_header(data,size,&h);if(error){free(data);return error;}
    size_t blocks=((size_t)h.total+31)/32,samples=blocks*32,pcm_bytes=samples*4;
    if(pcm_bytes>128*1024*1024-cache_bytes){free(data);return "Original ADX PCM exceeds native cache bounds";}
    int16_t *left=malloc(samples*2),*right=malloc(samples*2),*pcm=malloc(pcm_bytes);
    if(!left||!right||!pcm){free(left);free(right);free(pcm);free(data);return "Cannot allocate original CRI decoded PCM";}
    Sint16 history[2][2]={{0}};int decoded=h.channels==1?
        ADX_DecodeMonoFloat((Sint8 *)data+h.data_offset,(int)blocks,left,history[0],h.coefficient[0],h.coefficient[1]):
        ADX_DecodeSteFloat((Sint8 *)data+h.data_offset,(int)(blocks*2),left,history[0],right,history[1],h.coefficient[0],h.coefficient[1]);
    if(decoded!=(int)(blocks*h.channels)){free(left);free(right);free(pcm);free(data);return "Original CRI decoder rejected an ADX block";}
    for(size_t i=0;i<samples;++i){pcm[i*2]=left[i];pcm[i*2+1]=h.channels==1?left[i]:right[i];}
    free(left);free(right);free(data);
    ADXPS2_Lock();banks[bank_count++]=(Bank){partition,member,pcm,pcm_bytes,h};cache_bytes+=pcm_bytes;ADXPS2_Unlock();return NULL;
}
void rx_adx_reset(void){ADXPS2_Lock();memset(voices,0,sizeof(voices));for(size_t i=0;i<bank_count;++i)free(banks[i].pcm);memset(banks,0,sizeof(banks));bank_count=cache_bytes=0;ADXPS2_Unlock();}
const char *rx_adx_prepare_channels(unsigned partition,unsigned member,unsigned channels)
{
    const char *error=rx_adx_prepare(partition,member);if(error)return error;
    for(size_t i=0;i<bank_count;++i)if(banks[i].partition==partition&&banks[i].member==member)
        return banks[i].header.channels<=channels?NULL:"ADX member exceeds original stream channel capacity";
    return "ADX prepared member is absent";
}
ADXT ADXT_Create(Sint32 channels,void *work,Sint32 bytes){
    if(channels<1||channels>2||!work||bytes<=0)return NULL;
    ADXPS2_Lock();ADXT result=NULL;for(unsigned i=0;i<2;++i)if(!voices[i].handle.used){Voice *v=voices+i;memset(v,0,sizeof(*v));v->handle.used=1;v->handle.maxnch=channels;v->volume=-999;v->handle.stat=0;result=&v->handle;break;}ADXPS2_Unlock();return result;
}
void ADXT_Stop(ADXT h){ADXPS2_Lock();Voice *v=voice(h);if(v){v->bank=NULL;v->handle.stat=0;v->primed=0;v->played=v->phase=0;}ADXPS2_Unlock();}
void ADXT_Destroy(ADXT h){ADXPS2_Lock();Voice *v=voice(h);if(v)memset(v,0,sizeof(*v));ADXPS2_Unlock();}
void ADXT_StartAfs(ADXT h,Sint32 partition,Sint32 member){
    ADXPS2_Lock();Voice *v=voice(h);if(v){const Bank *bank=NULL;for(size_t i=0;i<bank_count;++i)if(banks[i].partition==(unsigned)partition&&banks[i].member==(unsigned)member)bank=banks+i;
        int max=v->handle.maxnch,vol=v->volume,freq=v->frequency;float gain=v->gain;
        memset(v,0,sizeof(*v));v->handle.used=1;v->handle.maxnch=max;v->volume=vol;v->gain=gain;v->frequency=freq;
        if(bank&&bank->header.channels<=(unsigned)max){v->bank=bank;v->handle.stat=3;}else v->handle.stat=6;
    }ADXPS2_Unlock();
}
Sint32 ADXT_GetStat(ADXT h){ADXPS2_Lock();Voice *v=voice(h);int status=v?v->handle.stat:6;ADXPS2_Unlock();return status;}
void ADXT_GetTime(ADXT h,Sint32 *samples,Sint32 *rate){ADXPS2_Lock();Voice *v=voice(h);if(samples)*samples=v?(int)(v->played>INT_MAX?INT_MAX:v->played):0;if(rate)*rate=v&&v->bank?(int)v->bank->header.rate:1;ADXPS2_Unlock();}
void ADXT_SetOutVol(ADXT h,Sint32 volume){if(volume>0)volume=0;if(volume< -999)volume=-999;ADXPS2_Lock();Voice *v=voice(h);if(v){v->volume=volume;v->gain=volume<=-999?0.0f:powf(10.0f,volume/200.0f);}ADXPS2_Unlock();}
void ADXT_SetSvrFreq(ADXT h,Sint32 frequency){ADXPS2_Lock();Voice *v=voice(h);if(v)v->frequency=frequency;ADXPS2_Unlock();}
void ADXT_SetAutoRcvr(ADXT h,Sint32 mode){(void)h;(void)mode; /* Original recovery policy has no I/O underruns with prepared immutable data. */}
void ADXT_SetReloadSct(ADXT h,Sint32 sectors){(void)h;(void)sectors; /* Encoded data is resident before publication. */}
void ADXT_Pause(ADXT h,Sint32 paused){(void)h;(void)paused; /* Retail PS2 ADXT_Pause is empty, as in the upstream host. */}
static int sample(Voice *v,int16_t out[2])
{
    const RxAdxHeader *h=&v->bank->header;
    if(h->loop&&v->decoded>=h->end_sample)v->decoded=h->start_sample;
    if(v->decoded>=h->total)return 0;
    memcpy(out,v->bank->pcm+(size_t)v->decoded*2,4);++v->decoded;return 1;
}

void rx_adx_mix(int16_t *pcm,size_t frames)
{
    if(!pcm)return;ADXPS2_Lock();for(unsigned id=0;id<2;++id){Voice *v=voices+id;if(!v->handle.used||v->handle.stat!=3||!v->bank)continue;
        for(size_t i=0;i<frames;++i){
            if(!v->primed){if(!sample(v,v->current)){v->handle.stat=5;break;}v->next_valid=sample(v,v->next);v->primed=1;}
            for(unsigned ch=0;ch<2;++ch){float value=v->current[ch];if(v->next_valid)value+=(v->next[ch]-v->current[ch])*(v->phase/48000.0f);
                int mixed=pcm[i*2+ch]+(int)lrintf(value*v->gain);if(mixed>32767)mixed=32767;else if(mixed< -32768)mixed=-32768;pcm[i*2+ch]=(int16_t)mixed;}
            v->phase+=v->bank->header.rate;if(v->phase>=48000){v->phase-=48000;++v->played;if(!v->next_valid){v->handle.stat=5;break;}memcpy(v->current,v->next,4);v->next_valid=sample(v,v->next);}
        }
    }ADXPS2_Unlock();
}
