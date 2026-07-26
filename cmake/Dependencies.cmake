include(FetchContent)

# 1. nlohmann/json (v3.11.3) with TLS verification and SHA-256 URL_HASH validation
message(STATUS "Acquiring nlohmann/json v3.11.3...")
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TLS_VERIFY ON
)
FetchContent_MakeAvailable(json)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/webview2.nupkg")
    FetchContent_Declare(
        webview2_sdk
        URL "${CMAKE_CURRENT_SOURCE_DIR}/webview2.nupkg"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
else()
    FetchContent_Declare(
        webview2_sdk
        URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
        URL_HASH SHA256=5a3bd2be7685af09c5dc4a999b77ffc1fdc20e4a4c8bb253907f6beec42a0d47
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        TLS_VERIFY ON
    )
endif()
FetchContent_MakeAvailable(webview2_sdk)

set(WEBVIEW2_INCLUDE_DIR "${webview2_sdk_SOURCE_DIR}/build/native/include")

# Explicit Architecture Selection & Validation
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SYSTEM_PROC)
string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" VS_PLATFORM)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(SYSTEM_PROC MATCHES "arm64|aarch64" OR VS_PLATFORM MATCHES "arm64")
        set(WEBVIEW2_ARCH "arm64")
    else()
        set(WEBVIEW2_ARCH "x64")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WEBVIEW2_ARCH "x86")
else()
    message(FATAL_ERROR "Unsupported target architecture for WebView2: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

message(STATUS "WebView2 Target Architecture: ${WEBVIEW2_ARCH}")

set(WEBVIEW2_LIB_DIR "${webview2_sdk_SOURCE_DIR}/build/native/${WEBVIEW2_ARCH}")
set(WEBVIEW2_DLL_DIR "${webview2_sdk_SOURCE_DIR}/runtimes/win-${WEBVIEW2_ARCH}/native")

# CMake Build Artifact Verification
if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
    message(FATAL_ERROR "Missing required WebView2 header: ${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
endif()

if(NOT EXISTS "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib")
    message(FATAL_ERROR "Missing required WebView2 import library: ${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib")
endif()

if(NOT EXISTS "${WEBVIEW2_DLL_DIR}/WebView2Loader.dll")
    message(FATAL_ERROR "Missing required WebView2 loader DLL: ${WEBVIEW2_DLL_DIR}/WebView2Loader.dll")
endif()
