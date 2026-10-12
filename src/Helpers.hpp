#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <string_view>
#include <system_error>

namespace Helpers
{

    bool isEquals(std::string_view a, std::string_view b);

    template <typename T>
    concept StrictNumeric = (std::integral<T> || std::floating_point<T>) && !std::same_as<T, bool> &&
                            !std::same_as<T, char> && !std::same_as<T, wchar_t>;

    template <StrictNumeric T>
    std::optional<T> parseNum(std::string_view sv)
    {
        T num {};
        auto [ptr, ec] { std::from_chars(sv.data(), sv.data() + sv.size(), num) };
        if (ec != std::errc {} || ptr != sv.data() + sv.size())
            return {};

        return num;
    }

    struct ParsedID
    {
        std::optional<unsigned long long> ms;
        std::optional<unsigned long long> seq;
    };

    std::optional<ParsedID> parseID(std::string_view id, unsigned long long defaultSeq = 0);
} // namespace Helpers
