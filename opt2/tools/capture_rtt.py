"""Capture one run; preserve timestamped evidence and release ST-LINK on DONE."""
import sys,subprocess,datetime,shutil,re
from pathlib import Path
from verify_worker import verify

def capture(elf):
    mode=verify(Path(elf).read_bytes())
    logs=Path(__file__).resolve().parent.parent/'logs'
    logs.mkdir(exist_ok=True)
    path=logs/f'mode{mode}_{datetime.datetime.now():%Y%m%d_%H%M%S}.log'
    print('Saving RTT to '+str(path),flush=True)
    proc=subprocess.Popen([sys.executable,'-u','-m','pyocd','rtt','-t','cortex_m','-M','attach',
                           '-a','0x20000000','-s','0x30000'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    done=False
    try:
        with path.open('wb') as f:
            for line in iter(proc.stdout.readline,b''):
                f.write(line);f.flush()
                print(line.decode('utf-8',errors='replace'),end='',flush=True)
                if b'[BENCH DONE]' in line:
                    done=True;break
                if b'[BENCH ERROR]' in line or b'[BENCH INVALID: FAULT]' in line:break
    except KeyboardInterrupt:
        print('\nCapture interrupted; partial log retained.')
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    # Latest attempt replaces old latest even if invalid: never compare stale success.
    shutil.copyfile(path,logs/f'mode{mode}_latest.log')
    stage=re.search(rb'\[TUNE\] stage=(\d+)',path.read_bytes())
    if stage: shutil.copyfile(path,logs/f'stage{stage.group(1).decode()}_mode{mode}_latest.log')
    print('\nLog: '+str(path))
    return 0 if done else 1

if __name__=='__main__':sys.exit(capture(sys.argv[1]))
