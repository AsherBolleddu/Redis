#include "Handlers.hpp"
#include "Helpers.hpp"
#include "RESP.hpp"
#include <algorithm>
#include <asio.hpp>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    std::string wrongNArgsErr(std::string_view command)
    {
        return std::format("-ERR wrong number of arguments for '{}' command\r\n", command);
    }

    constexpr std::string_view intOutOfRangeErr { "-ERR value is not an integer or out of range\r\n" };
    constexpr std::string_view wrongTypeErr {
        "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"
    };
    constexpr std::string_view emptyArray { "*0\r\n" };
    constexpr std::string_view nullBulkString { "$-1\r\n" };
} // namespace

bool Handlers::isExpired(const Value& entry)
{
    return entry.expiresAt && std::chrono::steady_clock::now() > *entry.expiresAt;
}

Handlers::Store::iterator Handlers::findLive(const std::string& key)
{
    const auto it { m_kvStore.find(key) };
    if (it == m_kvStore.end())
        return it;

    if (isExpired(it->second))
    {
        m_kvStore.erase(it);
        return m_kvStore.end();
    }

    return it;
}

asio::awaitable<std::string> Handlers::ping(std::span<const std::string_view>)
{
    co_return "+PONG\r\n";
}

asio::awaitable<std::string> Handlers::echo(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        co_return wrongNArgsErr("echo");

    co_return std::format("${}\r\n{}\r\n", data.front().size(), data.front());
}

asio::awaitable<std::string> Handlers::set(std::span<const std::string_view> data)
{
    if (data.size() != 2 && data.size() != 4)
        co_return wrongNArgsErr("set");

    std::optional<std::chrono::steady_clock::time_point> expiresAt {};
    if (data.size() == 4)
    {
        const auto unit { data[2] | std::ranges::views::transform([](unsigned char c) {
                              return static_cast<char>(std::tolower(c));
                          }) |
                          std::ranges::to<std::string>() };
        if (unit != "px" && unit != "ex")
            co_return "-ERR syntax error\r\n";

        const auto durationSV { data[3] };
        const auto duration { Helpers::parseNum<long long>(durationSV) };
        if (!duration)
            co_return std::string { intOutOfRangeErr };

        if (*duration <= 0)
            co_return "-ERR invalid expire time in 'set' command\r\n";

        expiresAt = std::chrono::steady_clock::now() +
                    (unit == "px" ? std::chrono::milliseconds { *duration } : std::chrono::seconds { *duration });
    }

    Value val { .data { std::string { data[1] } }, .expiresAt { expiresAt } };
    m_kvStore.insert_or_assign(std::string { data.front() }, std::move(val));

    co_return "+OK\r\n";
}

asio::awaitable<std::string> Handlers::get(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        co_return wrongNArgsErr("get");

    const std::string key { data.front() };
    const auto it { findLive(key) };
    if (it == m_kvStore.end())
        co_return std::string { nullBulkString };

    const auto* str { std::get_if<std::string>(&it->second.data) };
    if (!str)
        co_return std::string { wrongTypeErr };

    co_return std::format("${}\r\n{}\r\n", str->size(), *str);
}

asio::awaitable<std::string> Handlers::rpush(std::span<const std::string_view> data)
{
    if (data.size() < 2)
        co_return wrongNArgsErr("rpush");

    const std::string key { data.front() };
    const auto [it, inserted] { m_kvStore.try_emplace(
        key, Value { .data { std::vector<std::string> {} }, .expiresAt { std::nullopt } }) };
    if (auto& entry { it->second }; !inserted && isExpired(entry))
        entry = { .data { std::vector<std::string> {} }, .expiresAt { std::nullopt } };

    auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        co_return std::string { wrongTypeErr };

    for (const auto elem : data.subspan(1))
        list->emplace_back(elem);

    if (const auto w { m_waiters.find(key) }; w != m_waiters.end())
    {
        w->second.front()->cancel_one();
        w->second.pop_front();
        if (w->second.empty())
            m_waiters.erase(w);
    }

    co_return std::format(":{}\r\n", list->size());
}

asio::awaitable<std::string> Handlers::lrange(std::span<const std::string_view> data)
{
    if (data.size() != 3)
        co_return wrongNArgsErr("lrange");

    const std::string key { data.front() };
    const auto it { findLive(key) };
    if (it == m_kvStore.end())
        co_return std::string { emptyArray };

    auto start { Helpers::parseNum<std::ptrdiff_t>(data[1]) };
    auto stop { Helpers::parseNum<std::ptrdiff_t>(data[2]) };

    if (!stop || !start)
        co_return std::string { intOutOfRangeErr };

    const auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        co_return std::string { wrongTypeErr };

    const auto listSize { std::ssize(*list) };

    if (*start < 0)
        *start = std::max(*start + listSize, std::ptrdiff_t { 0 });

    if (*stop < 0)
        *stop += listSize;

    if (*start >= listSize)
        co_return std::string { emptyArray };

    *stop = std::min(listSize - 1, *stop);

    if (*start > *stop)
        co_return std::string { emptyArray };

    std::string output { std::format("*{}\r\n", *stop - *start + 1) };

    for (; *start <= *stop; ++(*start))
        std::format_to(std::back_inserter(output), "${}\r\n{}\r\n", (*list)[static_cast<std::size_t>(*start)].size(),
                       (*list)[static_cast<std::size_t>(*start)]);

    co_return output;
}

asio::awaitable<std::string> Handlers::lpush(std::span<const std::string_view> data)
{
    if (data.size() < 2)
        co_return wrongNArgsErr("lpush");

    const std::string key { data.front() };
    const auto [it, inserted] { m_kvStore.try_emplace(
        key, Value { .data { std::vector<std::string> {} }, .expiresAt { std::nullopt } }) };

    if (auto& entry { it->second }; !inserted && isExpired(entry))
        entry = { .data { std::vector<std::string> {} }, .expiresAt { std::nullopt } };

    auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        co_return std::string { wrongTypeErr };

    for (const auto item : data.subspan(1))
        list->emplace(list->begin(), item);

    if (const auto w { m_waiters.find(key) }; w != m_waiters.end())
    {
        w->second.front()->cancel_one();
        w->second.pop_front();
        if (w->second.empty())
            m_waiters.erase(w);
    }

    co_return std::format(":{}\r\n", list->size());
}

asio::awaitable<std::string> Handlers::llen(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        co_return wrongNArgsErr("llen");

    auto it { findLive(std::string { data[0] }) };
    if (it == m_kvStore.end())
        co_return ":0\r\n";

    const auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        co_return std::string { wrongTypeErr };

    co_return std::format(":{}\r\n", list->size());
}

asio::awaitable<std::string> Handlers::lpop(std::span<const std::string_view> data)
{
    if (data.size() < 1 || data.size() > 2)
        co_return wrongNArgsErr("lpop");

    auto it { findLive(std::string { data[0] }) };
    if (it == m_kvStore.end())
        co_return std::string { nullBulkString };

    auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        co_return std::string { wrongTypeErr };

    if (data.size() == 1)
    {
        auto first { list->front() };
        list->erase(list->begin());

        co_return std::format("${}\r\n{}\r\n", first.size(), first);
    }

    auto numRemovals { Helpers::parseNum<int>(data[1]) };
    if (!numRemovals)
        co_return std::string { intOutOfRangeErr };

    std::string output { std::format("*{}\r\n", *numRemovals) };
    for (int i { 0 }; i < numRemovals; ++i)
    {
        std::format_to(std::back_inserter(output), "${}\r\n{}\r\n", list->front().size(), list->front());
        list->erase(list->begin());
    }

    co_return output;
}

asio::awaitable<std::string> Handlers::blpop(std::span<const std::string_view> data)
{
    if (data.size() != 2)
        co_return wrongNArgsErr("blpop");

    const std::string key { data[0] };
    const auto num { Helpers::parseNum<double>(data[1]) };
    if (!num)
        co_return std::string { intOutOfRangeErr };

    auto executor { co_await asio::this_coro::executor };

    using Clock = asio::steady_timer::clock_type;
    const std::chrono::duration<double> timeout { *num };
    asio::steady_timer t { executor, *num == 0 ? Clock::time_point::max()
                                               : Clock::now() + std::chrono::duration_cast<Clock::duration>(timeout) };

    while (true)
    {

        if (auto it { findLive(key) }; it != m_kvStore.end())
        {
            auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
            if (!list)
                co_return std::string { wrongTypeErr };

            if (!list->empty())
            {
                const auto first { list->front() };
                list->erase(list->begin());

                co_return std::format("*{}\r\n${}\r\n{}\r\n${}\r\n{}\r\n", 2, key.size(), std::string_view { key },
                                      first.size(), std::string_view { first });
            }
        }

        m_waiters[key].push_back(&t);
        const auto [ec] { co_await t.async_wait(asio::as_tuple(asio::use_awaitable)) };
        if (!ec)
        {
            if (const auto w { m_waiters.find(key) }; w != m_waiters.end())
            {
                std::erase(w->second, &t);
                if (w->second.empty())
                    m_waiters.erase(w);
            }
            co_return "*-1\r\n";
        }
    }
}

asio::awaitable<std::string> Handlers::execute(const RESP::BulkString& request)
{
    static const std::unordered_map<std::string,
                                    asio::awaitable<std::string> (Handlers::*)(std::span<const std::string_view>)>
        dispatchTable { { "ping", &Handlers::ping },   { "echo", &Handlers::echo },   { "set", &Handlers::set },
                        { "get", &Handlers::get },     { "rpush", &Handlers::rpush }, { "lrange", &Handlers::lrange },
                        { "lpush", &Handlers::lpush }, { "llen", &Handlers::llen },   { "lpop", &Handlers::lpop },
                        { "blpop", &Handlers::blpop } };

    if (const auto it { dispatchTable.find(request.command) }; it != dispatchTable.end())
        co_return co_await std::invoke(it->second, this, request.data);

    co_return std::format("-ERR unknown command '{}'\r\n", request.command);
}
