# CMakeLists.txt - GStreamer Media Framework
cmake_minimum_required(VERSION 3.10)
project(gstreamer_media_framework VERSION 1.0.0 LANGUAGES C)

# 设置 C 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# 查找依赖包
find_package(PkgConfig REQUIRED)

# GStreamer 依赖
pkg_check_modules(GST REQUIRED
    gstreamer-1.0>=1.14
    gstreamer-pbutils-1.0>=1.14
    gstreamer-video-1.0>=1.14
    gstreamer-audio-1.0>=1.14
)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${GST_INCLUDE_DIRS}
)

# 链接目录
link_directories(
    ${GST_LIBRARY_DIRS}
)

# 源文件
set(CORE_SOURCES
    src/core/media_types.c
    src/core/media_controller.c
)

set(MODULE_SOURCES
    src/modules/player.c
    src/modules/recorder.c
    src/modules/transcoder.c
)

set(UTIL_SOURCES
    src/utils/logger.c
    src/utils/error.c
)

set(ALL_SOURCES
    ${CORE_SOURCES}
    ${MODULE_SOURCES}
    ${UTIL_SOURCES}
    src/main.c
)

# 创建可执行文件
add_executable(media_framework ${ALL_SOURCES})

# 链接库
target_link_libraries(media_framework
    ${GST_LIBRARIES}
    pthread
    m
    dl
    rt
)

# 添加编译定义
target_compile_definitions(media_framework PRIVATE
    _GNU_SOURCE
    PACKAGE_NAME="${PROJECT_NAME}"
    PACKAGE_VERSION="${PROJECT_VERSION}"
)

# 安装目标
install(TARGETS media_framework DESTINATION bin)

# 安装头文件
install(DIRECTORY include/ DESTINATION include/media_framework)

# 创建静态库（可选）
add_library(media_framework_lib STATIC
    ${CORE_SOURCES}
    ${MODULE_SOURCES}
    ${UTIL_SOURCES}
)

target_link_libraries(media_framework_lib
    ${GST_LIBRARIES}
    pthread
    m
)

install(TARGETS media_framework_lib DESTINATION lib)

# 测试（可选）
option(BUILD_TESTS "Build test programs" OFF)

if(BUILD_TESTS)
    enable_testing()
    
    add_executable(test_player tests/test_player.c)
    target_link_libraries(test_player media_framework_lib)
    
    add_executable(test_recorder tests/test_recorder.c)
    target_link_libraries(test_recorder media_framework_lib)
    
    add_test(NAME test_player COMMAND test_player)
    add_test(NAME test_recorder COMMAND test_recorder)
endif()

# 打印配置信息
message(STATUS "")
message(STATUS "GStreamer Media Framework Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  GStreamer version: ${GST_VERSION}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
