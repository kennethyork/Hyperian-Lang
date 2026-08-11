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
    hyperian/game.c
    sqlite/sqlite3.c)
target_include_directories(hyperian_mobile_jni PRIVATE hyperian sqlite)
target_compile_definitions(hyperian_mobile_jni PRIVATE
    HYPERIAN_HAVE_SQLITE3=1
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_DEFAULT_FOREIGN_KEYS=1)
target_compile_options(hyperian_mobile_jni PRIVATE -Wall -Wextra -Wpedantic)
set_source_files_properties(sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(hyperian_mobile_jni android log)
