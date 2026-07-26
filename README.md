# Kira Desktop Application Framework

**Kira** is a lightweight desktop application framework built with Tauri's architecture: an OS WebView frontend paired with a C++23 native backend.

---

## Architecture Overview

Kira enforces clean separation of concerns and thread safety across its core components:

- **`App`**: Process event loop owner, readiness state machine manager, and application lifecycle controller.
- **`CommandRegistry`**: Command handler store validating names against `[A-Za-z][A-Za-z0-9_.-]{0,127}`.
- **`ProtocolCodec`**: Version 1 protocol parser and serializer emitting discriminated results.
- **`CommandExecutor`**: Command dispatcher masking native exceptions to `command_exception` error codes.
- **`WorkerExecutor`**: Single serial FIFO worker thread executing commands off the UI thread.
- **`WebViewTransport`**: Raw UTF-8 IPC message transport and origin security gatekeeper.
- **`NativeWindow`**: Win32 window manager dispatching responses back to the UI thread via custom window messages.

---

## Security Model

- **Development Mode**: Navigation and IPC messages are validated against the exact configured `dev_url` origin (scheme, host, and port matching). Unapproved origins and frame navigations are blocked.
- **Production Mode**: Production assets are served under the virtual host domain `https://kira.local/` using WebView2 virtual host folder mapping. Navigation to external or unapproved origins is blocked before commitment.
- **Native Bridge Isolation**: The low-level transport bridge is isolated under `window.__KIRA_INTERNAL__`. The public frontend API is exposed via the browser ES module `packages/api/kira.js`.

---

## Prerequisites & Dependencies

- **Supported Platform**: Windows only (Win32 API + Microsoft WebView2 Runtime).
- **Toolchain**: C++23 compiler (MSVC 2022/2026 v143+) and CMake 3.25+.
- **Automated Dependencies**:
  - `nlohmann/json` `v3.11.3` (acquired via CMake `FetchContent` into build tree).
  - `Microsoft.Web.WebView2` `1.0.2903.40` SDK (acquired via CMake `FetchContent` into build tree).

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

3. **Run Automated Unit & Pipeline Tests**:
   ```cmd
   ctest --test-dir build --output-on-failure
   ```
   *(Runs platform-independent `kira_unit_tests` and `kira_pipeline_tests` without requiring a WebView2 UI session).*

4. **Run Windows Example Application**:
   ```cmd
   .\build\Debug\kira_example.exe
   ```

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
