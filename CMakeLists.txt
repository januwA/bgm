cmake_minimum_required(VERSION 3.0.0)
project(bgm VERSION 0.1.0)

set(vcpkg_ROOT D:/program/vcpkg)
set(CMAKE_TOOLCHAIN_FILE ${vcpkg_ROOT}/scripts/buildsystems/vcpkg.cmake)
set(ffmpeg_DIR ${vcpkg_ROOT}/packages/ffmpeg_x64-windows)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8") 
endif()

set(bgm_PROJECT_NAME bgm)

add_executable(${bgm_PROJECT_NAME} bgm.cpp)
target_include_directories(${bgm_PROJECT_NAME} PRIVATE ${ffmpeg_DIR}/include)
target_link_directories(${bgm_PROJECT_NAME} PRIVATE ${ffmpeg_DIR}/lib)
target_link_libraries(${bgm_PROJECT_NAME} avcodec avdevice avfilter avformat avutil swresample swscale)

# add_executable(demo1 demo1.cpp)
# target_include_directories(demo1 PRIVATE ${ffmpeg_DIR}/include)
# target_link_directories(demo1 PRIVATE ${ffmpeg_DIR}/lib)
# target_link_libraries(demo1 avcodec avdevice avfilter avformat avutil swresample swscale)

# set(dist_DIR "${PROJECT_BINARY_DIR}")
# file(GLOB ffmpeg_DLL "${ffmpeg_DIR}/bin/*.dll")
# add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
#   # COMMAND ${CMAKE_COMMAND} -E make_directory "${dist_DIR}"
#   # COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${dist_DIR}"

#   COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ffmpeg_DLL}" "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>"
# )

# copy dlls 
# file(GLOB ffmpeg_DLL "${ffmpeg_DIR}/bin/*.dll")
# file(COPY ${ffmpeg_DLL} DESTINATION ${CMAKE_BINARY_DIR})

# add_custom_target(asd COMMAND ${CMAKE_COMMAND} -E echo "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")

