cmake_minimum_required(VERSION 3.22.1)
project(HyperianMobile LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(hyperian_mobile_jni SHARED
    hyperian_jni.c
    hyperian/mobile.c
    hyperian/runtime.c
    hyperian/bytecode.c
    hyperian/network.c
    hyperian/security.c
    hyperian/desktop.c
    hyperian/game.c)
target_include_directories(hyperian_mobile_jni PRIVATE hyperian)
target_compile_options(hyperian_mobile_jni PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(hyperian_mobile_jni android log)
