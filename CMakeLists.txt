cmake_minimum_required(VERSION 3.0.0)
project(bgm VERSION 0.1.0)

set(vcpkg_ROOT D:/program/vcpkg)
set(CMAKE_TOOLCHAIN_FILE ${vcpkg_ROOT}/scripts/buildsystems/vcpkg.cmake)
set(ffmpeg_DIR ${vcpkg_ROOT}/packages/ffmpeg_x64-windows)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8") 
endif()

set(bgm_PROJECT_NAME bgm)
add_executable(${bgm_PROJECT_NAME} bgm.cpp)
target_include_directories(${bgm_PROJECT_NAME} PRIVATE ${ffmpeg_DIR}/include)
target_link_directories(${bgm_PROJECT_NAME} PRIVATE ${ffmpeg_DIR}/lib)

set(link_LIBS avcodec avdevice avfilter avformat avutil swresample swscale)

if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  list(APPEND link_LIBS dl pthread m)

  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    list(APPEND link_LIBS atomic)
  endif()

endif()


target_link_libraries(${bgm_PROJECT_NAME} ${link_LIBS})

# copy dlls 
# file(GLOB ffmpeg_DLL "${ffmpeg_DIR}/bin/*.dll")
# file(COPY ${ffmpeg_DLL} DESTINATION ${CMAKE_BINARY_DIR})

