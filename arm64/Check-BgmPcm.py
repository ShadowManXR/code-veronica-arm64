"""Compare Quest PCM captures with independent float arithmetic for original CRI.
Requires BGM members extracted from the user's disc and matching .quest.pcm files.
No game content is embedded in this script.
"""
from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);args=parser.parse_args()
import math,struct,array
f32=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
for track in [45,83,114]:
 p=args.directory;data=(p/f'{track}.adx').read_bytes(); rate=int.from_bytes(data[8:12],'big');cut=int.from_bytes(data[16:18],'big')
 val1=f32(f32(math.sqrt(2))-f32(math.cos(f32((3.14159265*2*cut)/rate))));val2=f32(math.sqrt(2))-1.0;r=(val1-f32(math.sqrt(f32((val1+val2)*(val1-val2)))))/val2;k0=int(2*r*4096);k1=int(-r*r*4096)
 channels=data[7];pos=int.from_bytes(data[2:4],'big')+4;history=[[0.0,0.0] for _ in range(channels)];result=array.array('h');frames=rate*2
 for frame in range((frames+31)//32):
  decoded=[]
  for ch in range(channels):
   block=data[pos:pos+18];pos+=18;gain=(int.from_bytes(block[:2],'big')&8191)+1;values=[];d0,d1=history[ch]
   for byte in block[2:]:
    for nib in (byte>>4,byte&15):
     if nib>=8:nib-=16
     value=f32(f32(f32(k0/4096*d0)+f32(k1/4096*d1))+f32(nib*65536*gain));d1,d0=d0,value
     values.append(max(-2147483648,min(2147483647,int(value)))>>16)
   history[ch]=[d0,d1];decoded.append(values)
  for i in range(32):result.extend((decoded[0][i],decoded[channels-1][i]))
 result=result[:frames*2];ref=array.array('h',(p/f'{track}.quest.pcm').read_bytes());diff=[abs(x-y) for x,y in zip(ref,result)];print(track,k0,k1,'mismatches',sum(x!=0 for x in diff),'max',max(diff));assert len(ref)==len(result) and not any(diff), 'Original CRI PCM differs'
