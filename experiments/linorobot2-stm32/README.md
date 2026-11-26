# Linorobot2 on STM32L552

Linorobot2 Humble의 2륜 운동학·PID·오도메트리 경로를 STM32L552의 micro-ROS 및 FreeRTOS 프로젝트에 이식했다. 호스트가 Twist를 하나씩 보내면 MCU는 고정된 합성 엔코더 RPM 기록으로 제어 계산을 하고 표준 Odometry를 응답한다. PID 결과는 메모리에 기록된다. 엔코더와 모터가 없어도 실행할 수 있다. 원본 출처와 Apache-2.0 라이선스는 [Linorobot2 Humble](https://github.com/linorobot/linorobot2_hardware/tree/077cfee58d6f7b9fd376857b104158c1b3b696e3)에 있다.

이 펌웨어는 LowFat ON, MPU OFF, SFI OFF인 Baseline 작동 확인용이다. 원본 50Hz 타이머는 명령당 한 번 동작하는 재생 방식으로 바꿨다. 오도메트리에는 고정 20ms의 논리적 시간 간격을 쓴다. 호스트에서 관측되는 실제 전송 주기는 직렬 통신과 명령·응답 시간에 따라 정해진다.

## 빌드

프로젝트 ZIP을 푼 뒤 CubeIDE의 **File → Import → General → Existing Projects into Workspace**에서 `freertos_linorobot2`를 가져온다. **Debug Build** 후 `Debug/freertos_linorobot2.elf`가 생성됐는지 확인한다. 아래 실행 경로는 CubeIDE가 복사한 기본 워크스페이스다. 다른 폴더에 가져왔다면 `$project`를 실제 경로로 바꾼다.

저장소에서 프로젝트 ZIP을 다시 만들려면 저장소 루트에서 실행한다.

```powershell
& "$env:USERPROFILE/pyocd-win/Scripts/python.exe" .\experiments\linorobot2-stm32\build_project.py
```

## Agent, Flash·RTT, 입력 재생

1. **PowerShell 창 1**에서 Agent를 실행한다. 기존 COM7 Agent가 실행 중이면 해당 창을 사용한다.

   ```powershell
   $project = "$env:USERPROFILE/STM32CubeIDE/workspace_2.2.0/freertos_linorobot2"
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$project/1_start_agent.ps1"
   ```

2. **PowerShell 창 2**에서 플래시하고 RTT를 연다. CubeIDE 디버그 세션은 먼저 종료한다.

   ```powershell
   $project = "$env:USERPROFILE/STM32CubeIDE/workspace_2.2.0/freertos_linorobot2"
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$project/2_flash_and_rtt.ps1"
   ```

3. RTT에 `[LINOROBOT READY]`가 뜨면 **PowerShell 창 3**에서 WSL을 연다.

   ```powershell
   wsl.exe -d Ubuntu-22.04
   ```

   같은 창의 WSL 프롬프트에서 실행한다. `YTY`와 경로를 실제 Windows 사용자와 프로젝트 위치에 맞춘다.

   ```bash
   source /opt/ros/humble/setup.bash
   cd /mnt/c/Users/YTY/STM32CubeIDE/workspace_2.2.0/freertos_linorobot2
   python3 replay.py --output "logs/linorobot2_host_$(date +%Y%m%d_%H%M%S).csv"
   ```

4. 호스트의 `PASS verified=100`과 RTT의 `[BENCH DONE] workload=linorobot2 mode=1 valid=YES` 두 줄을 모두 확인한다. 재실행 시 2번부터 진행하면 MCU의 제어 상태와 응답 번호가 초기화된다.

호스트 CSV의 `host_round_trip_us`는 Twist 발행부터 Odometry 수신까지의 전체 왕복 시간이다. RTT의 `control_total_cycles`와 `publish_total_cycles`는 워밍업 후 100회의 보드 측 합계다. 평균 µs는 각 값을 `hz`와 100으로 나눈 뒤 백만을 곱해 구한다. Odometry 타임스탬프는 응답을 매칭하는 논리적 재생 시간으로 사용한다.
