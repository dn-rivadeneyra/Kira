cmake_minimum_required(VERSION 3.25)
project(Kira VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/FS)
endif()

# Dependencies setup
include(cmake/Dependencies.cmake)

# 1. Platform-Independent Core Library
add_library(kira_core STATIC
    src/core/protocol.cpp
    src/core/registry.cpp
    src/core/executor.cpp
    src/core/worker.cpp
    src/core/pipeline.cpp
    src/core/shutdown_coordinator.cpp
)

target_include_directories(kira_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(kira_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kira_core PUBLIC
    nlohmann_json::nlohmann_json
)

target_compile_features(kira_core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(kira_core PRIVATE /W4 /permissive- /utf-8 /FS)
endif()

# 2. Native Platform Backend Library
if(WIN32)
    add_library(kira_platform STATIC
        src/platform/platform_factory.cpp
        src/platform/windows/windows_app_host.cpp
        src/platform/windows/win32_window.cpp
        src/platform/windows/webview_transport.cpp
        src/platform/windows/security.cpp
        src/platform/windows/string_utils.cpp
    )

    target_include_directories(kira_platform PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_include_directories(kira_platform PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${WEBVIEW2_INCLUDE_DIR}
    )

    target_link_libraries(kira_platform PUBLIC
        kira_core
        "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib"
        user32
        ole32
        shlwapi
        dwmapi
        urlmon
    )

    target_compile_features(kira_platform PUBLIC cxx_std_23)

    if(MSVC)
        target_compile_options(kira_platform PRIVATE /W4 /permissive- /utf-8 /FS)
    endif()
else()
    message(FATAL_ERROR "Kira currently supports Windows only.\nThe platform boundary is prepared for future macOS and Linux backends.")
endif()

# 3. Public Kira Library Target
add_library(kira STATIC
    src/core/app.cpp
)

target_include_directories(kira PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(kira PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kira PUBLIC
    kira_core
    kira_platform
)

target_compile_features(kira PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(kira PRIVATE /W4 /permissive- /utf-8)
endif()

# Automated Testing Suite
enable_testing()

# 1. Protocol Unit Test Target
add_executable(kira_test_protocol
    tests/unit/test_protocol.cpp
)
target_include_directories(kira_test_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kira_test_protocol PRIVATE kira_core)
add_test(NAME test_protocol COMMAND kira_test_protocol)

# 2. Registry Unit Test Target
add_executable(kira_test_registry
    tests/unit/test_registry.cpp
)
target_include_directories(kira_test_registry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kira_test_registry PRIVATE kira_core)
add_test(NAME test_registry COMMAND kira_test_registry)

# 3. Executor Unit Test Target
add_executable(kira_test_executor
    tests/unit/test_executor.cpp
)
target_include_directories(kira_test_executor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kira_test_executor PRIVATE kira_core)
add_test(NAME test_executor COMMAND kira_test_executor)

# 4. Windows Security Unit Test Target (Windows only, no WebView2 runtime needed)
add_executable(kira_test_security
    tests/unit/test_security.cpp
)
target_include_directories(kira_test_security PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kira_test_security PRIVATE kira_platform)
add_test(NAME test_security COMMAND kira_test_security)

# 5. Pipeline Integration Test Target (Platform-Independent using InvocationPipeline)
add_executable(kira_pipeline_tests
    tests/integration/test_pipeline.cpp
)
target_include_directories(kira_pipeline_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kira_pipeline_tests PRIVATE kira_core)
add_test(NAME pipeline_tests COMMAND kira_pipeline_tests)

# 6. Lifecycle Unit Test Target
add_executable(kira_test_lifecycle
    tests/unit/test_lifecycle.cpp
)
target_include_directories(kira_test_lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kira_test_lifecycle PRIVATE kira_core)
add_test(NAME test_lifecycle COMMAND kira_test_lifecycle)

# 7. Fake Host AppImpl Unit Test Target (Platform-Independent using FakeAppHost)
add_executable(kira_test_app_host
    tests/unit/test_app_host.cpp
)
target_include_directories(kira_test_app_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kira_test_app_host PRIVATE kira_core)
add_test(NAME test_app_host COMMAND kira_test_app_host)

# Example Application
add_executable(kira_example WIN32
    examples/main.cpp
)
target_link_libraries(kira_example PRIVATE kira)

# Copy WebView2Loader.dll to build output directory
add_custom_command(TARGET kira_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_DLL_DIR}/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:kira_example>/WebView2Loader.dll"
    COMMENT "Copying WebView2Loader.dll to build directory"
)

# Copy web assets AND single canonical packages/api/kira.js to target build directory
add_custom_command(TARGET kira_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:kira_example>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/packages/api/kira.js"
        "$<TARGET_FILE_DIR:kira_example>/assets/kira.js"
    COMMENT "Copying web assets and canonical kira.js to build directory"
)
