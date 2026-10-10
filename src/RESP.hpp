#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RESP
{
    enum class ParseError
    {
        Incomplete,
        Malformed,
    };

    struct BulkString
    {
        std::string command;
        std::vector<std::string_view> data;
    };

    struct Request
    {
        std::optional<BulkString> info;
        std::size_t bytesConsumed;
    };

    std::expected<std::size_t, ParseError> parseLength(std::string_view& input, char prefix);
    std::expected<std::string_view, ParseError> parseBulkString(std::string_view& input);
    std::expected<Request, ParseError> parseRequest(std::string_view input);

} // namespace RESP
