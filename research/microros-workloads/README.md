# 논문 실험용 micro-ROS 애플리케이션 원본

STM32L552의 배열 발행·구독 측정에 더해, 공개 로봇 펌웨어가 실제로 수행하는 계산을 사용할 수 있도록 원본을 추출했다. 각 원본의 커밋과 Git blob ID는 [sources.json](sources.json)에 기록했다. 원본의 라이선스와 저작권 표기를 포함한다. 이 디렉터리는 조사 및 이식 출발점이며, 보드용 전체 프로젝트의 드라이버·빌드 의존성은 각 upstream에 있다.

| 예제 | 실제 코드 경로 | 연구에 쓸 작업 |
|---|---|---|
| Linorobot2, ROS 2 Humble | [micro-ROS 펌웨어](linorobot2-humble/firmware/src/firmware.ino), [운동학](linorobot2-humble/firmware/lib/kinematics/kinematics.cpp), [PID](linorobot2-humble/firmware/lib/pid/pid.cpp), [오도메트리](linorobot2-humble/firmware/lib/odometry/odometry.cpp) | Twist 명령과 엔코더 속도로 바퀴 목표 속도, PWM 명령, 오도메트리를 계산한다. 펌웨어는 Odometry와 Imu를 발행한다. [출처](https://github.com/linorobot/linorobot2_hardware/tree/077cfee58d6f7b9fd376857b104158c1b3b696e3) |
| micro-ROS 공식 MoveIt 2 자세 추정 | [센서·발행 코드](microros-zephyr/apps/attitude_estimator/src/main.c), [센서 융합 C 코드](microros-zephyr/apps/attitude_estimator/src/sensfusion9.c) | 가속도계·자이로·자력계 데이터를 융합해 쿼터니언과 오일러 각을 계산한다. STM32L4 Discovery와 Zephyr에서 동작하는 공식 예제다. [공식 데모](https://micro.ros.org/docs/tutorials/demos/moveit2_demo/) |
| Leo Rover | [STM32 micro-ROS 앱](leo-rover/App/Src/app.cpp), [차동 구동 라이브러리](leo-rover-diff-drive/include/diff_drive_lib/diff_drive_controller.hpp) | 실제 STM32F401 로봇의 Twist 수신, 바퀴 제어, 엔코더 오도메트리와 상태 발행 경로다. 원본의 일부 발행에 쓰이는 사용자 정의 leo_msgs는 현재 opt3 라이브러리에 없다. [펌웨어](https://github.com/LeoRover/leocore_firmware_ros2/tree/5813a352ae34e2fd1e99268683885f605d88cee1) · [제조사 문서](https://docs.fictionlab.pl/leo-rover/1.8/documentation/specification) |
| 센서 및 액추에이터 I/O | [공식 ToF·가속도 예제](microros-zephyr/apps/sensors_publisher/src/main.c), Analog Devices의 [가속도 발행](analog-devices/apps/sensor-pub/src/main.c)과 [DAC 구독](analog-devices/apps/dac-dt-sub/src/main.c) | 센서 측정 발행과 ROS 명령 수신에 적합하다. 원본은 Zephyr의 센서 및 DAC API를 사용한다. [공식 ToF 데모](https://micro.ros.org/docs/tutorials/demos/tof_demo/) · [Analog Devices 원본](https://github.com/analogdevicesinc/zephyr-micro-ros-apps/tree/5c891cc043acfda70e5656f2220eb5b04ad7985e) |

## 첫 이식 범위

Linorobot2의 속도 명령 → 바퀴 제어 → 오도메트리 경로를 첫 번째 워크로드로 삼는다. 현재 STM32 라이브러리에 Twist, Odometry, Imu 헤더가 있다. `firmware.ino`의 `createEntities()`에서 구독·발행·타이머 등록을, `moveBase()`에서 제어 경로를, `publishData()`에서 센서와 오도메트리 발행을 확인할 수 있다. 첫 워크로드는 같은 속도 명령과 엔코더 기록을 각 보호 모드에 재생하고, 실제 micro-ROS 구독·발행과 원본 제어 계산을 수행하는 형태로 잡는다. 입력이 합성 값이면 실험 기록에 합성 입력으로 표시한다.

- **전체 지연**은 호스트에서 속도 명령을 보낸 시점부터 대응하는 Odometry를 받을 때까지 같은 호스트 시계로 측정한다. **앱 실행 시간**은 보드에서 바퀴 속도·PID·오도메트리 계산을 계측한다.
- 세 모드에서 동일 입력, 메시지 타입, 발행 간격, QoS 및 출력 정합성 기준을 적용한다. Baseline·MPU·SFI 각각의 실측 시간과 Baseline 대비 배율을 기록한다.
- 원본의 제어 타이머는 20ms다. 현재 115200 baud UART의 8N1 전송 한도는 이론상 약 11.52kB/s다. Odometry 공분산 두 배열만 576B이므로 50Hz에서 이미 28.8kB/s다. 이식 시 측정 주기를 별도로 정하고, 원본 50Hz와 실험 주기를 함께 기록한다. [원본 타이머](https://github.com/linorobot/linorobot2_hardware/blob/077cfee58d6f7b9fd376857b104158c1b3b696e3/firmware/src/firmware.ino#L191-L198)
- MPU와 SFI는 실제 제어 상태와 입력·결과 버퍼를 보호해야 한다. 현재 SFI 검증기는 정해진 앱 함수와 제한된 명령만 허용한다. 새 함수의 직접 호출, 부동소수점 연산, 64비트 메시지 데이터 접근을 검토하고 보호 경계 안의 최종 ELF를 다시 검증한다. [현재 SFI 검증기](../../opt3/tools/sfi_verify.py)

두 번째 워크로드에는 공식 자세 추정 C 코드를 사용할 수 있다. IMU가 연결되지 않은 실험이라면 센서 기록을 재생하고, 원본 알고리즘의 단위와 샘플 간격을 맞춘다. [BROAD 센서 기록](https://github.com/dlaidig/broad)

## 확인 범위

추출한 파일은 각 upstream Git blob과 바이트 단위로 일치한다. CubeIDE의 ARM GNU 도구로 Linorobot2의 원본 운동학·PID·오도메트리와 공식 자세 추정 알고리즘을 Cortex-M33 오브젝트로 컴파일했다. Linorobot2 컴파일 확인에는 Arduino 상수·범위 제한 함수의 임시 호환 헤더와 현재 micro-ROS 헤더를 사용했다. 전체 펌웨어 링크와 보드의 MPU/SFI 비교는 이식 후 검증할 범위다.
