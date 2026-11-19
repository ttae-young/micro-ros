from pathlib import Path
import sys,copy,tempfile,struct
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R/'tools'))
from verify_worker import ELF,verify,branch_target
from sfi_verify import decode,audit,APPS,HOOKS,verify_sfi
path=Path(sys.argv[1]) if len(sys.argv)>1 else R/'Debug/freertos_app_sfi.elf';data=path.read_bytes();e=ELF(data)
assert verify(data,elf_path=path)==3
allowed={e.addr(n):n for n in APPS+HOOKS}
ins=decode(e,path,'publisher_app_task');entry=e.addr('publisher_app_task')
first_body=entry+ins[entry][0]
variants={
 'raw_object_load':('ldr','r0, [r0]'),
 'raw_object_store':('str','r0, [r1]'),
 'indirect_branch':('bx','r0'),
 'privilege_change':('msr','CONTROL, r0'),
 'exception_bypass':('svc','#0'),
 'stack_pivot':('mov','sp, r0'),
 'LR_overwrite':('mov','lr, r0'),
 'saved_frame_store':('str','r0, [sp, #0]'),
 'outside_stack_load':('ldr','r0, [sp, #1024]'),
 'forbidden_call':('bl',f'{e.addr("sfi_probe_write"):x} <wrong_direct_entry>'),
 'middle_of_instruction':('b',f'{entry+2:x}'),
}
for name,(mn,op) in variants.items():
 bad=copy.deepcopy(ins);bad[first_body]=(ins[first_body][0],mn,op)
 try:audit('publisher_app_task',bad,entry,allowed)
 except (ValueError,AssertionError):print('PASS reject',name)
 else:raise AssertionError('Accepted '+name)
# A real ELF mutation must also fail, not just a synthetic disassembly map.
bad=bytearray(data);at=e.offset(first_body,2);bad[at:at+2]=bytes.fromhex('0068') # ldr r0,[r0]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'unchecked_load.elf';p.write_bytes(bad)
 try:verify(bytes(bad),elf_path=p)
 except (ValueError,AssertionError):print('PASS reject mutated ELF unchecked LDR')
 else:raise AssertionError('Mutated ELF accepted')
graph,frames=verify_sfi(e,path,branch_target)
print('PASS complete linked SFI app validation; frames=',frames)
