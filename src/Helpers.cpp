#include "Helpers.hpp"
#include <utility>

namespace Helpers
{
    std::optional<std::pair<std::string_view, std::string_view>> parseID(std::string_view id)
    {
        auto find { id.find('-') };
        if (find == std::string_view::npos)
            return {};

        return { { id.substr(0, find), id.substr(find + 1) } };
    }
} // namespace Helpers
