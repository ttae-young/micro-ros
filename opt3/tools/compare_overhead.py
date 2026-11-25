"""Compare matched OFF/protected logs; totals use exact uint64 cycle sums.
Usage: python tools/compare_overhead.py off.txt mpu.txt > overhead.csv
Times are elapsed time, not exclusive CPU time. Nested metrics must not be summed.
"""
import csv,sys,re
from decimal import Decimal
from pathlib import Path

def parse(path):
    raw=Path(path).read_bytes()
    text=raw.decode('utf-16' if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig')
    if '[BUILD] variant=app_sfi ' not in text:
        raise ValueError('Only app_sfi runs with periodic direct reads may be compared')
    runs=re.findall(r'\[BENCH DONE\] mode=(\d+) subscribers=(\d+) valid=(YES|NO)',text)
    if len(runs)!=1 or runs[0][2]!='YES': raise ValueError('Exactly one valid run per file required')
    config=re.search(r'CPU_Hz=(\d+) warmup=(\d+) samples=(\d+)',text)
    if not config: raise ValueError('Missing timing configuration')
    if 'actual_rx_buffer=1 in_timing=1 PASS' not in text: raise ValueError('Missing validated periodic direct read')
    rows={}
    for row in csv.reader(text.splitlines()):
        if len(row)==11 and row[0]=='CSV' and row[1].isdigit():
            if tuple(row[1:3])!=runs[0][:2]: raise ValueError('CSV mode/subscribers differ from completed run')
            if row[3] in rows: raise ValueError('Duplicate metric')
            rows[row[3]]=(int(row[4]),int(row[9]))
    if not rows: raise ValueError('Missing cumulative cycle columns')
    tune=re.search(r'^\[TUNE\] (.+)$',text,re.M)
    if not tune: raise ValueError('Missing tune stage')
    trim=re.search(r'^\[TRIM\] (.+)$',text,re.M)
    if not trim: raise ValueError('Missing trim configuration')
    workload=re.search(r'\[WORKLOAD\] direct_read_each_iteration=([01]) same_apps=1',text)
    if not workload: raise ValueError('Missing workload identifier')
    revision=re.search(r'^\[FIX\] (.+)$',text,re.M)
    payload=re.search(r'^\[PAYLOAD\] (.+)$',text,re.M)
    payload_config=payload.group(1).strip() if payload else 'legacy-int32'
    return runs[0][:2],config.groups()+(payload_config,)+(tune.group(1).strip(),trim.group(1).strip(),workload.group(1))+((revision.group(1).strip() if revision else 'original'),),rows

def compare(a,b):
    ar,ac,av=parse(a); br,bc,bv=parse(b)
    if ar[0]!='1' or br[0] not in ('2','3'): raise ValueError('First file must be mode1; second mode2/MPU or mode3/SFI')
    if ar[1]!=br[1] or ac!=bc or av.keys()!=bv.keys(): raise ValueError('Conditions/metrics differ')
    hz=Decimal(ac[0]); out=[]
    for metric,(n,off) in av.items():
        bn,on=bv[metric]
        if n!=bn or n==0: raise ValueError('Sample counts differ or zero: '+metric)
        delta=on-off; us=Decimal(delta)*1000000/hz
        out.append([metric,n,off,on,delta,f'{us:.6f}',f'{us/n:.6f}',
                    f'{Decimal(on)/off:.6f}' if off else 'NA',
                    'negative_observed_difference' if delta<0 else 'nonnegative_observed_difference'])
    return out

if __name__=='__main__':
    try:
        rows=compare(sys.argv[1],sys.argv[2])
        output=Path(sys.argv[3]).open('w',encoding='utf-8-sig',newline='') if len(sys.argv)>3 else sys.stdout
        w=csv.writer(output)
        w.writerow(['metric','count','off_total_cycles','protected_total_cycles','total_extra_cycles',
                    'total_overhead_us','mean_overhead_us','protected_over_off','note'])
        w.writerows(rows)
        if output is not sys.stdout: output.close()
    except (ValueError,IndexError,OSError) as e:
        sys.exit('Comparison rejected: '+str(e))
