"""Execute the LINKED SFI runtime with Cortex-M emulation.
Tests memory policy and denial-before-access, not UART/scheduling/hardware MPU.
"""
from pathlib import Path
import sys,struct
R=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(R/'tools'))
from verify_worker import ELF
from unicorn import *
from unicorn.arm_const import *
path=Path(sys.argv[1]) if len(sys.argv)>1 else R/'Debug/freertos_app_payload.elf'
data=path.read_bytes();e=ELF(data)
assert struct.unpack('<I',e.read(e.addr('bench_policy_mode'),4))[0]==3
def addr(n):return e.symbols[n][0]
tested=0
for who in range(5):
 own=0x20031000+who*128
 stack=addr('g_worker_stack') if who==0 else addr('g_sub_stacks')+(who-1)*4096
 tcb=addr('g_worker_tcb') if who==0 else addr('g_sub_tcbs')+(who-1)*(e.symbols['g_sub_tcbs'][1]//4)
 cases=[('own_word'+str(i),own+i*4,True,True) for i in range(32)]
 cases += [('stack_data',stack+64,True,True),('active_frame',stack+4092,True,False)]
 cases += [('other_object'+str(i),0x20031000+i*128,False,False) for i in range(5) if i!=who]
 for peer in range(5):
  if peer!=who:
   other=addr('g_worker_stack') if peer==0 else addr('g_sub_stacks')+(peer-1)*4096
   cases.append(('other_stack'+str(peer),other+64,False,False))
 cases += [('kernel',addr('g_policy_canary'),False,False),('sfi_metadata',addr('g_probe'),False,False),
           ('null',0,False,False),('overflow',0xfffffffc,False,False),('unaligned',own+1,False,False),
           ('boundary',own+127,False,False),('peripheral',0x40000000,False,False),('code',0x08000000,False,False),('forged_identity',own,False,False)]
 for name,target,read_ok,write_ok in cases:
  for write,allowed in ((False,read_ok),(True,write_ok)):
   c=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
   c.mem_map(0x08000000,0x80000);c.mem_map(0x20000000,0x40000)
   for typ,off,va,pa,fs,ms,flags,al in e.segments:
    if typ==1 and fs and ((0x08000000<=va<va+fs<=0x08080000) or (0x20000000<=va<va+fs<=0x20040000)):
     c.mem_write(va,data[off:off+fs])
   def put(a,v):c.mem_write(a,struct.pack('<I',v))
   def get(a):return struct.unpack('<I',c.mem_read(a,4))[0]
   put(addr('g_pub'),0x20031000)
   for peer in range(4):put(addr('g_sub')+4*peer,0x20031080+peer*128)
   put(addr('g_active'),who);put(addr('pxCurrentTCB'),tcb+4 if name=='forged_identity' else tcb)
   put(addr('g_probe')+8,target);put(addr('g_probe')+24,tcb)
   if allowed:put(target,0x12345678)
   c.reg_write(UC_ARM_REG_CONTROL,2);c.reg_write(UC_ARM_REG_PSP,stack+4096-128)
   c.reg_write(UC_ARM_REG_R0,target);c.reg_write(UC_ARM_REG_R1,0x87654321)
   stop=0x0807f000;c.reg_write(UC_ARM_REG_LR,stop|1);returned=[]
   def hook(uc,pc,size,u):
    if pc==e.addr('fatal'):
     if name=='forged_identity': returned.append('rejected');uc.emu_stop();return
     raise AssertionError(('unexpected fatal',who,name,write))
    if pc==stop:returned.append(True);uc.emu_stop()
   c.hook_add(UC_HOOK_CODE,hook)
   before=bytes(c.mem_read(0x20000000,0x40000))
   c.emu_start(e.addr('sfi_probe_write' if write else 'sfi_probe_read')|1,0,count=20000)
   if name=='forged_identity':
    assert returned==['rejected'] and get(addr('g_probe')+4)==0
    tested+=1;continue
   assert returned,(who,name,write,'not returned')
   assert c.reg_read(UC_ARM_REG_R0)==((0x5a5a if write else 0x12345678) if allowed else 0x80000001),(who,name,write)
   assert get(addr('g_probe')+4)==(0 if allowed else 1)
   if write and allowed:assert get(target)==0x87654321
   if not allowed and 0x20000000<=target<=0x2003fffc and target!=addr('g_probe'):
    assert bytes(c.mem_read(target,4))==before[target-0x20000000:target-0x20000000+4]
   tested+=1
print('PASS linked SFI runtime:',tested,'read/write cases across 5 actual TCB identities; object/stack/kernel/bounds/overflow/active-frame checks')
