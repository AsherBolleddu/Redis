#include "Handlers.hpp"
#include "Helpers.hpp"
#include "Replies.hpp"
#include <algorithm>
#include <asio.hpp>
#include <chrono>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
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
    if (parsed->ms == 0ULL && parsed->seq == 0ULL)
        co_return "-ERR The ID specified in XADD must be greater than 0-0\r\n";

    const auto [it, inserted] { m_kvStore.try_emplace(std::string { data.front() }) };
    if (auto& entry { it->second }; inserted || isExpired(entry))
        entry = { .data { Stream {} }, .expiresAt { std::nullopt } };

    auto* stream { std::get_if<Stream>(&it->second.data) };
    if (!stream)
        co_return std::string { Replies::wrongTypeErr };

    const auto nowMS { std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()) };

    auto ms { static_cast<unsigned long long>(nowMS.count()) };
    if (parsed->ms)
        ms = *parsed->ms;
    else if (!stream->empty() && ms < stream->back().id.ms)
        ms = stream->back().id.ms;

    unsigned long long seq {};
    if (parsed->seq)
        seq = *parsed->seq;
    else if (!stream->empty() && stream->back().id.ms == ms)
        seq = stream->back().id.seq + 1;
    else if (ms == 0)
        seq = 1;

    const StreamEntry::StreamID newID { ms, seq };

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

asio::awaitable<std::string> Handlers::xrange(std::span<const std::string_view> data)
{
    if (data.size() != 3)
        co_return Replies::wrongNArgsErr("xrange");

    const auto it { findLive(data.front()) };
    if (it == m_kvStore.end())
        co_return std::string { Replies::emptyArray };

    constexpr auto maxNum { std::numeric_limits<unsigned long long>::max() };
    const auto start { Helpers::parseID(data[1] == "-" ? "0-0" : data[1]) };
    const auto end { Helpers::parseID(data[2] == "+" ? std::to_string(maxNum) : data[2], maxNum) };
    if (!start || !end || !start->ms || !end->ms || !start->seq || !end->seq)
        co_return std::string { Replies::invalidStreamID };

    const auto* stream { std::get_if<Stream>(&it->second.data) };
    if (!stream)
        co_return std::string { Replies::wrongTypeErr };

    const StreamEntry::StreamID startID { .ms = *start->ms, .seq = *start->seq };
    const StreamEntry::StreamID endID { .ms = *end->ms, .seq = *end->seq };

    const auto first { std::ranges::lower_bound(*stream, startID, {}, &StreamEntry::id) };
    const auto last { std::ranges::upper_bound(*stream, endID, {}, &StreamEntry::id) };
    if (first >= last)
        co_return std::string { Replies::emptyArray };

    const std::size_t length { static_cast<std::size_t>(last - first) };
    std::string output { std::format("*{}\r\n", length) };
    for (auto entry { first }; entry < last; ++entry)
    {
        std::string formattedID { std::format("{}-{}", entry->id.ms, entry->id.seq) };
        std::format_to(std::back_inserter(output), "*2\r\n${}\r\n{}\r\n*{}\r\n", formattedID.size(), formattedID,
                       entry->fields.size() * 2);
        for (const auto& field : entry->fields)
            std::format_to(std::back_inserter(output), "${}\r\n{}\r\n${}\r\n{}\r\n", field.first.size(), field.first,
                           field.second.size(), field.second);
    }

    co_return output;
}
