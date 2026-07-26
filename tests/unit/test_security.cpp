#include <cassert>
#include <iostream>
#include "src/platform/windows/security.hpp"

using namespace kira;

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
    test_security_origin_matching();
    std::cout << "ALL SECURITY POLICY TESTS PASSED." << std::endl;
    return 0;
}
