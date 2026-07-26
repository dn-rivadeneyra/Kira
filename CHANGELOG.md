# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Extracted private platform host interface `kira::platform::AppHost` (`src/platform/app_host.hpp`).
- Added platform-neutral error reporting model `PlatformError` and `PlatformResult` (`src/platform/platform_error.hpp`).
- Added private platform factory `kira::platform::create_app_host` (`src/platform/platform_factory.hpp` / `src/platform/platform_factory.cpp`).
- Added concrete `WindowsAppHost` (`src/platform/windows/windows_app_host.hpp` / `src/platform/windows/windows_app_host.cpp`) owning Win32 message loop, `NativeWindow`, `WebViewTransport`, and security failure routing.
- Added platform-independent unit test suite `test_app_host.cpp` exercising `AppImpl` coordination with a fake `AppHost`.

### Fixed
- Applied terminal callback deferral fix (`kira_terminal_callback_fix.zip`).
- Defer readiness and fatal error callback delivery via custom Windows messages (`WM_KIRA_HOST_READY` and `WM_KIRA_HOST_FATAL`) in `WindowsAppHost::run_event_loop`.
- Prevented native COM/WebView2 and WndProc re-entrancy during terminal callback dispatch.
- Applied platform-boundary stabilization bundle (`Kira-platform-boundary-fix.zip`).
- Ensured non-Windows CMake checks fail prior to acquiring platform dependencies.
- Restored `TLS_VERIFY ON` certificate validation for WebView2 SDK download.
- Separated post-readiness security failure handling to `FatalErrorCallback`.
- Guaranteed one-shot readiness completion and distinct non-zero terminal exit codes for platform runtime failures.
- Enforced Win32 `GetMessage == -1` failure handling as `event_loop_failed`.
- Expanded `test_app_host.cpp` to exercise thread-safe state synchronization and in-flight worker task shutdown sequence.

## [0.2.0] - 2026-07-26

### Fixed
- Fixed `wstring_to_string` buffer size bug and consolidated string conversion utilities into `string_utils`.
- Integrated hotfix bundles for window security and transport layer (`kira_window_security_hotfix.zip` and `kira_transport_fix_6a1a231.zip`).
- Addressed blocker issues: zero raw owner captures, script removal, one-shot attach, fail-closed cancel, release thread checks, and `AppShutdownCoordinator`.

### Added
- Complete stabilization pass: deferred shutdown, checked `HRESULT`s, one-shot initialization, and TLS verification.
- Async bootstrap completion, fail-closed navigation, `TransportCallbackState`, and production security gates.
- Refactored Kira framework into safe, versioned, modular C++23 desktop architecture (Win32 + WebView2).
