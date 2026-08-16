cmake_minimum_required(VERSION 3.16)
project(DouyinLiveMonitorC C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# 查找 zlib（gzip 解压用）；优先静态链接，生成免依赖单文件
set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)

# 核心库
add_library(dlm_core STATIC
    src/md5.c
    src/xbogus.c
    src/protobuf.c
    src/gzip.c
    src/json.c
    src/json_dom.c
    src/http.c
    src/messages.c
    src/fetcher.c
    src/enter.c
)
target_link_libraries(dlm_core PUBLIC ZLIB::ZLIB)

# 主程序（控制台）
add_executable(dlm src/main.c)
target_link_libraries(dlm PRIVATE dlm_core winhttp ws2_32)

# GUI 版本（Win32 窗口，无控制台）
add_executable(dlm_gui src/gui.c)
target_link_libraries(dlm_gui PRIVATE dlm_core winhttp ws2_32 gdi32)
set_target_properties(dlm_gui PROPERTIES WIN32_EXECUTABLE TRUE)

# 测试
enable_testing()
add_executable(test_xbogus test/test_xbogus.c)
target_link_libraries(test_xbogus dlm_core)
add_test(NAME xbogus COMMAND test_xbogus)

add_executable(test_core test/test_core.c)
target_link_libraries(test_core dlm_core ZLIB::ZLIB)
add_test(NAME core COMMAND test_core)

add_executable(test_json_dom test/test_json_dom.c)
target_link_libraries(test_json_dom dlm_core)
add_test(NAME json_dom COMMAND test_json_dom)
