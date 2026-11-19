"""Validate MPU build identity, required C application symbols, syscall veneer
and privileged placement. This is not a whole-program SFI verifier.
"""
import sys, struct, json
from pathlib import Path

class ELF:
    def __init__(self,data):
        self.data=data
        if data[:7]!=b'\x7fELF\x01\x01\x01': raise ValueError('ELF32 LE required')
        if struct.unpack_from('<H',data,18)[0]!=40: raise ValueError('ARM required')
        phoff,shoff=struct.unpack_from('<II',data,28)
        phsize,phnum,shsize,shnum=struct.unpack_from('<HHHH',data,42)
        self.segments=[struct.unpack_from('<IIIIIIII',data,phoff+i*phsize) for i in range(phnum)]
        self.sections=[struct.unpack_from('<IIIIIIIIII',data,shoff+i*shsize) for i in range(shnum)]
        self.symbols={}
        for sh in self.sections:
            if sh[1]!=2: continue
            names=self.sections[sh[6]]
            strings=data[names[4]:names[4]+names[5]]
            for off in range(sh[4],sh[4]+sh[5],sh[9]):
                n,v,size,info,other,index=struct.unpack_from('<IIIBBH',data,off)
                name=strings[n:strings.find(b'\0',n)].decode('ascii',errors='replace')
                if name and index: self.symbols[name]=(v,size,info,index)
    def addr(self,name): return self.symbols[name][0]&~1
    def span(self,name):
        v,n,info,index=self.symbols[name]
        if info&15 != 2 or not(v&1): raise ValueError('Thumb function required: '+name)
        return v&~1,n
    def offset(self,address,size):
        for typ,off,vaddr,paddr,filesz,memsz,flags,align in self.segments:
            if typ==1 and vaddr==paddr and flags&2==0 and 0x08000000<=address and address+size<=0x08080000:
                if vaddr<=address and address+size<=vaddr+filesz:
                    return off+address-vaddr
        raise ValueError('Not immutable loaded flash: '+hex(address))
    def read(self,address,size):
        off=self.offset(address,size)
        b=self.data[off:off+size]
        if len(b)!=size: raise ValueError('Truncated ELF')
        return b

def branch_target(blob,pc,link):
    h1,h2=struct.unpack('<HH',blob)
    if h1&0xf800!=0xf000 or h2&0xd000!=(0xd000 if link else 0x9000):
        raise ValueError('Expected BL' if link else 'Expected B.W')
    s=(h1>>10)&1
    i1=1^((h2>>13)&1)^s
    i2=1^((h2>>11)&1)^s
    off=(s<<24)|(i1<<23)|(i2<<22)|((h1&1023)<<12)|((h2&2047)<<1)
    if s: off-=1<<25
    return pc+4+off


def verify(data,manifest=None,elf_path=None):
    elf=ELF(data)
    mode=struct.unpack('<I',elf.read(elf.addr('bench_policy_mode'),4))[0]
    if mode not in (1,2,3): raise ValueError('Unsupported mode')
    for name in ('publisher_app_task','subscriber_app_task','subscriber_app_callback','app_gate_impl'):
        addr,size=elf.span(name)
        if size==0: raise ValueError('Missing application function '+name)
        elf.read(addr,size)
    addr,size=elf.span('app_gate'); gate=elf.read(addr,size)
    if mode==2:
        if gate!=bytes.fromhex('47df7047'): raise ValueError('Incorrect syscall veneer')
        lo=elf.symbols['__syscalls_flash_start__'][0]; hi=elf.symbols['__syscalls_flash_end__'][0]
        if not lo<=addr<=hi: raise ValueError('Gate not in syscall section')
        lo=elf.symbols['__privileged_functions_start__'][0]; hi=elf.symbols['__privileged_functions_end__'][0]
        if not lo<=elf.addr('app_gate_impl')<=hi: raise ValueError('Implementation not privileged')
    else:
        if size!=4 or branch_target(gate,addr,False)!=elf.addr('app_gate_impl'):
            raise ValueError('Incorrect direct gate')
    if mode==3:
        if elf_path is None: raise ValueError('ELF path required for SFI disassembly validation')
        from sfi_verify import verify_sfi
        verify_sfi(elf,elf_path,branch_target)
    return mode

if __name__=='__main__':
    try:
        mode=verify(Path(sys.argv[1]).read_bytes(),elf_path=sys.argv[1])
        print(f'[APP VERIFY] PASS mode={mode} C_apps=2 trusted_gate=1 SFI_CLOSED_APP_VALIDATION={1 if mode==3 else 0}')
    except Exception as e:
        print('[APP VERIFY] FAIL: '+str(e),file=sys.stderr); sys.exit(1)
