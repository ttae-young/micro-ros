"""Matched baseline/MPU/SFI logs; exact cycle totals, no rounded-mean subtraction."""
import sys,csv
from decimal import Decimal
from pathlib import Path
from compare_overhead import parse

def compare_three(paths):
 runs=[parse(p) for p in paths]
 if [x[0][0] for x in runs]!=['1','2','3']:raise ValueError('Expected MODE1, MODE2, MODE3 logs in that order')
 if any(x[0][1]!=runs[0][0][1] or x[1]!=runs[0][1] or x[2].keys()!=runs[0][2].keys() for x in runs[1:]):
  raise ValueError('Workload, configuration, or metrics differ; rerun matching modes')
 hz=Decimal(runs[0][1][0]);rows=[]
 for metric,(n,_) in runs[0][2].items():
  if not n or any(r[2][metric][0]!=n for r in runs):raise ValueError('Sample counts differ')
  totals=[Decimal(r[2][metric][1])*1000000/hz for r in runs]
  rows.append([metric,n]+[f'{v/n:.6f}' for v in totals]+[f'{v:.6f}' for v in totals]+
   [f'{(totals[i]-totals[0])/n:.6f}' for i in (1,2)]+[f'{totals[i]-totals[0]:.6f}' for i in (1,2)]+
   [f'{totals[i]/totals[0]:.6f}' if totals[0] else 'NA' for i in (1,2)]+[f'{totals[2]/totals[1]:.6f}' if totals[1] else 'NA'])
 return rows

HEADERS=['metric','count','baseline_mean_us','mpu_mean_us','sfi_mean_us','baseline_total_us','mpu_total_us','sfi_total_us',
 'mpu_mean_extra_us','sfi_mean_extra_us','mpu_total_extra_us','sfi_total_extra_us','mpu_over_baseline','sfi_over_baseline','sfi_over_mpu']
if __name__=='__main__':
 try:
  rows=compare_three(sys.argv[1:4])
  with Path(sys.argv[4]).open('w',encoding='utf-8-sig',newline='') as f:
   w=csv.writer(f);w.writerow(HEADERS);w.writerows(rows)
 except (ValueError,IndexError,OSError) as e:sys.exit('Comparison rejected: '+str(e))
