#pragma once

#include "src/core/types.hpp"
#include "src/core/registry.hpp"

namespace kira {

class CommandExecutor {
public:
    static InvocationResult execute(const InvocationRequest& request, const CommandRegistry& registry);
};

} // namespace kira
