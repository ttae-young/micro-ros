freertos_lowfat_isolation — 핵심 PUB/SUB 격리 실험

1. 무엇을 확인하나
  실제 micro-ROS PUB -> Agent -> SUB 통신을 수행한다.
  MCU LowFat 크기별 할당기로 PUB/SUB 객체를 각각 32바이트 슬롯에 둔다.
  PUB worker가 각 SUB의 실제 수신 메시지 주소를 LDR 명령으로 읽는다.
  MODE 1: 읽기 성공 + SUB 메시지 값 일치.
  MODE 2: 비권한 PUB worker의 읽기가 MPU에 의해 차단.
  예상한 MemManage fault만 복구하고 정상 메시지 통신이 계속되는지 확인한다.

2. 줄인 작업 / 유지한 보호
  fast3를 기반으로 한다. fast4의 fused frame/fixed MAIR 변경은 포함하지 않는다.
  기존 fast3의 커널 O3, 내부 할당기 mutex 경로, 고정 worker dispatch,
  SysTick 수정, 기존 스택 복사 fast path는 유지한다.
  매 통신마다 넣었던 48바이트 실험용 malloc/memset/free를 제거했다.
  이는 정상 PUB/SUB에 필요하지 않던 별도 벤치마크 작업이다.
  할당기 자체와 micro-ROS allocator callbacks, allocator selftest는 유지한다.
  PUB/SUB 객체는 실행 중 주소/크기/수명이 고정되어 free/realloc되지 않는다.
  이 전제 아래 객체의 live/bounds/겹침을 최초 1회 확인하고 반복 검사를 생략한다.
  동적 객체 재할당/크기 변경을 추가한다면 다시 검증해야 한다.
  BENCH_VALIDATE_EACH_MESSAGE=1이면 동일 프로젝트에서 매 메시지 검사를 복원한다.
  어느 설정도 MPU 권한, SVC 검사, 태스크 스택 보호를 제거하지 않는다.
  메시지를 묶지 않고, worker를 privileged로 바꾸지 않는다.

3. 실험 범위
  보호 대상은 비권한 PUB/SUB worker와 각 메시지 객체다.
  rcl_publish/executor/allocator/RTT는 기존처럼 신뢰하는 privileged 서비스가 담당한다.
  micro-ROS 라이브러리 전체나 임의 노드 코드를 비권한 실행하는 구조는 아니다.
  LowFat은 이 프로젝트의 MCU용 크기별 슬롯 할당기와 명시적 검사이며,
  모든 C 메모리 접근을 자동 계측하는 컴파일러 기반 LowFat 전체 구현은 아니다.
  파일 내 고정 worker 전제: 실행 중 worker 삭제/재생성 없음.

4. CubeIDE 가져오기
  ZIP을 C:\iso 같은 짧은 경로에 압축 해제한다.
  File > Import > General > Existing Projects into Workspace.
  Select root directory에서 압축 푼 freertos_lowfat_isolation 폴더 선택.
  Copy projects into workspace 체크 > Finish.
  새 프로젝트 이름은 freertos_lowfat_isolation이다. 기존 프로젝트는 유지한다.
  .ioc Generate Code는 필요 없다. Debug를 선택해 Clean / Build Project.

5. 설정 (Core/Inc/benchmark_config.h)
  BENCH_MODE 1 : LowFat ON, MPU OFF
  BENCH_MODE 2 : LowFat ON, MPU ON (ZIP 기본값)
  BENCH_MODE 0 : 기존 heap 계열 allocator, MPU OFF (선택적 baseline)
  BENCH_SUBSCRIBERS 1~4 : 기본 4
  BENCH_VALIDATE_EACH_MESSAGE 0 : 고정 객체 처음 검증 (기본)
  BENCH_VALIDATE_EACH_MESSAGE 1 : 매 메시지 객체 경계 재검증 (대조군)
  BENCH_OPT3 / BENCH_FAST_PATHS는 기본값 1 유지.
  권장: SUB=4, 반복 검사=0으로 MODE1 -> MODE2를 각각 빌드/플래시.
  추가로 반복 검사=1/0을 같은 MODE에서 비교하면 생략 효과를 볼 수 있다.
  설정을 바꿀 때마다 저장, Clean, Build 후 플래시한다.

6. 창 두 개로 실행 (기존 Windows/WSL/COM7 환경 사용)
  실제 빌드한 workspace 프로젝트 폴더를 탐색기로 연다.
  RUN_1_AGENT.cmd 더블클릭. link + Agent를 이 창에서 유지한다.
  RUN_2_FLASH_RTT.cmd 더블클릭. Debug ELF를 다운로드하고 RTT를 읽는다.
  Agent가 이미 정상 실행 중이면 첫 번째 창을 중복 실행하지 않는다.
  각 .cmd는 자신의 폴더의 .ps1을 실행하므로 Downloads의 다른 복사본과 혼동하지 않는다.
  프로그램을 다시 올릴 때 RTT 창에서 Ctrl+C로 reader를 먼저 끝낸다.
  ST-LINK DEV_USB_COMM_ERR: RTT / CubeIDE Debug / Programmer 연결 종료 후
  ST-LINK USB 재연결하고 재실행. USB 재연결로 COM bridge가 끊겼으면 Agent도 재시작.
  스크립트는 실행 정책을 해당 PowerShell 프로세스에서만 Bypass한다.

7. RTT 순서와 성공 조건
  [LOWFAT SELFTEST] ... PASS
  [COMPARE] mode=... LowFat=... MPU=... subscribers=...
  [CORE] repeated_bounds=0 synthetic_alloc=0 probe_outside_timing=1
  준비 통신 10회
  [ISOLATION PRE] PUB -> SUB1 ... READ ALLOWED / BLOCKED BY MPU ... PASS
  ... 모든 SUB에 대해 한 번씩 (MPU ON은 fault PC/CFSR/CONTROL도 출력)
  정상 통신 100회 시간 측정, SUB마다 received=100/100
  [ISOLATION POST] 모든 SUB 주소에 대해 다시 같은 검사
  [RECOVERY] normal_exchange=PASS
  [BENCH DONE] ... valid=YES
  MPU ON의 CFSR=0x82는 이 실험에서 의도한 데이터 읽기 차단이다.
  READ ALLOWED일 때만 읽은 메시지 값을 출력한다. 차단 시 값은 읽지 못한다.
  fault PC, SUB 주소, 현재 PUB task, 비권한 상태, 스택 범위, fault 종류를 확인한다.
  다른 fault는 [BENCH INVALID: FAULT]로 정지하며 성공으로 처리하지 않는다.

8. 시간 비교 주의
  공격 읽기/fault 복구/RTT 출력/초기 검증은 정상 통신 100회 시간에서 제외된다.
  MPU는 정상 통신 시간에도 계속 활성화되고, 태스크별 매핑이 적용된다.
  bounds_check count=0은 기본값에서 반복 검사를 하지 않는다는 뜻이며
  LowFat 할당이나 MPU를 껐다는 뜻이 아니다.
  alloc_check_free 항목은 정상 통신과 무관한 작업을 제거하여 출력하지 않는다.
  기존 fast3/fast4의 iteration_total과는 작업 구성이 달라 단순 개선율로 쓰지 않는다.
  이 프로젝트 안에서 동일 SUB 수/검사 설정으로 MODE1과 MODE2를 비교한다.
  worker_roundtrip/sub_workers_total: worker 전환을 포함한 처리 비용.
  iteration_total/pub_to_all_done: Agent/USB/WSL 대기 영향까지 포함한다.
  반복 경계 검사는 worker 타이머 밖에 있었으므로 생략만으로 worker ON/OFF 비율이
  1.3~1.5가 되지는 않는다. 그 비율이나 실제 속도 개선은 보드 측정으로 판단한다.
  각 설정을 3회 이상 반복하고 가장 빠른 한 번만 선택하지 않는다.

9. 핵심 코드 위치
  Core/Src/freertos_mpu_demo.c:
    micro_ros_task: 할당/초기 검증, 정상 통신, PRE/POST 직접 읽기 테스트
    publisher_worker_task/isolation_read: 실제 PUB 문맥에서 SUB 주소 LDR
    isolation_test: 읽기 값/차단 여부 검증 (계측 밖)
    BenchmarkFault: 정확한 예상 fault만 복구
    create_workers: 각 worker에 자기 객체 + 자기 스택만 허용
    run_one/subscription_callback: 정상 PUB/SUB 및 시간 측정
  Core/Inc/isolation_fault.h: 예상 fault 허용 조건 (호스트 부정 테스트 포함)
  Core/Src/lowfat_mcu.c: 기존 MCU LowFat 할당기
  Core/Src/bench_fast_worker.c: fast3 신뢰 서비스 내부 고정 worker 경로
  Core/Inc/benchmark_config.h: 실행 설정

10. 검증 범위
  CubeIDE 2.2.0 Debug 6가지 설정 빌드: 오류 0, 경고 0.
  호스트 LowFat/worker/fault 조건 테스트 통과.
  ELF에서 비권한 PUB 코드의 실제 LDR, syscall 대기 경로,
  privileged probe 상태 배치를 확인했다.
  하드웨어에서의 fault 복구/통신/성능은 아직 실행 검증하지 않았다.
  위 성공 조건을 보드 RTT에서 확인해야 실험 완료다.
