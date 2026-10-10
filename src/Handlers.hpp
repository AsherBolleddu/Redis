#pragma once

#include "RESP.hpp"
#include <string>

namespace Handlers
{
    std::string execute(const RESP::BulkString& request);
} // namespace Handlers
