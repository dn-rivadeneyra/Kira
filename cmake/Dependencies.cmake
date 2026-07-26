include(FetchContent)

# 1. nlohmann/json (v3.11.3) with pinned SHA-256 URL_HASH
message(STATUS "Acquiring nlohmann/json v3.11.3...")
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TLS_VERIFY OFF
)
FetchContent_MakeAvailable(json)

# 2. Microsoft.Web.WebView2 (1.0.2903.40)
message(STATUS "Acquiring Microsoft.Web.WebView2 1.0.2903.40 SDK...")
FetchContent_Declare(
    webview2_sdk
    URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TLS_VERIFY OFF
)
FetchContent_MakeAvailable(webview2_sdk)

set(WEBVIEW2_INCLUDE_DIR "${webview2_sdk_SOURCE_DIR}/build/native/include")

# Explicit Architecture Mapping
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

if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
    message(FATAL_ERROR "WebView2 SDK headers not found in build tree: ${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
endif()

if(NOT EXISTS "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib")
    message(FATAL_ERROR "WebView2 import library not found: ${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib")
endif()
