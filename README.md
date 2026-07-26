# Kira Desktop Application Framework

**Kira** is a lightweight desktop application framework designed with Tauri's architecture: native OS WebView frontend rendering paired with a high-performance C++23 native backend.

This repository contains the initial Windows vertical slice using Win32 API and Microsoft WebView2.

---

## Key Features

- **No Heavy Bundles**: Uses the native OS WebView2 component instead of bundling Chromium or Node.js.
- **C++23 Native Core**: Pure C++23 standard library backend built with CMake.
- **Platform Isolation**: Clean separation between platform-independent C++ API (`include/kira/`) and platform-specific windowing code (`src/platform/windows/`).
- **Bidirectional JSON IPC**: Asynchronous bridge mapping C++ lambda handlers to JavaScript Promises.
- **JavaScript `invoke(command, args)` API**: Standard promise-based invoke function automatically injected into WebView context.
- **Configurable Development URL**: Easily switch between local web dev server (e.g. Vite/Next.js) or embedded HTML assets.
- **Zero Heavy Dependencies**: No Qt, Electron, Rust, or Boost required.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│              HTML5 / CSS / JavaScript UI                │
│    window.invoke('command', { ... }) => Promise         │
└──────────────────────────┬──────────────────────────────┘
                           │ WebView2 IPC (JSON)
┌──────────────────────────▼──────────────────────────────┐
│                  Kira C++23 Dispatcher                  │
│    app.register_command("command", [](args) { ... });   │
└──────────────────────────┬──────────────────────────────┘
                           │ Native Win32 Engine
┌──────────────────────────▼──────────────────────────────┐
│             Win32 Window / WebView2 Host                │
└─────────────────────────────────────────────────────────┘
```

---

## Prerequisites

- **Windows 10/11**
- **C++23 Compiler**: Microsoft Visual Studio 2022/2026 (MSVC v143+) or Clang/GCC with C++23 support (`/std:c++23`).
- **CMake**: Version 3.25 or higher.
- **Microsoft WebView2 Runtime**: Pre-installed on Windows 11 and updated Windows 10 installations.

---

## Build Instructions

1. **Open Developer Command Prompt or Terminal**:
   Ensure MSVC tools are in your environment (e.g., using Visual Studio Developer Command Prompt).

2. **Configure with CMake**:
   ```bash
   cmake -B build -S .
   ```
   *CMake will automatically fetch `nlohmann/json` and `Microsoft.Web.WebView2` SDK via `FetchContent`.*

3. **Build Target**:
   ```bash
   cmake --build build --config Debug
   ```

4. **Run Example Application**:
   ```bash
   .\build\Debug\kira_example.exe
   ```

---

## C++ API Usage

```cpp
#include <kira/kira.hpp>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    kira::AppConfig config;
    config.window.title = "My Kira App";
    config.window.width = 1024;
    config.window.height = 768;
    // Optional: config.window.dev_url = "http://localhost:5173";

    kira::App app(config);

    // Register a native command
    app.register_command("greet", [](const nlohmann::json& args) -> nlohmann::json {
        std::string name = args.value("name", "World");
        return {
            {"message", "Hello, " + name + "!"}
        };
    });

    return app.run();
}
```

---

## JavaScript IPC Usage

From your frontend HTML/JS:

```javascript
// window.invoke(commandName, payloadObject) -> Promise
async function greetUser() {
    try {
        const response = await window.invoke('greet', { name: 'Alice' });
        console.log(response.message); // "Hello, Alice!"
    } catch (err) {
        console.error('IPC Error:', err);
    }
}
```

---

## License

MIT License
