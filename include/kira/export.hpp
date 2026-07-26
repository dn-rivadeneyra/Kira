#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(KIRA_BUILD_DLL)
        #define KIRA_API __declspec(dllexport)
    #elif defined(KIRA_CONSUME_DLL)
        #define KIRA_API __declspec(dllimport)
    #else
        #define KIRA_API
    #endif
#else
    #define KIRA_API __attribute__((visibility("default")))
#endif
