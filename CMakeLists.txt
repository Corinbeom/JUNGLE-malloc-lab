cmake_minimum_required(VERSION 3.16)
project(malloc_lab C)

set(CMAKE_C_STANDARD 99)

# 소스 파일들 추가
add_executable(malloc_lab
        clock.c
        fcyc.c
        fsecs.c
        ftimer.c
        mdriver.c
        memlib.c
        mm.c
)

# 헤더 포함 디렉토리
include_directories(.)
