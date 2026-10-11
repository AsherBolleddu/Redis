#pragma once

#include <format>
#include <string>
#include <string_view>

namespace Replies
{
    inline std::string wrongNArgsErr(std::string_view command)
    {
        return std::format("-ERR wrong number of arguments for '{}' command\r\n", command);
    }

    inline constexpr std::string_view intOutOfRangeErr { "-ERR value is not an integer or out of range\r\n" };
    inline constexpr std::string_view wrongTypeErr {
        "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"
    };
    inline constexpr std::string_view emptyArray { "*0\r\n" };
    inline constexpr std::string_view nullBulkString { "$-1\r\n" };
} // namespace Replies
