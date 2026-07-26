# Kira Desktop Application Framework

**Kira** is a lightweight desktop application framework built with Tauri's architecture: an OS WebView frontend paired with a C++23 native backend.

---

## Architecture Overview

Kira enforces a strict private platform boundary separating core logic from platform-specific WebView and windowing APIs:

```text
Public Kira API
       │
       ▼
Platform-independent AppImpl
       │
       ▼
Private platform::AppHost interface
       │
       ▼
WindowsAppHost
       │
       ├── NativeWindow
       ├── WebViewTransport
       ├── SecurityPolicy
       └── Win32 event loop
```

### Key Components

- **`kira_core`**: Platform-independent static library.
  - **`AppImpl`**: Core coordinator managing `AppConfig`, `CommandRegistry`, `WorkerExecutor`, `InvocationPipeline`, `AppShutdownCoordinator`, and interacting strictly via the private `platform::AppHost` interface.
  - **`CommandRegistry`**: Command handler store validating names against `[A-Za-z][A-Za-z0-9_.-]{0,127}`.
  - **`ProtocolCodec`**: Version 1 protocol parser and serializer emitting discriminated results (`type: "invoke"`, `type: "result"`, `type: "protocol_error"`).
  - **`CommandExecutor`**: Command dispatcher masking native exceptions to `command_exception` error codes.
  - **`WorkerExecutor`**: Single serial FIFO worker thread executing commands off the UI thread with clean joining shutdown.
  - **`InvocationPipeline`**: Platform-independent pipeline orchestrating message parsing, command execution, and response delivery.
  - **`AppShutdownCoordinator`**: Production shutdown coordinator enforcing idempotent, single-pass pipeline and worker shutdown.
- **`kira_platform`**: Native platform backend library (Windows implementation).
  - **`platform::AppHost`**: Private platform abstraction interface exposing Kira lifecycle operations (`start`, `post_message`, `show`, `request_close`, `run_event_loop`).
  - **`WindowsAppHost`**: Concrete Windows implementation owning `NativeWindow`, `WebViewTransport`, `SecurityPolicy`, and the Win32 message loop.
  - **`SecurityPolicy`**: Origin normalization using Windows `CreateUri` / `IUri` API.
  - **`WebViewTransport`**: Transactional asynchronous raw UTF-8 IPC transport and top-level `WebMessageReceived` event handler.
  - **`NativeWindow`**: Win32 window manager dispatching UI-thread responses (`WM_USER + 100`) and managing `InitializationGate` document readiness.
- **`kira`**: Public library exposing `kira::App`, `kira::AppConfig`, and `kira::WindowConfig`.

---

## Security & Frame Policy Model

- **Asynchronous Bootstrap Registration**: Native bootstrap installation is asynchronously confirmed through its `AddScriptToExecuteOnDocumentCreated` completion callback before navigation starts.
- **Transactional Script Cleanup**: On attachment failure or `detach()`, registered bootstrap scripts are explicitly removed via `RemoveScriptToExecuteOnDocumentCreated`.
- **Zero Raw-Owner Captures**: Asynchronous COM callbacks operate via `AttachOperationState` and `WindowAsyncState` weak pointer structs. No raw `this` pointer is captured or dereferenced.
- **Fail-Closed Navigation Security**: Top-level origin navigation authorization fails closed (`args->put_Cancel(TRUE)`) if URI normalization fails, args is null, or target origin is unapproved.
- **Top-Level Event Subscription**: Kira subscribes to `ICoreWebView2::WebMessageReceived` on the top-level `CoreWebView2`. Frame `WebMessageReceived` events are not subscribed.
- **Source Origin Authorization**: The sender URL of every received top-level message is validated against the active top-level document URL (`webview_->get_Source`) and approved origin policy.
- **Release-Mode Thread Enforcement**: WebView operations explicitly verify UI-thread affinity (`check_ui_thread()`) in both debug and release builds.
- **Development Mode**: Navigations and IPC messages are validated against the exact configured `dev_url` origin using `CreateUri` / `IUri`.
- **Production Mode**: Production assets are served under the virtual host domain `https://kira.local/` using `SetVirtualHostNameToFolderMapping` (`ICoreWebView2_3`). Direct `file:///` navigation is blocked.
- **Native Bridge Isolation**: The low-level transport bridge is isolated under `window.__KIRA_INTERNAL__`. The public frontend API is exposed via the browser ES module `packages/api/kira.js`.

---

## Prerequisites & Dependencies

- **Supported Platform**: Windows only (Win32 API + Microsoft WebView2 Runtime). Windows is currently the only implemented backend; the platform boundary is prepared for future macOS and Linux backends.
- **Toolchain**: C++23 compiler (MSVC 2022/2026 v143+) and CMake 3.25+.
- **Automated Dependencies**:
  - `nlohmann/json` `v3.11.3` (acquired via CMake `FetchContent` with SHA-256 validation).
  - `Microsoft.Web.WebView2` `1.0.2903.40` SDK (acquired via CMake `FetchContent` with build artifact validation for `WebView2.h`, `WebView2Loader.dll.lib`, and `WebView2Loader.dll`).

---

## Build & Test Instructions

1. **Configure CMake**:
   ```cmd
   cmake -B build -S .
   ```

2. **Compile Core Library, Platform Backend, Tests, and Example**:
   ```cmd
   cmake --build build --config Debug
   ```

3. **Run Automated Unit & Integration Tests**:
   ```cmd
   ctest --test-dir build -C Debug --output-on-failure
   ```
   *(Runs platform-independent tests `test_protocol`, `test_registry`, `test_executor`, `pipeline_tests`, `test_lifecycle`, and `test_app_host` with a fake host, as well as `test_security`, without requiring a visible WebView2 session).*

4. **Run Windows Example Application**:
   ```cmd
   .\build\Debug\kira_example.exe
   ```

5. **Manual Iframe Security Smoke Test Procedure**:
   1. Build and run `kira_example.exe`.
   2. Click the **"Open Iframe Security Smoke Test →"** link on the main page to navigate to `iframe_test.html`.
   3. Click **"Invoke greet from Top-Level"** and confirm the top-level response appears.
   4. Click **"Try invoke greet from Iframe"** inside the child iframe.
   5. Confirm status transitions to `Status: WAITING (Observing for 2.0s...)` and then concludes as `Status: PASS (No frame-1 response received)`.

---

## C++ API Usage

```cpp
#include <windows.h>
#include <kira/kira.hpp>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    kira::App app({
        .window = {
            .title = "Kira Example",
            .width = 1024,
            .height = 768,
            .dev_url = ""
        }
    });

    app.command("greet", [](const nlohmann::json& payload) {
        const std::string name = payload.value("name", "World");
        return nlohmann::json{
            {"message", "Hello, " + name + "!"}
        };
    });

    return app.run();
}
```

---

## JavaScript Frontend API

Import the canonical ES module `kira.js`:

```javascript
import { invoke, KiraError } from './kira.js';

async function greetUser() {
    try {
        const result = await invoke('greet', { name: 'Alice' }, { timeoutMs: 30000 });
        console.log(result.message); // "Hello, Alice!"
    } catch (err) {
        if (err instanceof KiraError) {
            console.error(`IPC Error [${err.code}]: ${err.message}`);
        }
    }
}
```

---

## Current Framework Boundaries

- **Single Window**: The public API supports one primary window per `App` instance.
- **Windows Only**: Windows is currently the only supported platform. Non-Windows CMake configurations fail with an explicit message.
- **Serial Execution**: Native commands execute sequentially on a single FIFO worker thread.
