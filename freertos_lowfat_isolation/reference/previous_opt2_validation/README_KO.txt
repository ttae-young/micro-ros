최적화 버전: 먼저 START_OPT2_KO.txt를 읽으세요. 기본 MODE=1, SUB=4.
STM32L552 / FreeRTOS / micro-ROS: 기본 / LowFat / LowFat+MPU
==========================================================

1. 변경 내용
이전 "노드마다 4KB를 떼어 주는 bump allocator"를 제거했습니다.
보관된 allocator_full_port의 실제 LowFat MCU 포트를 사용합니다.
크기별 영역 선택, 해제 블록 재사용, 주소에서 객체 시작/크기 계산,
실제 요청 크기 기반 경계 검사, calloc/realloc/free가 포함됩니다.
micro-ROS의 allocate/deallocate/reallocate/zero_allocate에 연결했습니다.
기본 모드도 같은 수/크기의 객체를 동적으로 할당합니다.
매 회 48바이트 할당→검사(LowFat 모드)→쓰기→해제 실험을 수행합니다.
PUB/SUB worker 실행 직전에도 명시적으로 LowFat 범위 검사를 합니다.
MPU 모드는 같은 LowFat 코드에 제한된 worker 실행을 적용합니다.

2. CubeIDE에 넣기
1) ZIP을 C:\lf 에 풉니다. C:\lf\freertos_lowfat_opt2\.project가 보여야 합니다.
2) File → Import → General → Existing Projects into Workspace.
3) Select root directory: C:\lf\freertos_lowfat_opt2
4) "Copy projects into workspace" 체크 해제 → Finish.
   수정/빌드/플래시가 같은 폴더를 사용하게 하기 위함입니다.
5) Build Configurations → Set Active → Debug.
6) Core/Inc/benchmark_config.h에서 아래 두 값만 수정합니다.
   #define BENCH_MODE 0
   #define BENCH_SUBSCRIBERS 1
7) Project → Clean → Build Project. 0 errors 확인.
   Debug/freertos_lowfat_opt2.elf가 생깁니다.

FreeRTOS, HAL, 기존 micro-ROS 정적 라이브러리가 모두 포함되어 있습니다.
라이브러리를 다시 만들 필요는 없습니다.
.ioc는 원래 하드웨어 설정 참고용입니다. CubeMX 코드 재생성은 하지 마세요.
수동 설정한 FreeRTOS/링커 설정이 덮어써질 수 있습니다.

3. 비교 설정
mode 0: 기본 heap-4 방식 할당기, LowFat 검사 없음, MPU OFF.
mode 1: 복구한 LowFat MCU 할당기 + 명시적 검사, MPU OFF.
mode 2: mode1과 같은 할당기/검사 + MPU ON.
SUBSCRIBERS=1~4: SUB node 하나에 생성하는 독립 subscription 수.
처음에는 subscriber=1을 고정하고 mode=0 → 1 → 2 순서로 실행하세요.
각 변경 후 Clean + Build가 필요합니다. 실행 중 설정 변경이 아닙니다.

4. 실행: 기존 COM7 / WSL Ubuntu-22.04 환경
각 명령을 위에서 아래로 한 줄씩 실행하세요.
기존 bridge/link/agent가 실행 중이면 중복 실행하지 마세요.

창1 (계속 열어두기):
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
& "C:\lf\freertos_lowfat_opt2\1_start_bridge.ps1"
COM7 bridge listening on TCP 8888 확인.

창2 (계속 열어두기):
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
& "C:\lf\freertos_lowfat_opt2\3_start_link.ps1"
successfully connected 확인.

창3 (계속 열어두기):
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
& "C:\lf\freertos_lowfat_opt2\4_start_agent.ps1"

창4 (보드에 올리고 RTT 읽기):
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
& "C:\lf\freertos_lowfat_opt2\2_flash_and_rtt.ps1"

다음 모드:
창4에서 Ctrl+C → 설정 변경 → Clean/Build → 창4 스크립트 재실행.
창1~3은 유지합니다. CubeIDE Debug는 종료해 ST-LINK 충돌을 피하세요.

workspace에 프로젝트를 복사했다면 실제 빌드된 ELF를 지정합니다:
& "C:\lf\freertos_lowfat_opt2\2_flash_and_rtt.ps1" -Firmware "C:\Users\YTY\STM32CubeIDE\workspace_2.2.0\freertos_lowfat_opt2\Debug\freertos_lowfat_opt2.elf"

COM 번호가 바뀌면 serial_tcp_bridge.py의 PORT를 수정합니다.
WSL 호스트 주소가 바뀌면 serial_tcp_bridge.py의 bind 주소와
3_start_link.ps1의 TCP 주소를 동일한 Windows 호스트 주소로 수정합니다.
도우미는 기존 Python/Agent 설치 경로를 그대로 사용합니다.

5. 성공 확인
[COMPARE] mode=... LowFat=... MPU=... subscribers=...
LowFat 모드: [LOWFAT SELFTEST] base/size/bounds/realloc/free PASS
ROS_setup_allocator_calls=양수
allocator ... failures=0 invalid_frees=0
SUB1 received=100/100 (N개 모두)
[BENCH DONE] ... valid=YES

자체 검사는 17바이트 요청 객체의 18번째 바이트 접근을 API가 거절하는지
확인합니다. 실제 잘못된 메모리 쓰기는 하지 않습니다.
범위 검사 실패/예상치 못한 MPU fault는 성공 처리하지 않습니다.
할당 실패는 BENCH ERROR와 failures/last_request로 출력합니다.
다른 힙으로 몰래 우회하지 않습니다.
기본 모드 bounds_check count=0은 정상입니다.

6. CSV 의미
DWT CPU 사이클로 측정, 동일 Debug(-O2) 빌드 조건입니다.
alloc_check_free: 48바이트 malloc→(LowFat 검사)→쓰기→free 전체.
  실제 ROS 통신과 구별되는 별도 소형 할당 실험입니다.
bounds_check: 명시적으로 호출한 LowFat 검사 한 번의 시간.
  정상 count=100*(subscriber 수+2). 기본 모드는 0.
worker_roundtrip: PUB worker 깨우기→작업→대기→호출자 복귀.
sub_workers_total: 모든 SUB worker 구간의 합.
publish_call: rcl_publish 호출.
pub_to_all_done: publish 시작부터 모든 SUB worker 완료까지.
iteration_total: 할당 실험, PUB 검사/worker, publish, 모든 SUB 완료를
  처리하는 spin 반환까지. 마지막 100ms 대기 제외.
allocator_init_cycles: 힙 초기화 1회 비용.
object_alloc_cycles: PUB 및 N개 SUB 객체 할당 비용.

mode1 - mode0: 이 실험의 LowFat MCU 할당/검사 적용 경로 추가 비용.
mode2 - mode1: 같은 LowFat에서 MPU 활성화 경로 추가 비용.
MPU 차이는 RTOS 문맥 전환/시스템 호출도 포함합니다.
MPU 레지스터 쓰기만의 시간이 아닙니다.
bounds_check는 alloc_check_free/iteration_total에 포함된 구간입니다.
서로 합산하면 중복입니다. 중첩 타이머·통계 기록 비용은 차감하지 않았습니다.
순수 알고리즘 명령 수가 아닌 계측된 경로 비용입니다.
timer_pair_cycles도 함께 출력합니다.
통신 시간은 Windows/WSL/Agent/직렬 전송 지연을 포함합니다.
통신 수치만으로 LowFat이 더 빠르다고 결론내리지 마세요.
각 조건을 같은 환경에서 5회 이상 반복하고 분산도 함께 보고하세요.
이전 고정 4KB 영역 결과와 섞지 말고 세 조건 모두 새로 측정하세요.

7. 적용 범위 / 보고서에서 쓸 정확한 표현
권장 명칭: "LowFat MCU 포트(크기별 할당 + 명시적 경계 검사)"
보관된 MCU 포트를 실제 적용한 것입니다. 데스크톱 LowFat의 컴파일러
자동 계측, mmap/guard page까지 이식한 버전은 아닙니다.
콜백을 사용하는 micro-ROS 동적 할당은 LowFat을 사용합니다.
FreeRTOS 커널 힙과 라이브러리 정적 버퍼는 그대로입니다.
모든 C 접근/미리 컴파일된 라이브러리 내부 접근에 자동 검사하지 않습니다.
LowFat 검사는 객체 범위/할당 상태를 확인하며 노드 소유권은 판단하지 않습니다.
다른 살아 있는 객체를 가리키는 포인터 자체를 막는 검사가 아닙니다.
MPU는 비특권 PUB/SUB worker의 자기 32바이트 객체와 스택 접근을 제한합니다.
micro-ROS 서비스/콜백/할당기는 privileged입니다.
노드 전체를 별도 보호 프로세스로 실행하는 완전한 격리 실험은 아닙니다.

기본: micro-ROS heap 64KiB, SRAM2.
LowFat: 13개 클래스 * 4KiB = 52KiB payload pool, SRAM2.
클래스: 16/32/48/64/80/96/112/128/256/512/1024/2048/4096바이트.
추가 requested-size 메타데이터 13KiB(ARM 32bit), SRAM1.
FreeRTOS kernel heap 64KiB는 모든 모드에서 별도 유지.
동일 payload heap 용량 비교는 아닙니다. 메모리 비용도 함께 보고하세요.
클래스 고갈 또는 4096바이트 초과 요청은 실패합니다.
실제 보드 ROS workload의 최대 요청/실패 여부는 RTT로 확인해야 합니다.
PUB/SUB 모두 32바이트를 요청하고 MPU도 정확히 32바이트를 매핑합니다.
기존의 노드마다 4KB 배정하는 구조는 제거했습니다.
SUB 1~4는 번들 RMW 설정을 유지하기 위한 실험 제한이며 MPU 하드웨어의
이론적 최대 subscriber 수를 뜻하지 않습니다.

8. 코드 위치
Core/Inc/benchmark_config.h: 모드/SUB 수.
Core/Inc/FreeRTOSConfig.h: configENABLE_MPU ← BENCH_MPU_ENABLED.
Core/Src/microros_allocators.c: 기본 heap / LowFat 콜백, 호출 통계.
Core/Src/lowfat_mcu.c: 복구한 할당/해제/주소/경계 검사.
Core/Src/lowfat_config_stm32.c: 크기 클래스.
Core/Src/freertos_mpu_demo.c:
  micro_ros_task: 콜백 등록/ROS 초기화/측정/출력.
  guard: LowFat 검사.
  allocation_sample: 할당→검사→쓰기→해제 측정.
  create_workers: mode2 MPU 객체 매핑.
  subscription_callback: N개 SUB 처리.
STM32L552ZETXQ_FLASH.ld: SRAM2 allocator pool.
reference/allocator_full_port: 복구한 원본 그대로 보관 (빌드 대상 아님).
tests: 실제 Core/Src 소스에 대한 호스트 테스트.

원본 대비 변경: 클래스/영역 용량 확대, SRAM2 배치/NOLOAD 초기화,
콜백 연결/진단. size-class/free-list/base/bounds 알고리즘 유지.
원본 adapter의 rcutils_set_default_allocator 반환값 비교 오류를 피하고
bool 성공 여부를 직접 검사합니다.

9. 검증
BUILD_VALIDATION_OPT.txt 참고.
CubeIDE 빌드/호스트 테스트와 실제 보드 실측은 구분합니다.
새 펌웨어의 Agent 연결 및 100회 통신은 보드에서 확인해야 합니다.


