#include "Helpers.hpp"
#include <optional>

namespace Helpers
{
    std::optional<ParsedID> parseID(std::string_view id)
    {
        if (id == "*")
            return { { std::nullopt, std::nullopt } };

        const auto dash { id.find('-') };
        const auto ms { parseNum<unsigned long long>(id.substr(0, dash)) };
        if (!ms)
            return {};

        if (dash == std::string_view::npos)
            return { { *ms, 0 } };

        const auto seqSv { id.substr(dash + 1) };
        if (seqSv == "*")
            return { { *ms, std::nullopt } };

        const auto seq { parseNum<unsigned long long>(seqSv) };
        if (!seq)
            return {};

        return { { *ms, *seq } };
    }
} // namespace Helpers
