#include "Handlers.hpp"
#include "Helpers.hpp"
#include "Replies.hpp"
#include <asio.hpp>
#include <cstddef>
#include <format>
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

    const auto parsed { Helpers::parseID(data[1]) };
    if (!parsed)
        co_return std::string { Replies::invalidStreamID };
    if (parsed->ms == 0 && parsed->seq == 0ULL)
        co_return "-ERR The ID specified in XADD must be greater than 0-0\r\n";

    const auto [it, inserted] { m_kvStore.try_emplace(std::string { data.front() }) };
    if (auto& entry { it->second }; inserted || isExpired(entry))
        entry = { .data { Stream {} }, .expiresAt { std::nullopt } };

    auto* stream { std::get_if<Stream>(&it->second.data) };
    if (!stream)
        co_return std::string { Replies::wrongTypeErr };

    unsigned long long seq {};
    if (parsed->seq)
        seq = *parsed->seq;
    else if (!stream->empty() && stream->back().id.ms == parsed->ms)
        seq = stream->back().id.seq + 1;
    else if (parsed->ms == 0)
        seq = 1;

    const StreamEntry::StreamID newID { parsed->ms, seq };

    if (!stream->empty() && newID <= stream->back().id)
        co_return std::string { Replies::entryIDError };

    StreamEntry entry { .id { newID }, .fields {} };
    const auto pairs { data.subspan(2) };

    for (std::size_t i { 1 }; i < pairs.size(); i += 2)
        entry.fields.emplace_back(pairs[i - 1], pairs[i]);

    stream->push_back(std::move(entry));
    const auto idStr { std::format("{}-{}", newID.ms, newID.seq) };
    co_return std::format("${}\r\n{}\r\n", idStr.size(), idStr);
}
