#include <cassert>
#include <iostream>
#include <stdexcept>
#include "src/core/registry.hpp"
#include "src/core/validator.hpp"

using namespace kira;

void test_name_validation() {
    assert(is_valid_command_name("greet"));
    assert(is_valid_command_name("project.open"));
    assert(is_valid_command_name("audio-render"));
    assert(is_valid_command_name("v1_test"));

    assert(!is_valid_command_name(""));
    assert(!is_valid_command_name("123greet"));
    assert(!is_valid_command_name("hello world"));
    assert(!is_valid_command_name("foo/bar"));
    assert(!is_valid_command_name("foo:bar"));

    std::string long_name(129, 'a');
    assert(!is_valid_command_name(long_name));

    std::cout << "[PASS] test_name_validation" << std::endl;
}

void test_registry_registration() {
    CommandRegistry registry;

    // Successful registration
    registry.register_command("greet", [](const nlohmann::json& payload) {
        return nlohmann::json{{"msg", "hi"}};
    });
    assert(registry.has_command("greet"));

    // Duplicate registration throws invalid_argument
    try {
        registry.register_command("greet", [](const nlohmann::json&) { return nlohmann::json{}; });
        assert(false && "Should have thrown on duplicate command");
    } catch (const std::invalid_argument&) {
        // Expected
    }

    // Invalid name throws invalid_argument
    try {
        registry.register_command("invalid/name", [](const nlohmann::json&) { return nlohmann::json{}; });
        assert(false && "Should have thrown on invalid name");
    } catch (const std::invalid_argument&) {
        // Expected
    }

    // Empty handler throws invalid_argument
    try {
        registry.register_command("valid_name", nullptr);
        assert(false && "Should have thrown on empty handler");
    } catch (const std::invalid_argument&) {
        // Expected
    }

    std::cout << "[PASS] test_registry_registration" << std::endl;
}

int main() {
    test_name_validation();
    test_registry_registration();
    std::cout << "ALL REGISTRY TESTS PASSED." << std::endl;
    return 0;
}
