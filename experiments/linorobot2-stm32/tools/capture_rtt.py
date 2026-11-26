"""Save the Linorobot2 RTT log and release the debug probe after the run."""
from datetime import datetime
from pathlib import Path
import subprocess
import sys


def capture():
    directory = Path(__file__).resolve().parents[1] / "logs"
    directory.mkdir(exist_ok=True)
    path = directory / f"linorobot2_{datetime.now():%Y%m%d_%H%M%S}.log"
    print(f"Saving RTT to {path}", flush=True)
    reader = subprocess.Popen(
        [sys.executable, "-u", "-m", "pyocd", "rtt", "-t", "cortex_m", "-M", "attach",
         "-a", "0x20000000", "-s", "0x30000"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    try:
        with path.open("wb") as log:
            for line in reader.stdout:
                log.write(line)
                log.flush()
                print(line.decode(errors="replace"), end="", flush=True)
                if b"[BENCH ERROR]" in line:
                    return 1
                if b"[BENCH DONE]" in line:
                    return 0 if b"valid=YES" in line else 1
    except KeyboardInterrupt:
        return 1
    finally:
        if reader.poll() is None:
            reader.terminate()
            reader.wait(timeout=5)
    return 1


if __name__ == "__main__":
    sys.exit(capture())
