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

### Changed
- Refactored `AppImpl` (`src/core/app_impl.hpp` / `src/core/app.cpp`) to depend exclusively on `kira::platform::AppHost`, eliminating all Win32, COM, WebView2, `HRESULT`, `HWND`, and Windows message types from core files.
- Separated CMake build targets into `kira_core` (platform-independent core), `kira_platform` (native backend), and `kira` (public library).
- Configured CMake build system to fail cleanly with explicit error message on non-Windows platforms.
- Updated `README.md` to document the platform abstraction model and current Windows-only status.

## [0.2.0] - 2026-07-26

### Fixed
- Fixed `wstring_to_string` buffer size bug and consolidated string conversion utilities into `string_utils`.
- Integrated hotfix bundles for window security and transport layer (`kira_window_security_hotfix.zip` and `kira_transport_fix_6a1a231.zip`).
- Addressed blocker issues: zero raw owner captures, script removal, one-shot attach, fail-closed cancel, release thread checks, and `AppShutdownCoordinator`.

### Added
- Complete stabilization pass: deferred shutdown, checked `HRESULT`s, one-shot initialization, and TLS verification.
- Async bootstrap completion, fail-closed navigation, `TransportCallbackState`, and production security gates.
- Refactored Kira framework into safe, versioned, modular C++23 desktop architecture (Win32 + WebView2).
