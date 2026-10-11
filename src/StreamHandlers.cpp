#include "Handlers.hpp"
#include "Helpers.hpp"
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

    std::string newID { data[1] };
    if (newID == "0-0")
        co_return "-ERR The ID specified in XADD must be greater than 0-0\r\n";

    const auto [it, inserted] { m_kvStore.try_emplace(std::string { data.front() }) };
    if (auto& entry { it->second }; inserted || isExpired(entry))
        entry = { .data { Stream {} }, .expiresAt { std::nullopt } };

    auto* stream { std::get_if<Stream>(&it->second.data) };
    if (!stream)
        co_return std::string { Replies::wrongTypeErr };

    auto& idArr { stream->idArr };
    if (idArr.empty())
    {
        idArr.push_back(std::move(newID));
        co_return std::format("${}\r\n{}\r\n", idArr.back().size(), idArr.back());
    }

    std::string_view prevID { idArr.back() };
    if (prevID >= newID)
        co_return "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n";

    const auto splitPrevID { Helpers::parseID(prevID) };
    const auto splitNewID { Helpers::parseID(newID) };
    if (!splitPrevID || !splitNewID)
        co_return "-ERR\r\n";

    // millisecondsTime -> first, sequenceNumber->second
    if (splitPrevID->first == splitNewID->first && splitPrevID->second >= splitNewID->second)
        co_return "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n";

    idArr.push_back(std::move(newID));
    std::span pairs { data.subspan(2) };
    for (std::size_t i { 1 }; i < pairs.size(); i += 2)
        stream->kvPairs.insert_or_assign(std::string { pairs[i - 1] }, pairs[1]);

    co_return std::format("${}\r\n{}\r\n", idArr.back().size(), idArr.back());
}
