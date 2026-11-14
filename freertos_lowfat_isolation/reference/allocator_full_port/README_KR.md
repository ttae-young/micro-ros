# STM32 FreeRTOS용 LowFat 전체 포팅 작업본

이 폴더는 `my_allocator(src)`에 있던 Linux용 LowFat 코드를 그대로 복사한
것이 아니라, 각 기능을 STM32L552와 FreeRTOS에서 실행할 수 있도록 바꾼
작업본이다.

## 포함된 기능

- 크기별 메모리 영역 선택
- 새 블록 순차 할당
- 해제된 블록의 Free List 저장 및 재사용
- `malloc`, `free`, `calloc`, `realloc`
- 포인터의 영역, 객체 시작 주소 및 크기 계산
- 실제 요청 크기를 이용한 범위 검사
- 16바이트 정렬 할당
- `strdup`, `strndup`
- micro-ROS `rcutils_allocator_t` 연결
- FreeRTOS mutex를 이용한 동시 접근 보호

## 보드에서 사용하는 순서

1. `lowfat_malloc_init()`으로 SRAM 영역과 mutex를 초기화한다.
2. `lowfat_install_microros_allocator()`로 micro-ROS 기본 할당기를 바꾼다.
3. 그다음 `rclc_support_init()`과 노드, Publisher, Subscriber, Executor를
   생성한다.
4. 직접 포인터를 읽거나 쓰기 전에는
   `lowfat_check_access(pointer, access_length)`를 호출한다.

```c
if (!lowfat_install_microros_allocator()) {
  Error_Handler();
}

/* 이 호출 뒤부터 micro-ROS 객체를 생성한다. */
rclc_support_init(&support, 0, NULL, &allocator);
```

할당기 연결만으로 C 언어의 모든 포인터 접근이 자동 검사되지는 않는다.
예를 들어 `*target`을 실행하기 전에는 다음과 같은 검사가 필요하다.

```c
if (!lowfat_check_access(target, sizeof(*target))) {
  /* 경계 초과 접근: 기록 후 중단 또는 오류 처리 */
  Error_Handler();
}
value = *target;
```

## 의도적으로 제외된 Linux 기능

`mmap`, `mprotect`, `madvise`, `dlsym`, `LD_PRELOAD`, glibc 내부 할당 함수,
constructor 및 가드 페이지는 MCU에 같은 기능이 없어서 사용할 수 없다.
이 부분은 정적 SRAM 영역, 명시적 초기화, FreeRTOS mutex 및 실행 전 범위
검사로 대체하였다. 4 KiB 페이지 정렬이 필요한 `valloc`과 `pvalloc`은
지원되지 않으며 `NULL`을 반환한다.

## Ubuntu에서 먼저 확인하는 방법

```text
cmake -S . -B build-host -DLOWFAT_HOST_TEST_BUILD=ON
cmake --build build-host
./build-host/lowfat_host_tests
```

보드 프로젝트에서는 `LOWFAT_HOST_TEST_BUILD`를 켜지 않고 FreeRTOS 헤더
경로와 함께 라이브러리를 빌드한다. micro-ROS 어댑터까지 포함할 때는
`LOWFAT_WITH_MICROROS=ON`을 사용한다.
