# LowFat + 실제 PUB/SUB: baseline / MPU / SFI 비교

프로젝트 이름: **freertos_app_sfi**. 기본값은 MODE 3(SFI), 직접 접근 포함입니다.

## 유지한 기능

- 실제 micro-ROS Publisher 1개와 Subscription 4개가 같은 토픽으로 통신합니다. ROS Node는 2개입니다.
- FreeRTOS PUB 애플리케이션 태스크 1개와 SUB 애플리케이션 태스크 4개를 실행합니다. Executor는 수신용 4개입니다.
- PUB/SUB 객체는 각각 32바이트 LowFat 슬롯에 인접하게 배치됩니다. SUB의 객체 자체가 실제 등록된 수신 버퍼입니다.
- PUB는 발행 호출에서 돌아온 뒤 SUB 수신 버퍼를 직접 읽는 동작을 수행합니다. 대상은 SUB1~4 순환이며, 이번 SUB 수신 처리 전이므로 OFF에서는 직전 수신 값을 읽습니다.
- 정상 통신, 자기 객체/스택 접근, 다른 앱의 객체/스택 및 커널 접근, 차단 후 정상 통신 복구를 검사합니다.
- LowFat config 파일 및 allocator 콜백 연결은 그대로 유지했습니다. 전역 malloc을 모두 가로채는 구현이라는 뜻은 아닙니다.

## 세 모드

| BENCH_MODE | LowFat | MPU | 보호 방법 |
|---|---|---|---|
| 1 | ON | OFF | 비교용 baseline, 외부 메모리 접근 허용 |
| 2 | ON | ON | 태스크별 MPU 권한, 실제 접근 위반 예외와 제한적인 복구 |
| 3 | ON | OFF | C 앱의 메모리 접근 검사 + 빌드 후 앱 기계어/제어 흐름 검증 |

세 모드의 앱 동작은 동일한 `pubsub_app.c`에서 생성합니다. MODE 3의 객체 읽기/쓰기만 소프트웨어 검사 함수를 통과합니다. SFI를 위해 임의로 대기나 반복을 추가해 느리게 만들지 않았습니다.

## SFI 구현을 정확히 설명하면

1. **할당:** 기존 micro-ROS allocator 콜백 → LowFat. config.h/config.c 분리 방식 유지.
2. **데이터 접근:** 앱의 필드 읽기/쓰기는 MODE 3에서 `sfi_app_read32()` / `sfi_app_write32()`로 연결됩니다. 직접 접근 시험도 `sfi_probe_read()` / `sfi_probe_write()`에서 접근 전에 검사합니다.
3. **권한 판단:** 앱이 전달한 소유자 번호를 신뢰하지 않습니다. 현재 FreeRTOS TCB를 인증하고, 그 태스크의 고정 LowFat 객체와 스택 범위인지 검사합니다. 다른 앱의 유효한 LowFat 객체라도 거부합니다.
4. **검사 우회 방지:** CubeIDE post-build 검증기가 네 C 함수(두 태스크 진입 함수, SUB 처리 함수, 진단 함수)의 기계어를 검사합니다. 검사 없는 포인터 load/store, 임의 간접 분기, 허용하지 않은 함수 호출, 권한 변경 명령, 스택 피벗은 거부합니다.
5. **스택 접근:** 컴파일러가 생성한 스택 지역 변수 접근은 제어 흐름별로 고정 스택 범위와 복귀 균형을 정적으로 검사합니다. 따라서 모든 push/pop에 런타임 검사를 붙인 구현은 아닙니다. 읽기 전용 Flash 상수 로드도 검증 후 허용합니다.
6. **반환 주소 보호:** 간접 쓰기로 현재 실행 중인 호출 스택 프레임을 수정하지 못하게 합니다. saved register/return address를 바꿔 검사 밖으로 점프하는 경로를 막기 위한 SFI의 추가 제약입니다. 스택의 비활성 데이터 영역 접근은 허용합니다.

### 보호 범위와 한계 — 보고 시 반드시 함께 명시

- **검증된 고정 C 앱 집합에 대한 native SFI**입니다. 임의의 새 C 프로그램을 자동 변환하는 범용 SFI 컴파일러는 아닙니다. 앱에 새 접근/명령을 추가하면 계측 및 검증기 재검토가 필요합니다.
- 실제 PUB/SUB 앱의 메모리 소유권 경계는 비교하지만, MPU와 모든 동작이 비트 단위로 동일한 보호 모델은 아닙니다. SFI는 활성 호출 프레임에 대한 간접 쓰기를 추가로 거부합니다. MPU 모드는 자신의 스택에 대한 그런 쓰기를 일반적으로 허용합니다.
- MPU는 위반 명령에서 하드웨어 예외가 발생하고, SFI는 위반 접근 전에 소프트웨어로 거부합니다. 두 차단·복구 방식의 비용은 다릅니다.
- SFI 앱의 Flash 데이터 읽기는 검증된 PC 상대 상수 로드만 허용합니다. 임의 주소를 통한 코드 영역 읽기까지 MPU와 동일하게 허용하는 정책은 아닙니다.
- micro-ROS, allocator, 드라이버, RTOS, SFI 검사 함수와 검증기는 **공통 신뢰 기반**입니다. 미들웨어 전체의 SFI나 PUB/SUB별 미들웨어 인스턴스 격리가 아닙니다. DMA, 신뢰 커널 손상, Flash 변경 공격까지 다루는 실험도 아닙니다.
- MODE 3에서 MPU는 꺼져 있으며 앱은 하드웨어 비특권 경계에 의존하지 않습니다. 소프트웨어 접근 검사와 닫힌 제어 흐름 검증에 의존합니다.
- 빌드 검증을 건너뛴 ELF를 실행하거나 검증 실패 후 강제 Flash하면 SFI 보장을 주장할 수 없습니다.

## 작업 부하를 분리해서 비교

`Core/Inc/benchmark_config.h`에서 설정합니다.

```c
#define BENCH_MODE 3          // 1=baseline, 2=MPU, 3=SFI
#define BENCH_DIRECT_READ 1   // 1=매 발행 후 직접 읽기, 0=정상 통신만
```

- **DIRECT_READ=0:** 정상 통신 경로에 보호를 적용한 비용 비교. PRE/POST 차단 검사는 별도로 실행하되 측정 밖입니다.
- **DIRECT_READ=1:** 기존 opt2 의도대로 매 반복마다 PUB가 실제 SUB 버퍼를 읽습니다. 접근 검사/거부/MPU 예외 복구까지 측정 안에 포함합니다.
- 설정이 다른 로그끼리 비교 도구가 비교를 거부합니다. 두 작업 부하를 한 표에서 섞지 마세요.
- 1회당 공통 서비스 API 진입은 PUB 2회 + SUB 4회 = 6회, 100회에서 600회입니다. MODE 2는 MPU 시스템 호출 경로, MODE 1/3은 직접 호출 경로입니다. 이 값은 모든 SVC/예외의 총 발생 횟수라는 뜻은 아닙니다.
- SFI 접근 검사 카운터도 측정 안에서 갱신되므로 해당 비용이 결과에 포함됩니다.

## 측정 항목

- `pub_request_to_all_done`: PUB 요청부터 SUB 4개 완료까지, 미들웨어 처리/통신 대기 포함.
- `all_dispatch_excluding_middleware`: PUB+SUB 실행 구간 합에서 미들웨어 내부 시간을 제외. 앱 처리, 서비스 호출/태스크 전환, 보호 검사 및 직접 접근을 포함. 순수 MPU 설정 시간이나 순수 SFI 명령 시간만은 아닙니다.
- `mean_us`: 반복 1회 평균. `total_us`: 해당 구간 전체 샘플의 누적 경과 시간.
- 총 추가 시간 = 보호 모드 누적 시간 - baseline 누적 시간. 중첩된 구간들을 서로 더하지 마세요.
- 전체 통신 시간은 Agent/전송 대기 변동에 민감하므로 그 차이를 전부 보호 기법의 비용으로 해석하지 마세요.
- 이전 opt2 로그와 바로 비교하지 말고 **이 프로젝트에서 MODE 1/2/3을 모두 새로 실행**하세요.

## CubeIDE에서 빌드 및 실행

1. ZIP을 풀고 `File > Import > General > Existing Projects into Workspace`에서 `freertos_app_sfi`를 Import합니다. `Copy projects into workspace`를 선택합니다.
2. 기본 DIRECT_READ=1을 유지하고 MODE를 **1 → 2 → 3** 순서로 실행합니다. 설정을 바꿀 때마다 Save → Clean → Debug Build합니다. post-build 검증 PASS까지 확인합니다.
3. Agent 창에서 한 번 실행합니다. 기존 Agent가 정상 동작 중이면 중복 실행하지 마세요.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\YTY\STM32CubeIDE\workspace_2.2.0\freertos_app_sfi\1_start_agent.ps1"
```

4. RTT 창에서 각 빌드마다 실행합니다. CubeIDE 디버그 및 기존 RTT는 종료된 상태여야 합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\YTY\STM32CubeIDE\workspace_2.2.0\freertos_app_sfi\2_flash_and_rtt.ps1"
```

5. 세 모드 모두 `valid=YES` 후 같은 RTT 창에서 비교합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\YTY\STM32CubeIDE\workspace_2.2.0\freertos_app_sfi\3_compare_three.ps1" -DirectRead 1
```

6. 정상 통신만 비교하려면 DIRECT_READ=0으로 바꾸고 MODE 1→2→3을 다시 빌드/실행한 뒤 위 비교 명령의 마지막 숫자를 0으로 바꿉니다.

결과: `logs/attack1_comparison.csv`, `logs/attack0_comparison.csv`.
기본 Python 경로는 기존 환경인 `C:\Users\YTY\pyocd-win\Scripts\python.exe`입니다. SFI 검증에는 CubeIDE에 포함된 `arm-none-eabi-objdump.exe`를 사용합니다. 별도 LLVM 플러그인은 필요하지 않습니다.

## 결과 확인

- 세 모드: LowFat self-test / 32-byte adjacency PASS, SUB1~4 각각 100/100, 오류 0, 복구 PASS.
- 직접 읽기 포함: `[DIRECT READ] measured=100`, MODE1 blocked=0, MODE2/3 blocked=100, 각 SUB 25회.
- 정상 통신만: `[DIRECT READ] measured=0 blocked=0`, PRE/POST 차단 검사는 여전히 PASS.
- MODE3: `[SFI SCOPE] ... MPU=0`, `[SFI COUNTS]`의 실제 검사 횟수 확인.
- `[GATE COUNT] measured_entries=600 ... per_iteration=6 ... PASS`.

빌드·보드 결과와 검증기 음성 테스트 기록은 `validation/`에 포함합니다.

추가 검증용 `tools/test_sfi_verifier.py`는 MODE3 ELF에서 우회 코드가 거부되는지 검사합니다. `tools/test_sfi_runtime.py`는 Unicorn으로 실제 링크된 검사 함수를 실행하는 호스트 테스트이며 별도 `unicorn` Python 패키지가 필요합니다. 이 추가 테스트 패키지는 CubeIDE 빌드나 보드 실행에 필요하지 않습니다.
