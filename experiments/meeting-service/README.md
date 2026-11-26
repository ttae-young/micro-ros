# STM32L552 micro-ROS AddTwoInts 서비스

8월 5일 미팅 자료 7쪽에 나온 공식 micro-ROS FreeRTOS 예제 중 `add_two_ints_service`를 사용했다. Windows·WSL Agent와 STM32L552 UART 설정은 현재 opt3에서 가져왔다. 서비스는 `/addtwoints`에서 두 정수를 받아 STM32에서 더한 값을 응답한다. LowFat을 사용하고 MPU·SFI를 끈 통신 확인용 빌드다.

[원본 코드](https://github.com/micro-ROS/freertos_apps/blob/7a426032bd3c33d36d3a6411affc6881522bc05b/apps/add_two_ints_service/app.c)

## GitHub 소스에서 빌드

저장소 루트에서 Python 3.9 이상으로 프로젝트 ZIP을 생성한다. 스크립트는 같은 저장소의 `opt3`에서 STM32L552 보드 지원 파일을 가져와 별도 CubeIDE 프로젝트를 만든다.

```powershell
python .\experiments\meeting-service\build_project.py
```

이 명령으로 만든 ZIP에는 소스가 들어 있다. ZIP을 푼 뒤 CubeIDE의 **File → Import → General → Existing Projects into Workspace**에서 `freertos_meeting_service`를 가져오고 **Debug Build**를 한다. `Debug/freertos_meeting_service.elf`가 생성된 프로젝트 폴더에서 아래 스크립트를 실행한다.

## 실행

1. Debug ELF가 포함된 로컬 배포 ZIP을 사용하면 압축을 푼 프로젝트 폴더에서 바로 시작할 수 있다. Agent가 이미 실행 중이면 그 창을 사용한다. 새로 시작할 때는 프로젝트 폴더의 PowerShell에서 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\1_start_agent.ps1
```

2. 다른 PowerShell 창에서도 같은 프로젝트 폴더로 이동한 뒤 플래시하고 RTT를 연다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\2_flash_and_rtt.ps1
```

3. RTT에 `[SERVICE READY]`가 표시되면 WSL Ubuntu에서 호출한다. ROS 2 Humble에 `ros-humble-example-interfaces` 패키지가 필요하다.

```bash
source /opt/ros/humble/setup.bash
ros2 service call /addtwoints example_interfaces/srv/AddTwoInts "{a: 7, b: 5}"
```

정상 응답은 `sum=12`다. RTT는 첫 요청을 처리한 뒤 `[BENCH DONE]`을 기록하고 종료한다. 보드의 서비스는 계속 실행된다.

기존 실험 PC에서는 패키지를 별도 WSL overlay로 빌드했다. Humble 설정을 읽은 다음 `source /mnt/c/micro-ros/.payload-build/example-interfaces/install/local_setup.bash`를 한 줄 추가하면 된다.
