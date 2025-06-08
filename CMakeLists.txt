cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

project(dfix)
set(CMAKE_C_FLAGS "-O3 -fuse-ld=lld -fno-common -fno-record-gcc-switches -DNDEBUG -mcrc32 -static -fomit-frame-pointer -fno-asynchronous-unwind-tables -fno-unwind-tables -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-s")
set(CMAKE_CXX_FLAGS "-O3 -fuse-ld=lld -lstdc++ -fno-common -fno-record-gcc-switches -DNDEBUG -mcrc32 -static -fomit-frame-pointer -fno-unwind-tables -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,-s")

add_library(dfix SHARED
            src/main.cpp
            src/impl.cpp
            src/impl.h
            src/log.h
            src/d3d11.def
            src/util.h
            src/shaders/snow.hpp)

set(minhook "${CMAKE_CURRENT_SOURCE_DIR}/lib/minhook")

add_subdirectory(${minhook})
# todo msvc
#target_compile_options(dfix PRIVATE -Wall -Wuse-after-free=3 -Wextra -Wshadow -Wnon-virtual-dtor -Wpedantic -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wpedantic -Wconversion -Wsign-conversion -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference -Wuseless-cast -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough -Weffc++)
# remove warnings for third party libraries
target_compile_options(minhook PRIVATE -w)

target_include_directories(dfix SYSTEM PRIVATE ${minhook})

target_link_libraries(dfix PRIVATE minhook)

# rename dll
set_target_properties(dfix PROPERTIES PREFIX "")
set_target_properties(dfix PROPERTIES OUTPUT_NAME "d3d11")
