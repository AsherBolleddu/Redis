#include "RESP.hpp"
#include <cctype>
#include <charconv>
#include <cstddef>
#include <expected>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>

namespace RESP
{
    std::expected<std::size_t, ParseError> parseLength(std::string_view& input, char prefix)
    {
        using enum ParseError;

        if (input.empty())
            return std::unexpected { Incomplete };
        if (input[0] != prefix)
            return std::unexpected { Malformed };

        auto pos { input.find("\r\n") };
        if (pos == std::string_view::npos)
            return std::unexpected { Incomplete };

        auto lenSV { input.substr(1, pos - 1) }; // [1, 1 + 2 - 1 = 2)
        std::size_t len {};
        auto [ptr, ec] { std::from_chars(lenSV.data(), lenSV.data() + lenSV.size(), len) };
        if (ec != std::errc {} || ptr != lenSV.data() + lenSV.size())
            return std::unexpected { Malformed };

        input.remove_prefix(pos + 2);
        return len;
    }

    std::expected<std::string_view, ParseError> parseBulkString(std::string_view& input)
    {
        using enum ParseError;

        auto len { parseLength(input, '$') };
        if (!len)
            return std::unexpected { len.error() };

        if (input.size() < *len + 2)
            return std::unexpected { Incomplete };

        auto data { input.substr(0, *len) }; // [0, 0 + 4 = 4)

        input.remove_prefix(*len + 2);
        return data;
    }

    std::expected<Request, ParseError> parseRequest(std::string_view input)
    {
        /*
         * 1. Parse the length (*n)
         * 2. Parse the command ($m\r\nCOMMAND\r\n)
         * 3. Parse the data ($p\r\nDATA\r\n)
         */
        auto originalSize { input.size() };

        auto numElements { parseLength(input, '*') };
        if (!numElements)
            return std::unexpected { numElements.error() };

        if (numElements == 0)
            return Request { .info { std::nullopt }, .bytesConsumed = originalSize - input.size() };

        auto commandSV { parseBulkString(input) };
        if (!commandSV)
            return std::unexpected { commandSV.error() };

        auto commandStr { *commandSV | std::ranges::views::transform([](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }) | std::ranges::to<std::string>() };

        Request request { .info { { .command { std::move(commandStr) } } } };

        for (auto i { 1uz }; i < *numElements; ++i)
        {
            auto data { parseBulkString(input) };
            if (!data)
                return std::unexpected { data.error() };
            request.info->data.push_back(*data);
        }

        request.bytesConsumed = originalSize - input.size();
        return request;
    }
} // namespace RESP
