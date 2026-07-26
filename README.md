# Kira Desktop Application Framework

**Kira** is a lightweight desktop application framework built with Tauri's architecture: an OS WebView frontend paired with a C++23 native backend.

---

## Architecture Overview

Kira enforces clean separation of concerns, process event loop ownership, and thread safety across its components:

- **`App`**: Process event loop owner, readiness manager, production `ShutdownGate` coordinator, and process lifecycle manager.
- **`CommandRegistry`**: Command handler store validating names against `[A-Za-z][A-Za-z0-9_.-]{0,127}` manually without regex.
- **`ProtocolCodec`**: Version 1 protocol parser and serializer emitting discriminated results (`type: "invoke"`, `type: "result"`, `type: "protocol_error"`).
- **`CommandExecutor`**: Command dispatcher masking native exceptions to `command_exception` error codes.
- **`WorkerExecutor`**: Single serial FIFO worker thread executing commands off the UI thread with clean joining shutdown.
- **`InvocationPipeline`**: Private platform-independent pipeline orchestrating parsing, execution, and response delivery.
- **`SecurityPolicy`**: Origin normalization using Windows `CreateUri` / `IUri` API.
- **`WebViewTransport`**: Transactional asynchronous raw UTF-8 IPC transport, native bootstrap script installer with completion callback confirmation, and top-level `WebMessageReceived` event handler.
- **`NativeWindow`**: Win32 window manager dispatching UI-thread responses (`WM_USER + 100`), managing `InitializationGate` document readiness completion, and deferring window close requests (`WM_CLOSE`) to `AppImpl`.

---

## Security & Frame Policy Model

- **Asynchronous Bootstrap Registration**: Native bootstrap installation is asynchronously confirmed through its `AddScriptToExecuteOnDocumentCreated` completion callback before navigation starts.
- **Fail-Closed Navigation Security**: Top-level origin navigation authorization fails closed (`args->put_Cancel(TRUE)`) if URI normalization fails or target origin is unapproved.
- **Top-Level Event Subscription**: Kira subscribes to `ICoreWebView2::WebMessageReceived` on the top-level `CoreWebView2`. Frame `WebMessageReceived` events are not subscribed.
- **Source Origin Authorization**: The sender URL of every received top-level message is validated against the active top-level document URL (`webview_->get_Source`) and the approved origin policy. Messages from unapproved origins or mismatched contexts are rejected before protocol processing.
- **Development Mode**: Navigations and IPC messages are validated against the exact configured `dev_url` origin using `CreateUri` / `IUri` (scheme, host, and port matching). Unapproved top-level navigations are canceled during `NavigationStarting`.
- **Production Mode**: Production assets are served under the virtual host domain `https://kira.local/` using `SetVirtualHostNameToFolderMapping` (`ICoreWebView2_3`). Direct `file:///` navigation is blocked.
- **Native Bridge Isolation**: The low-level transport bridge is isolated under `window.__KIRA_INTERNAL__`. The public frontend API is exposed via the browser ES module `packages/api/kira.js`.

---

## Prerequisites & Dependencies

- **Supported Platform**: Windows only (Win32 API + Microsoft WebView2 Runtime).
- **Toolchain**: C++23 compiler (MSVC 2022/2026 v143+) and CMake 3.25+.
- **Automated Dependencies**:
  - `nlohmann/json` `v3.11.3` (acquired via CMake `FetchContent` with TLS verification and SHA-256 URL_HASH validation).
  - `Microsoft.Web.WebView2` `1.0.2903.40` SDK (acquired via CMake `FetchContent` with TLS verification and build artifact validation for `WebView2.h`, `WebView2Loader.dll.lib`, and `WebView2Loader.dll`).

---

## Build & Test Instructions

1. **Configure CMake**:
   ```cmd
   cmake -B build -S .
   ```

2. **Compile Core Library, Tests, and Example**:
   ```cmd
   cmake --build build --config Debug
   ```

3. **Run Automated Unit & Integration Tests**:
   ```cmd
   ctest --test-dir build -C Debug --output-on-failure
   ```
   *(Runs platform-independent `test_protocol`, `test_registry`, `test_executor`, `test_security`, `pipeline_tests`, and `test_lifecycle` without requiring a visible WebView2 session).*

4. **Run Windows Example Application**:
   ```cmd
   .\build\Debug\kira_example.exe
   ```

5. **Manual Iframe Security Smoke Test Procedure**:
   1. Build and run `kira_example.exe`.
   2. Click the **"Open Iframe Security Smoke Test →"** link on the main page to navigate to `iframe_test.html`.
   3. Click **"Invoke greet from Top-Level"** and confirm the top-level response appears.
   4. Click **"Try invoke greet from Iframe"** inside the child iframe.
   5. Confirm status remains `Status: PASS (No frame-1 response received)` and no native command response is returned to the client.

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
- **Windows Only**: macOS and Linux backends are out of scope.
- **Serial Execution**: Native commands execute sequentially on a single FIFO worker thread.
