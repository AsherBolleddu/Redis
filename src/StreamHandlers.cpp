#include "Handlers.hpp"
#include "Replies.hpp"
#include <asio.hpp>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    template <typename... Ts>
    struct Overloaded : Ts...
    {
        using Ts::operator()...;
    };
} // namespace

asio::awaitable<std::string> Handlers::type(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        co_return Replies::wrongNArgsErr("type");

    const std::string key { data[0] };
    auto it { findLive(key) };
    if (it == m_kvStore.end())
        co_return "+none\r\n";

    co_return std::visit(
        Overloaded { [](const std::string&) { return "+string\r\n"; }, [](const List&) { return "+list\r\n"; } },
        it->second.data);
}
