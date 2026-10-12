#include "Helpers.hpp"
#include <algorithm>
#include <cctype>
#include <optional>

namespace Helpers
{
    bool isEquals(std::string_view a, std::string_view b)
    {
        return std::ranges::equal(a, b,
                                  [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }

    std::optional<ParsedID> parseID(std::string_view id, unsigned long long defaultSeq)
    {
        if (id == "*")
            return { { std::nullopt, std::nullopt } };

        const auto dash { id.find('-') };
        const auto ms { parseNum<unsigned long long>(id.substr(0, dash)) };
        if (!ms)
            return {};

        if (dash == std::string_view::npos)
            return { { *ms, defaultSeq } };

        const auto seqSv { id.substr(dash + 1) };
        if (seqSv == "*")
            return { { *ms, std::nullopt } };

        const auto seq { parseNum<unsigned long long>(seqSv) };
        if (!seq)
            return {};

        return { { *ms, *seq } };
    }
} // namespace Helpers
