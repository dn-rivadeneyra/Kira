#pragma once

#include <string>
#include <memory>
#include <functional>
#include "kira/export.hpp"

namespace kira {

struct WindowConfig {
    std::string title = "Kira Application";
    int width = 1024;
    int height = 768;
    bool resizable = true;
    std::string dev_url = ""; // Optional development URL (e.g. http://localhost:5173)
    std::string asset_path = ""; // Optional local HTML file path
};

class NativeWindowImpl; // Forward declaration for platform isolation

class KIRA_API NativeWindow {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    explicit NativeWindow(const WindowConfig& config);
    ~NativeWindow();

    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    bool initialize();
    void show();
    void post_web_message(const std::string& message);
    void set_on_web_message(MessageCallback callback);
    void* get_native_handle() const;

private:
    std::unique_ptr<NativeWindowImpl> impl_;
};

} // namespace kira
