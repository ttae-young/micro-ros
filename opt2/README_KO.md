# 실제 PUB/SUB 수신 버퍼 직접 접근 실험

프로젝트: `freertos_app_direct` / 기본 설정: `BENCH_MODE 2` (MPU ON).
이전 trim2 프로젝트와 이름이 달라 별도로 Import할 수 있습니다.

## 이번에 고친 기능

1. PUB와 SUB 4개의 객체를 LowFat으로 각각 32바이트 할당합니다. 슬롯은 인접합니다.
2. 각 SUB 객체의 `message` 자체를 `rclc_executor_add_subscription()`의 수신 버퍼로 등록합니다. 별도 수신 버퍼에서 앱 객체로 복사하던 경로를 없앴습니다.
3. 실제 PUB C 태스크가 메시지를 생성하고 신뢰된 미들웨어 시스템 호출을 통해 발행합니다.
4. 발행 호출이 PUB 태스크로 돌아온 다음, PUB가 SUB의 실제 수신 버퍼를 LDR로 직접 읽습니다. 정상 반복마다 한 번이며 SUB1~4를 순환합니다.
5. MPU OFF에서는 실제 값을 읽습니다. MPU ON에서는 비특권 PUB의 읽기가 MemManage 예외로 차단됩니다. MPU를 끄거나 읽기 권한을 넓히지 않습니다.
6. 정확한 PC·대상 주소·태스크·예외 종류·스택 범위가 일치하는 예상된 접근 위반만 벤치마크용으로 복구합니다. 다른 예외는 실패로 중단합니다.
7. 그 뒤 SUB 4개가 정상 수신하고, 각 SUB C 태스크가 자기 수신 메시지를 처리합니다.

PUB가 직접 읽는 시점은 이번 발행 이후, 이번 SUB 수신 처리 이전입니다. 따라서 OFF에서 읽는 것은 SUB 버퍼의 **직전 수신 값**입니다. 새 메시지 수신 여부는 이어지는 SUB 처리에서 별도로 검사합니다.

## LowFat / 보호 범위

- `Core/Inc/lowfat_config_stm32.h`, `Core/Src/lowfat_config_stm32.c`의 별도 설정 방식은 그대로 유지했습니다.
- MCU용 LowFat 할당기와 micro-ROS allocator 콜백 연결도 유지했습니다. 모든 C 라이브러리 malloc을 전역 후킹했다는 의미는 아닙니다.
- MPU ON에서 앱은 자기 32바이트 객체와 자기 스택을 사용할 수 있습니다. 다른 앱의 객체·스택 및 커널 데이터에 대한 읽기/쓰기는 차단합니다.
- micro-ROS/전송 드라이버는 공통의 신뢰된 미들웨어입니다. 미들웨어 인스턴스 전체를 PUB/SUB별로 분리한 구조는 아닙니다.
- SFI는 포함하지 않았습니다. MODE 3 빌드는 거부합니다.
- 원본 Zephyr 타이머 앱을 그대로 옮긴 것은 아닙니다. FreeRTOS의 실제 PUB/SUB 태스크, SUB 4개, 순차 실행 컨트롤러로 구성한 실험입니다.

## 계측

- 정상 1회: PUB 2회(발행, 완료+다음 대기) + SUB 4회 = 실제 시스템 호출 진입 6회. 100회 측정 시 600회입니다.
- 직접 읽기, MPU 예외와 복귀, 검증 비용은 두 모드 모두 정상 측정 구간 안에 있습니다.
- `pub_request_to_all_done`: PUB 실행 요청부터 SUB 4개 완료까지의 경과 시간. micro-ROS 내부 처리·통신 대기와 직접 접근을 포함합니다. 초기화/워밍업/반복 사이 100ms 휴식/PRE·POST 진단은 제외합니다.
- `all_dispatch_excluding_middleware`: PUB+SUB 실행 구간 합에서 측정한 미들웨어 내부 시간을 뺀 값입니다. 태스크 전환·시스템 호출·앱 처리·직접 접근·예외 복구·벤치마크 검증이 포함되며 순수 MPU 설정 시간은 아닙니다.
- CSV의 `mean_us`는 평균, `total_us`는 해당 구간 전체 샘플의 누적 경과 시간입니다. 총 추가 시간은 ON 누적 시간 - OFF 누적 시간입니다.
- 중첩된 계측 항목을 서로 더하지 마세요. 이전 trim2는 공격이 측정 밖에 있었으므로 이번 값과 단순 속도 개선율로 비교하지 마세요.
- 전체 시간의 ON/OFF 차이는 통신 대기의 변동까지 포함하므로 MPU만의 인과적 비용으로 해석하지 마세요.

## CubeIDE 실행 순서

1. ZIP을 풉니다. CubeIDE에서 `File > Import > General > Existing Projects into Workspace`를 선택합니다.
2. `freertos_app_direct` 폴더를 선택하고 `Copy projects into workspace`를 체크해 Import합니다.
3. `Core/Inc/benchmark_config.h`에서 `#define BENCH_MODE 1`로 저장합니다. `Project > Clean` 후 Debug Build합니다.
4. Agent PowerShell 창에서 아래 명령을 실행합니다. 이미 정상 Agent가 실행 중이면 그대로 사용합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\YTY\STM32CubeIDE\workspace_2.2.0\freertos_app_direct\1_start_agent.ps1"
```

5. 두 번째 PowerShell(RTT) 창에서 실행합니다. CubeIDE 디버그 세션과 이전 RTT는 종료된 상태여야 합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\YTY\STM32CubeIDE\workspace_2.2.0\freertos_app_direct\2_flash_and_rtt.ps1"
```

6. `valid=YES`가 나오면 MODE를 2로 바꾸고 Clean/Build한 뒤, 같은 RTT 명령을 다시 실행합니다. Agent는 계속 켜둡니다.
7. 두 모드 완료 후 RTT 창에서 비교합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\YTY\STM32CubeIDE\workspace_2.2.0\freertos_app_direct\3_compare_overhead.ps1"
```

결과는 `logs/overhead.csv`에 저장됩니다. 소스 변경 후 재빌드하지 않은 ELF는 실행 스크립트가 거부합니다.

## 확인할 로그

- `[LAYOUT] adjacent_32_byte_slots=PASS`
- SUB1~4 각각 `received=100/100`, 오류 카운터 모두 0
- `[GATE COUNT] measured_entries=600 ... per_iteration=6 ... PASS`
- OFF: `[DIRECT READ] measured=100 blocked=0 ... actual_rx_buffer=1 in_timing=1 PASS`
- ON: `[DIRECT READ] measured=100 blocked=100 ... actual_rx_buffer=1 in_timing=1 PASS`
- 각 `[DIRECT TARGET] SUBn measured=25`
- PRE/POST 격리 검사, 앱 5개의 READ+WRITE 정책 검사 및 복구 후 통신 PASS
- `[BENCH DONE] ... valid=YES`

## 주요 수정 위치

- `Core/Src/pubsub_app.c`: PUB 발행 후 직접 읽기, SUB 앱 처리
- `Core/Src/freertos_mpu_demo.c`: 실제 수신 버퍼 등록, 시스템 호출, 예상 접근 위반 검증, 계측/카운터
- `Core/Src/worker_domain.S`: 시스템 호출 진입과 실제 LDR/STR 접근 위치(기존 유지)
- `Core/Inc/worker_domain.h`: 발행 호출을 `APP_PUBLISH`로 명확히 분리

`validation/`에 직접 실행한 보드 로그, 비교 결과와 검증 기록이 있습니다.
