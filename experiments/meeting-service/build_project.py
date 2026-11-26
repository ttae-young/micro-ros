"""Package the committed opt3 board support and the AddTwoInts service."""
import argparse
import io
from pathlib import Path
import re
import subprocess
from zipfile import ZIP_DEFLATED, ZipFile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NAME = "freertos_meeting_service"
BOARD_REF = "0b2a7270695011ed2fa6b402db6b0c6ffac7a58a"
KEEP = {"1_start_agent.ps1", "2_flash_and_rtt.ps1", "serial_tcp_bridge.py", "start_agent_wsl.sh"}


def compose(output, ref, elf=None):
    board = subprocess.check_output(["git", "archive", "--format=zip", ref, "opt3/"], cwd=ROOT)
    with ZipFile(io.BytesIO(board)) as base, ZipFile(output, "w", ZIP_DEFLATED) as result:
        for item in base.infolist():
            path = item.filename.removeprefix("opt3/")
            if item.is_dir() or path.startswith(("validation/", "tools/")):
                continue
            if "/" not in path and path.endswith((".md", ".cmd", ".ps1")) and path not in KEEP:
                continue
            if path in {"Core/Src/pubsub_app.c", "Core/Src/worker_domain.S"}:
                continue
            data = base.read(item)
            if path == "start_agent_wsl.sh":
                data = data.replace(b"\r\n", b"\n")
            if path == "Core/Src/freertos_mpu_demo.c":
                data = (HERE / "service_app.c").read_bytes()
            if path in {".project", ".cproject", "2_flash_and_rtt.ps1"}:
                data = data.replace(b"freertos_app_payload", NAME.encode())
            if path == ".cproject":
                data = re.sub(rb' post(?:announcebuild|build)Step="[^"]*"', b"", data)
            if path == "2_flash_and_rtt.ps1":
                data = b"\n".join(
                    line for line in data.split(b"\n") if b"tools/verify_worker.ps1" not in line
                )
                data = data.replace(b"'tools/capture_rtt.py') $Firmware", b"'tools/capture_rtt.py')")
            if path == "Core/Inc/benchmark_config.h":
                data = data.replace(b"#define BENCH_MODE 3", b"#define BENCH_MODE 1")
            result.writestr(f"{NAME}/{path}", data)

        for path in ("README.md", "tools/capture_rtt.py", "LICENSE", "NOTICE"):
            result.write(HERE / path, f"{NAME}/{path}")
        if elf:
            result.writestr(f"{NAME}/Debug/{NAME}.elf", elf.read_bytes())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / f"{NAME}.zip")
    parser.add_argument("--ref", default=BOARD_REF, help="Git ref supplying opt3 board support")
    parser.add_argument("--elf", type=Path, help="Include a linked AddTwoInts firmware")
    args = parser.parse_args()
    compose(args.output, args.ref, args.elf)
    print(args.output.resolve())

