#include <cassert>
#include <iostream>
#include "src/platform/windows/security.hpp"
#include "src/platform/windows/string_utils.hpp"

using namespace kira;

void test_string_utils_conversion() {
    // 1. ASCII string round-trip
    std::string ascii_str = "https://kira.local/index.html";
    std::wstring ascii_wstr = string_to_wstring(ascii_str);
    assert(ascii_wstr == L"https://kira.local/index.html");
    assert(wstring_to_string(ascii_wstr) == ascii_str);

    // 2. Non-ASCII multi-byte UTF-8 expansion string round-trip
    std::string multibyte_str = "https://kira.local/test/pfad/n/sub";
    std::wstring multibyte_wstr = string_to_wstring(multibyte_str);
    std::string converted_back = wstring_to_string(multibyte_wstr);
    assert(converted_back == multibyte_str);

    // 3. Verify wstring_to_string correctly converts non-ASCII URLs where str.size() > wstr.size()
    std::wstring non_ascii_wurl = L"https://\x4F8B.local"; // L"https://例.local"
    std::string utf8_converted = wstring_to_string(non_ascii_wurl);
    assert(utf8_converted.size() > non_ascii_wurl.size()); // str.size() > wstr.size()
    assert(string_to_wstring(utf8_converted) == non_ascii_wurl);

    std::cout << "[PASS] test_string_utils_conversion" << std::endl;
}

void test_security_origin_matching() {
    WindowConfig prod_config{.dev_url = ""};
    WindowConfig dev_config{.dev_url = "http://localhost:5173"};

    // 1. Default port normalization
    assert(SecurityPolicy::is_approved_origin("http://localhost:5173/index.html?query=1#frag", dev_config));
    assert(SecurityPolicy::is_approved_origin("http://localhost:5173", dev_config));

    // Production virtual host mapping
    assert(SecurityPolicy::is_approved_origin("https://kira.local/index.html", prod_config));
    assert(SecurityPolicy::is_approved_origin("https://kira.local:443/app/page", prod_config));

    // 2. Mismatched ports
    assert(!SecurityPolicy::is_approved_origin("http://localhost:8080", dev_config));

    // 3. Mismatched schemes
    assert(!SecurityPolicy::is_approved_origin("https://localhost:5173", dev_config));

    // 4. Prefix / subdomain tricks rejected
    assert(!SecurityPolicy::is_approved_origin("https://kira.local.evil.example/", prod_config));
    assert(!SecurityPolicy::is_approved_origin("http://sub.localhost:5173", dev_config));
    assert(!SecurityPolicy::is_approved_origin("http://localhost.attacker.com:5173", dev_config));

    // 5. Userinfo URLs rejected
    auto userinfo_opt = SecurityPolicy::parse_and_normalize_origin("http://user:pass@localhost:5173/");
    assert(!userinfo_opt.has_value());

    // 6. Paths and fragments ignored
    auto p1 = SecurityPolicy::parse_and_normalize_origin("https://kira.local/path/1?a=b#hash");
    auto p2 = SecurityPolicy::parse_and_normalize_origin("https://kira.local:443");
    assert(p1.has_value() && p2.has_value());
    assert(SecurityPolicy::matches_origin(*p1, *p2));

    std::cout << "[PASS] test_security_origin_matching" << std::endl;
}

int main() {
    test_string_utils_conversion();
    test_security_origin_matching();
    std::cout << "ALL SECURITY POLICY TESTS PASSED." << std::endl;
    return 0;
}
