cmake_minimum_required(VERSION 3.25)
project(Kira VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(WARNING "Kira current vertical slice only supports Windows (Win32 + WebView2).")
endif()

# Local Third Party Dependencies
set(JSON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(WEBVIEW2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/include")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WEBVIEW2_ARCH "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WEBVIEW2_ARCH "x86")
else()
    set(WEBVIEW2_ARCH "arm64")
endif()

set(WEBVIEW2_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/build/native/${WEBVIEW2_ARCH}")
set(WEBVIEW2_DLL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2/runtimes/win-${WEBVIEW2_ARCH}/native")

# Core Kira Library
add_library(kira STATIC
    src/core/app.cpp
    src/core/ipc.cpp
    src/platform/windows/win32_window.cpp
)

target_include_directories(kira PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${JSON_INCLUDE_DIR}
    ${WEBVIEW2_INCLUDE_DIR}
)

target_link_libraries(kira PUBLIC
    "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib"
    user32
    ole32
    shlwapi
    dwmapi
)

target_compile_features(kira PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(kira PRIVATE /W4 /permissive- /utf-8)
endif()

# Example Application
add_executable(kira_example WIN32
    examples/main.cpp
)

target_link_libraries(kira_example PRIVATE kira)

# Copy WebView2Loader.dll next to executable
add_custom_command(TARGET kira_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_DLL_DIR}/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:kira_example>/WebView2Loader.dll"
    COMMENT "Copying WebView2Loader.dll to build directory"
)

# Copy web assets next to executable
add_custom_command(TARGET kira_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:kira_example>/assets"
    COMMENT "Copying web assets to target build directory"
)
