#include "event_audio.h"
#include "audio/snd.h"
#include "ps2/cdvd_image.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned char *owned;static unsigned owned_key;static SndBlob sq,hd,bd,shot_hd,shot_bd;
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned char *part(const char *name,uint32_t offset,uint32_t size,uint32_t *bytes){
 uint32_t lsn,total;if(!cdvd_find_file(name,&lsn,&total))return NULL;if(!size)size=total;
 if(offset>total||size>total-offset||!size||size>16*1024*1024)return NULL;
 size_t lead=offset%2048,sectors=(lead+size+2047)/2048;unsigned char *data=malloc(sectors*2048);if(!data)return NULL;
 if(cdvd_read_sectors(lsn+offset/2048,(uint32_t)sectors,data)!=(int)sectors){free(data);return NULL;}
 memmove(data,data+lead,size);*bytes=size;return data;
}
int rx_event_se_mounted(void){return owned!=NULL;}
const char *rx_event_se_assets(unsigned sequence){
 if(!owned||memcmp(&g_snd_banks.sq[2],&sq,sizeof(sq))||memcmp(&g_snd_banks.midi_hd[2],&hd,sizeof(hd))||memcmp(&g_snd_banks.bd[2],&bd,sizeof(bd)))return "Event MIDI requires its owned native room sound bank";
 if(sq.size<0x28||hd.size<0x34||!bd.size)return "Truncated native event MIDI bank";
 uint32_t chunk=word(sq.data+0x24);if(chunk>sq.size||sq.size-chunk<16)return "Invalid event sequence table";
 unsigned max=word(sq.data+chunk+12);if(max>127||sequence>max||sequence>=128||((size_t)max+1)*4>sq.size-chunk-16)return "Event MIDI sequence exceeds owned SQ bank";
 uint32_t offset=word(sq.data+chunk+16+sequence*4);
 if(offset>sq.size-chunk||sq.size-chunk-offset<8)return "Event MIDI sequence block exceeds owned SQ bank";
 unsigned remain=sq.size-chunk-offset;const unsigned char *block=sq.data+chunk+offset;unsigned start=word(block),division=(unsigned)block[4]|(unsigned)block[5]<<8;
 if(start<6||start>=remain||!division)return "Invalid original event MIDI sequence header";
 return NULL;
}
const char *rx_event_shot_assets(unsigned program){
 if(!owned||memcmp(&g_snd_banks.se_hd[2],&shot_hd,sizeof(shot_hd))||memcmp(&g_snd_banks.bd[5],&shot_bd,sizeof(shot_bd)))return "Shot request requires its owned native room bank";
 if(shot_hd.size<0x34||!shot_bd.size)return "Truncated original shot bank";
 unsigned chunk=word(shot_hd.data+0x24);
 if(chunk>shot_hd.size||shot_hd.size-chunk<16)return "Invalid original shot program table";
 unsigned max=word(shot_hd.data+chunk+12);
 if(max>127||program>max||((size_t)max+1)*4>shot_hd.size-chunk-16)return "Shot program exceeds native bank";
 unsigned offset=word(shot_hd.data+chunk+16+program*4);
 if(offset>shot_hd.size-chunk||shot_hd.size-chunk-offset<8)return "Shot program exceeds owned header";
 return NULL;
}
const char *rx_event_se_mount(unsigned key){
 if(owned){if(key!=owned_key)return "Native room event MIDI bank is already occupied";const char *error=rx_event_se_assets(0);return error?error:rx_event_shot_assets(0);}
 if(key>65535)return "Invalid room sound key";
 unsigned char *pack=NULL;uint32_t bytes=0;
 for(unsigned disc=1;disc<=2&&!pack;++disc){char name[32];uint32_t n=0;snprintf(name,sizeof(name),"\\MULTSPQ%u.IDX;1",disc);unsigned char *index=part(name,0,0,&n);if(!index)continue;
  unsigned i=0;while(i<n/2&&((unsigned)index[i*2]|(unsigned)index[i*2+1]<<8)!=65535&&((unsigned)index[i*2]|(unsigned)index[i*2+1]<<8)!=key)++i;
  int found=i<n/2&&((unsigned)index[i*2]|(unsigned)index[i*2+1]<<8)==key;free(index);if(!found)continue;
  snprintf(name,sizeof(name),"\\MULTSPQ%u.AFS;1",disc);unsigned char *toc=part(name,0,16+i*8,&n);if(!toc)return "Cannot read native room sound archive table";
  if((memcmp(toc,"AFS\0",4)&&memcmp(toc,"AFS ",4))||i>=word(toc+4)){free(toc);return "Invalid native room sound archive";}
  uint32_t offset=word(toc+8+i*8),size=word(toc+12+i*8);free(toc);if(!size)return "Empty native room sound member";pack=part(name,offset,size,&bytes);
 }
 if(!pack)return "Cannot read original room SPQ sound pack";
 SndBlob next_sq={0},next_hd={0},next_bd={0},next_sh={0},next_sb={0};int end=0;const char *error=NULL;
 for(size_t t=0;t+16<=bytes;t+=16){uint32_t offset=word(pack+t),size=word(pack+t+4),type=word(pack+t+8),bank=word(pack+t+12);if(!offset){end=1;break;}
  if(offset>bytes||size>bytes-offset){error="SPQ payload exceeds native sound pack";break;}
  if(bank!=2)continue;
  if(type==2){if(next_sh.data||size<16){error="Invalid original shot block";break;}const unsigned char *block=pack+offset;uint32_t h=word(block),hs=word(block+4),b=word(block+8),bs=word(block+12);
   if(h>size||hs>size-h||b>size||bs>size-b){error="Shot HD/BD exceeds owned sound pack";break;}next_sh=(SndBlob){pack+offset+h,hs,hs};next_sb=(SndBlob){pack+offset+b,bs,bs};}

  if(type==0){if(next_sq.data){error="Duplicate event SQ block";break;}next_sq=(SndBlob){pack+offset,size,size};}
  if(type==1){if(next_hd.data||size<16){error="Invalid event HD/BD block";break;}const unsigned char *block=pack+offset;uint32_t h=word(block),hs=word(block+4),b=word(block+8),bs=word(block+12);
   if(h>size||hs>size-h||b>size||bs>size-b){error="Event HD/BD ranges exceed SPQ block";break;}next_hd=(SndBlob){pack+offset+h,hs,hs};next_bd=(SndBlob){pack+offset+b,bs,bs};}
 }
 if(!error&&(!end||!next_sq.data||next_sq.size<0x28||!next_hd.data||next_hd.size<0x34||!next_bd.size||!next_sh.data||!next_sb.size))error="Missing original room MIDI/shot banks";
 if(error){free(pack);return error;}
 snd_synth_lock();
 if(g_snd_banks.sq[2].data||g_snd_banks.midi_hd[2].data||g_snd_banks.bd[2].data||g_snd_banks.se_hd[2].data||g_snd_banks.bd[5].data){snd_synth_unlock();free(pack);return "Event MIDI bank would replace occupied native assets";}
 owned=pack;owned_key=key;sq=next_sq;hd=next_hd;bd=next_bd;g_snd_banks.sq[2]=sq;g_snd_banks.midi_hd[2]=hd;g_snd_banks.bd[2]=bd;shot_hd=next_sh;shot_bd=next_sb;g_snd_banks.se_hd[2]=shot_hd;g_snd_banks.bd[5]=shot_bd;
 snd_synth_bank_changed(SND_BANK_SQ,2);snd_synth_bank_changed(SND_BANK_MIDI,2);snd_synth_bank_changed(SND_BANK_BODY,2);snd_synth_unlock();
 error=rx_event_se_assets(0);if(!error)error=rx_event_shot_assets(0);if(error)rx_event_se_end();return error;
}
void rx_event_se_end(void){
 if(!owned)return;snd_synth_lock();snd_synth_release_midi_bank(2);snd_synth_release_shot_bank(2);
 if(g_snd_banks.sq[2].data==sq.data)g_snd_banks.sq[2]=(SndBlob){0};if(g_snd_banks.midi_hd[2].data==hd.data)g_snd_banks.midi_hd[2]=(SndBlob){0};if(g_snd_banks.bd[2].data==bd.data)g_snd_banks.bd[2]=(SndBlob){0};
 if(g_snd_banks.se_hd[2].data==shot_hd.data)g_snd_banks.se_hd[2]=(SndBlob){0};if(g_snd_banks.bd[5].data==shot_bd.data)g_snd_banks.bd[5]=(SndBlob){0};
 free(owned);owned=NULL;sq=hd=bd=shot_hd=shot_bd=(SndBlob){0};snd_synth_bank_changed(SND_BANK_SQ,2);snd_synth_bank_changed(SND_BANK_MIDI,2);snd_synth_bank_changed(SND_BANK_BODY,2);snd_synth_unlock();
}


