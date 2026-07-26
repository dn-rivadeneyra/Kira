#include <windows.h>
#include <kira/kira.hpp>
#include <iostream>
#include <chrono>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    kira::AppConfig config;
    config.app_id = "com.kira.example";
    config.window.title = "Kira Framework - Windows C++23 Slice";
    config.window.width = 960;
    config.window.height = 680;
    config.window.resizable = true;

    kira::App app(config);

    // Register example command 'greet(name)'
    app.register_command("greet", [](const nlohmann::json& args) -> nlohmann::json {
        std::string name = args.value("name", "World");
        if (name.empty()) {
            name = "World";
        }
        
        return {
            {"message", "Hello, " + name + "! Greeting from C++23 native backend."},
            {"status", "success"},
            {"backend", "Kira Win32 WebView2 Native Engine"}
        };
    });

    // Register system info example command
    app.register_command("get_info", [](const nlohmann::json& /*args*/) -> nlohmann::json {
        return {
            {"framework", "Kira C++23"},
            {"architecture", "Tauri-like OS WebView"},
            {"os", "Windows Win32"},
            {"cxx_standard", 202302L}
        };
    });

    return app.run();
}
