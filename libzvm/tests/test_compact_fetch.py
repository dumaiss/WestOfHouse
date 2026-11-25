"""Execute the actual fetch routine on the platform Z80 core.

Models zvm_mem_fetch's ABI and error return, not OS mapping or IRQ timing.
"""
import os,sys,re,subprocess,tempfile
from pathlib import Path
os_root = Path(os.environ.get('ZEPHYROS_ROOT', Path(__file__).resolve().parents[3] / 'ZephyrOS'))
sys.path.insert(0,str(os_root / 'tests'))
from z80sim import Z80
source=Path(__file__).resolve().parents[1] / 'src/compact_fetch.asm'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 (p/'stub.asm').write_text('.module stub\n.globl _zvm_mem_fetch\n.area _CODE\n_zvm_mem_fetch::\nret\n')
 subprocess.run(['sdasz80','-o',str(p/'fetch.rel'),str(source)],check=True,capture_output=True)
 subprocess.run(['sdasz80','-o',str(p/'stub.rel'),str(p/'stub.asm')],check=True,capture_output=True)
 (p/'probe.lk').write_text('-n\n-m\n-w\n-i probe.ihx\n-b _CODE = 0x0100\nfetch.rel\nstub.rel\n-e\n')
 subprocess.run(['sdldz80','-f','probe.lk'],cwd=p,check=True,capture_output=True)
 syms={m.group(2):int(m.group(1),16) for m in re.finditer(r'^\s*([0-9A-F]{8})\s+(\S+)',(p/'probe.map').read_text(),re.M)}
 mem=bytearray(65536)
 for line in (p/'probe.ihx').read_text().splitlines():
  n,a,t=int(line[1:3],16),int(line[3:7],16),int(line[7:9],16)
  if t==0:mem[a:a+n]=bytes.fromhex(line[9:9+n*2])
 for pc in (0,0x42,0xff,0x1fff,0xffff,0x1ffff,0xffffffff):
  for error in (0,4,7):
   z=Z80(bytes(mem),bytes(65536));z.latch=0x10;z.bank0[:]=mem
   z.sp=0xd000;z.push16(0xface);z.pc=syms['_zvm_compact_next']
   z.h=0x40;z.l=0;z.d=0x50;z.e=0;z.iy=0x9876;z.ix=0x1234
   z.bank0[0x4021:0x4025]=pc.to_bytes(4,'little')
   calls=0
   for step in range(100):
    if z.pc==0xface:break
    if z.pc==syms['_zvm_mem_fetch']:
     assert z.bank_of(z.pc)==0
     assert (z.h<<8|z.l)==0x4000
     ret=z.pop16()
     assert int.from_bytes(bytes(z.bank0[z.sp:z.sp+4]),'little')==pc
     assert z.rd16(z.sp+4)==0x5000
     z.sp+=6
     if not error:z.wr(0x5000,0xab)
     z.h=0xbe;z.l=0xef;z.iy=0x7777;z.a=error;z.pc=ret;calls+=1
    else:z.step()
   assert calls==1 and z.pc==0xface and z.sp==0xd000
   assert z.a==error and z.iy==0x9876 and z.ix==0x1234
   want=pc if error else (pc+1)&0xffffffff
   assert int.from_bytes(bytes(z.bank0[0x4021:0x4025]),'little')==want
 print('compact Z80 fetch: PASS (21 boundary/error combinations, ABI/stack/register preservation)')
