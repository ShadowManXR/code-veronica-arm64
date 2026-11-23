#include "item_names.h"
#include "types.h"
#include "main.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *bank;
static size_t bank_bytes;
static unsigned name_count;
static unsigned char *system_bank;
static size_t system_bytes;
static unsigned system_count;
static unsigned word(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static const char *span(const unsigned char *base,size_t bytes,unsigned count,unsigned id,const unsigned char **begin,size_t *length)
{
    if(!count||count>256||bytes<(size_t)(count+1)*4||word(base)!=count||id>=count)return "Invalid original item-name table";
    unsigned offset=word(base+4+id*4);size_t end=bytes;
    for(unsigned i=0;i<count;++i){unsigned at=word(base+4+i*4);
        if(at<(count+1)*4||at%2||at>=bytes)return "Item-name offset exceeds owned bank";
        if(at>offset&&at<end)end=at;
    }
    unsigned words=0;
    for(size_t at=offset;at+2<=end&&words<512;at+=2,++words){unsigned code=base[at]|(unsigned)base[at+1]<<8;
        if(code==0xffff){*begin=base+offset;*length=at+2-offset;return NULL;}
        if(code>=108&&code!=0xff01)return "Unported original item-name glyph/control";
    }
    return "Item name has no bounded terminator";
}
void rx_item_names_clear(void)
{
    free(bank);bank=NULL;bank_bytes=0;name_count=0;
    free(system_bank);system_bank=NULL;system_bytes=0;system_count=0;
    if(sys){sys->mes_ip=NULL;sys->mes_sp=NULL;}
}
const char *rx_item_names_load(const void *data,size_t bytes)
{
    if(!data||bytes<8)return "Truncated original system message bank";
    if(bank||!sys||sys->mes_ip)return "Item-name bank already has an owner";
    const unsigned char *source=data;size_t length=word(source);
    if(length<8||length%2||length>1024*1024||length>bytes-4)return "Invalid original item-name bank size";
    unsigned count=word(source+4);const unsigned char *text;size_t size;
    if(!count||count>256)return "Invalid original item-name count";
    for(unsigned i=0;i<count;++i){const char *error=span(source+4,length,count,i,&text,&size);if(error)return error;}
    unsigned char *copy=malloc(length);if(!copy)return "Could not own original item-name bank";
    memcpy(copy,source+4,length);bank=copy;bank_bytes=length;name_count=count;sys->mes_ip=(unsigned int*)copy;return NULL;
}
const char *rx_item_names_span(unsigned id,const unsigned char **text,size_t *bytes)
{
    if(!text||!bytes||!bank||!sys||sys->mes_ip!=(unsigned int*)bank)return "Missing or foreign native item-name bank";
    return span(bank,bank_bytes,name_count,id,text,bytes);
}
static const char *system_offsets(const unsigned char *base,size_t bytes,unsigned count)
{
    if(!count||count>1024||bytes<(size_t)(count+1)*4||word(base)!=count)return "Invalid original system-message table";
    for(unsigned i=0;i<count;++i){unsigned at=word(base+4+i*4);
        if(at<(count+1)*4||at%2||at>bytes-2)return "System-message offset exceeds owned bank";
    }
    return NULL;
}
const char *rx_system_messages_load(const void *data,size_t bytes)
{
    if(!data||bytes<8||!sys)return "Truncated original system messages";
    if(system_bank||sys->mes_sp)return "System-message bank already has an owner";
    const unsigned char *source=data;size_t first=word(source);
    if(first>bytes-8)return "Missing second system-message bank";
    source+=4+first;size_t length=word(source);
    if(length<8||length%2||length>1024*1024||length>bytes-first-8)return "Invalid system-message bank size";
    unsigned count=word(source+4);const char *error=system_offsets(source+4,length,count);if(error)return error;
    unsigned char *copy=malloc(length);if(!copy)return "Could not own original system messages";
    memcpy(copy,source+4,length);system_bank=copy;system_bytes=length;system_count=count;sys->mes_sp=(unsigned int*)copy;return NULL;
}
const char *rx_system_messages_data(const unsigned char **data,size_t *bytes,unsigned *count)
{
    if(!data||!bytes||!count||!system_bank||!sys||sys->mes_sp!=(unsigned int*)system_bank)return "Missing or foreign native system-message bank";
    const char *error=system_offsets(system_bank,system_bytes,system_count);if(error)return error;
    *data=system_bank;*bytes=system_bytes;*count=system_count;return NULL;
}
int rx_system_message_current(void)
{
    uintptr_t pointer=(uintptr_t)sys->mes_dp,base=(uintptr_t)system_bank;
    return system_bank&&pointer>=base&&pointer-base<system_bytes;
}
