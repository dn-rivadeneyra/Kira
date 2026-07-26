include(FetchContent)

# 1. nlohmann/json (v3.11.3)
message(STATUS "Acquiring nlohmann/json v3.11.3...")
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
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

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WEBVIEW2_ARCH "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WEBVIEW2_ARCH "x86")
else()
    set(WEBVIEW2_ARCH "arm64")
endif()

set(WEBVIEW2_LIB_DIR "${webview2_sdk_SOURCE_DIR}/build/native/${WEBVIEW2_ARCH}")
set(WEBVIEW2_DLL_DIR "${webview2_sdk_SOURCE_DIR}/runtimes/win-${WEBVIEW2_ARCH}/native")

if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
    message(FATAL_ERROR "Failed to acquire WebView2 SDK headers.")
endif()
