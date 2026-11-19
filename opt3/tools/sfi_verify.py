"""Fail-closed verifier for this closed native C application, not arbitrary C.
All dynamic data accesses go through authenticated runtime hooks. Compiler
stack spills are confined to bounded local slots, never saved control words.
Control transfers are direct internal edges, approved entries, or balanced
returns through protected active frames. Unknown instructions reject the ELF.
The compiler/linker, this verifier, trusted runtime and RTOS are the TCB.
"""
import re,subprocess
from pathlib import Path

APPS=('publisher_app_task','subscriber_app_task','subscriber_app_callback','diagnostic')
HOOKS=('sfi_app_read32','sfi_app_write32','app_probe_read','app_probe_write','app_gate')

def reg(s):
 s=s.strip();return {'sp':'r13','lr':'r14','pc':'r15','ip':'r12','fp':'r11','sl':'r10','sb':'r9'}.get(s,s)

def registers(s):
 assert s.startswith('{') and s.endswith('}'),('register list',s)
 out=[]
 for part in s[1:-1].split(','):
  part=part.strip()
  if '-' in part:
   a,b=map(reg,part.split('-'));out.extend('r'+str(i) for i in range(int(a[1:]),int(b[1:])+1))
  else:out.append(reg(part))
 return out

def decode(elf,path,name):
 tools=list(Path('C:/ST').glob('STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-objdump.exe'))
 if not tools:raise ValueError('ARM objdump required for complete SFI validation')
 text=subprocess.check_output([str(sorted(tools)[-1]),'-d','--disassemble='+name,str(path)],text=True)
 lo,size=elf.span(name); out={}
 pattern=r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4}\s+)+)([a-z][a-z0-9.]*)\s*(.*)$'
 for line in text.splitlines():
  literal=re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+\.word\s+',line)
  if literal:
   pc=int(literal[1],16)
   if lo<=pc<lo+size:out[pc]=(4,'.word',literal[2])
   continue
  m=re.match(pattern,line)
  if not m:continue
  pc=int(m[1],16)
  if not lo<=pc<lo+size:continue
  out[pc]=(len(m[2].split())*2,m[3],re.split('[;@]',m[4])[0].strip())
 # No hidden undecoded bytes or literal pools inside app functions.
 pos=lo
 while pos<lo+size:
  if pos not in out:raise ValueError(f'Undecoded app instruction {name} {pos:x}')
  pos+=out[pos][0]
 if pos!=lo+size:raise ValueError('Instruction extends outside app function')
 return out

def audit(name,ins,entry,allowed):
 pending=[(entry,())]; states={}; calls=set(); maximum=0; literals=set()
 while pending:
  pc,stack=pending.pop()
  if pc in states:
   if states[pc]!=stack:raise ValueError(f'Inconsistent stack at {name}:{pc:x}')
   continue
  if pc not in ins:raise ValueError('Branch outside decoded instruction boundary')
  states[pc]=stack;size,mn,op=ins[pc]; base=mn.split('.')[0]
  if base in ('stmdb','ldmia') and op.startswith('sp!,'):
   base='push' if base=='stmdb' else 'pop';op=op.split(',',1)[1].strip()
  nxt=pc+size; successors=[nxt]; new=stack
  if base in ('push','pop'):
   rs=registers(op)
   if 'r13' in rs:raise ValueError('SP in register list')
   if base=='push':
    if 'r15' in rs:raise ValueError('PC push forbidden')
    new=tuple(rs)+stack
   else:
    if len(rs)>len(stack):raise ValueError('Stack underflow')
    want=tuple('r14' if r=='r15' else r for r in rs)
    if stack[:len(rs)]!=want:raise ValueError('Saved register/control frame overwritten')
    new=stack[len(rs):]
    if 'r15' in rs:
     if new:raise ValueError('Unbalanced return')
     successors=[]
  elif base in ('sub','add') and op.startswith('sp,'):
   m=re.fullmatch(r'sp,\s*(?:sp,\s*)?#(\d+)',op)
   if not m or int(m[1])%4:raise ValueError('Dynamic/unaligned SP update')
   n=int(m[1])//4
   if base=='sub':new=('local',)*n+stack
   else:
    if len(stack)<n or any(x!='local' for x in stack[:n]):raise ValueError('SP skips control frame')
    new=stack[n:]
  elif base in ('ldr','str'):
   literal=re.fullmatch(r'([^,]+),\s*\[pc,\s*#(\d+)\]',op)
   if base=='ldr' and literal:
    target=((pc+4)&~3)+int(literal[2])
    if reg(literal[1]) in ('r13','r14','r15') or target not in ins or ins[target][1]!='.word':
     raise ValueError('Invalid literal load')
    literals.add(target);pending.append((nxt,stack));continue
   m=re.fullmatch(r'([^,]+),\s*\[sp(?:,\s*#(\d+))?\]',op)
   if not m:raise ValueError(f'Raw non-stack memory access: {name}:{pc:x} {mn} {op}')
   if reg(m[1]) in ('r13','r14','r15'):raise ValueError('Control register data load/store')
   offset=int(m[2] or 0)
   if offset%4 or offset//4>=len(stack) or stack[offset//4]!='local':raise ValueError('Access to saved frame/outside stack locals')
  elif base=='bl':
   target=int(op.split()[0],16)
   if target not in allowed:raise ValueError(f'Unapproved call {target:x}')
   if len(stack)%2:raise ValueError('Unaligned call stack')
   # LR must be saved/protected before making any call.
   if 'r14' not in stack:raise ValueError('Call without protected return address')
   calls.add(allowed[target])
  elif base=='b' or re.fullmatch(r'b(eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)',base):
   target=int(op.split()[0],16)
   if target in ins:successors=([target] if base=='b' else [target,nxt])
   elif base=='b' and target in allowed and not stack:
    calls.add(allowed[target]);successors=[]
   else:raise ValueError('Unapproved branch/tail call')
  elif base in ('cbz','cbnz'):
   target=int(op.split(',')[1].strip().split()[0],16)
   if target not in ins:raise ValueError('CB branch outside function')
   successors=[target,nxt]
  elif base=='bx':
   if reg(op)!='r14' or stack:raise ValueError('Indirect branch or unbalanced return')
   successors=[]
  elif base=='mrs':
   if not re.fullmatch(r'r\d+,\s*CONTROL',op,re.I):raise ValueError('Unapproved system register read')
   if reg(op.split(',')[0]) in ('r13','r14','r15'):raise ValueError('MRS to control register')
  elif re.fullmatch(r'it[te]{0,3}',base):pass
  else:
   arithmetic=('mov','movs','movw','movt','add','adds','sub','subs','cmp','cmn','tst','and','ands','orr','orrs','eor','eors','bic','bics','lsl','lsls','lsr','lsrs','asr','asrs','rsb','rsbs','uxtb','uxth','nop')
   root=base
   if root not in arithmetic and root[-2:] in ('eq','ne','cs','cc','hi','ls','ge','lt','gt','le'):
    root=root[:-2]
   if root not in arithmetic:raise ValueError(f'Unapproved instruction {name}: {mn} {op}')
   if root not in ('cmp','cmn','tst','nop') and reg(op.split(',')[0]) in ('r13','r14','r15'):
    raise ValueError('Unapproved control register update')
  maximum=max(maximum,len(new)*4)
  if maximum>256:raise ValueError('App stack frame exceeds reviewed bound')
  for target in successors:pending.append((target,new))
 # Only trailing unreachable NOP alignment is allowed; hidden code rejects.
 for pc,(_,mn,op) in ins.items():
  if pc not in states and mn!='nop' and pc not in literals:raise ValueError('Unreachable unreviewed app code')
 return calls,maximum

def verify_sfi(elf,path,branch_target):
 allowed={elf.addr(n):n for n in APPS+HOOKS}
 graph={};frames={}
 for n in APPS:
  graph[n],frames[n]=audit(n,decode(elf,path,n),elf.addr(n),allowed)
 def walk(n,ancestors):
  if n in ancestors:raise ValueError('Recursive app call graph')
  return frames[n]+max([walk(c,ancestors+(n,)) for c in graph[n] if c in APPS]+[0])
 for n in APPS:
  if walk(n,())>512:raise ValueError('App call-stack bound exceeded')
 for n,target in (('app_probe_read','sfi_probe_read'),('app_probe_write','sfi_probe_write')):
  a,size=elf.span(n)
  if size!=4 or branch_target(elf.read(a,4),a,False)!=elf.addr(target):raise ValueError('SFI probe bypass')
 return graph,frames
