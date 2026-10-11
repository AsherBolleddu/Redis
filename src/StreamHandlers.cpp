#include "Handlers.hpp"
#include "Replies.hpp"
#include <asio.hpp>
#include <optional>
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

    auto it { findLive(data.front()) };
    if (it == m_kvStore.end())
        co_return "+none\r\n";

    co_return std::visit(Overloaded { [](const std::string&) { return "+string\r\n"; },
                                      [](const List&) { return "+list\r\n"; },
                                      [](const Stream&) { return "+stream\r\n"; } },
                         it->second.data);
}

asio::awaitable<std::string> Handlers::xadd(std::span<const std::string_view> data)
{
    if (data.size() < 4 || data.size() % 2 != 0)
        co_return Replies::wrongNArgsErr("xadd");

    const auto [it, inserted] { m_kvStore.try_emplace(std::string { data.front() }) };
    if (auto& entry { it->second }; inserted || isExpired(entry))
        entry = { .data { Stream {} }, .expiresAt { std::nullopt } };

    auto* stream { std::get_if<Stream>(&it->second.data) };
    if (!stream)
        co_return std::string { Replies::wrongTypeErr };

    stream->entryID = data[1];
    std::span pairs { data.subspan(2) };

    for (std::size_t i { 1 }; i < pairs.size(); i += 2)
        stream->kvPairs.insert_or_assign(std::string { pairs[i - 1] }, pairs[1]);

    co_return std::format("${}\r\n{}\r\n", stream->entryID.size(), stream->entryID);
}
