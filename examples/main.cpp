#include <windows.h>
#include <kira/kira.hpp>
#include <iostream>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

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
