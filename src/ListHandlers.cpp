#include "Handlers.hpp"
#include "Helpers.hpp"
#include "Replies.hpp"
#include <algorithm>
#include <asio.hpp>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

asio::awaitable<std::string> Handlers::rpush(std::span<const std::string_view> data)
{
    if (data.size() < 2)
        co_return Replies::wrongNArgsErr("rpush");

    const std::string key { data.front() };
    const auto [it, inserted] { m_kvStore.try_emplace(key, Value { .data { List {} }, .expiresAt { std::nullopt } }) };
    if (auto& entry { it->second }; !inserted && isExpired(entry))
        entry = { .data { List {} }, .expiresAt { std::nullopt } };

    auto* list { std::get_if<List>(&it->second.data) };
    if (!list)
        co_return std::string { Replies::wrongTypeErr };

    for (const auto elem : data.subspan(1))
        list->emplace_back(elem);

    wakeWaiters(key, data.size() - 1);

    co_return std::format(":{}\r\n", list->size());
}

asio::awaitable<std::string> Handlers::lrange(std::span<const std::string_view> data)
{
    if (data.size() != 3)
        co_return Replies::wrongNArgsErr("lrange");

    const std::string key { data.front() };
    const auto it { findLive(key) };
    if (it == m_kvStore.end())
        co_return std::string { Replies::emptyArray };

    auto start { Helpers::parseNum<std::ptrdiff_t>(data[1]) };
    auto stop { Helpers::parseNum<std::ptrdiff_t>(data[2]) };

    if (!stop || !start)
        co_return std::string { Replies::intOutOfRangeErr };

    const auto* list { std::get_if<List>(&it->second.data) };
    if (!list)
        co_return std::string { Replies::wrongTypeErr };

    const auto listSize { std::ssize(*list) };

    if (*start < 0)
        *start = std::max(*start + listSize, std::ptrdiff_t { 0 });

    if (*stop < 0)
        *stop += listSize;

    if (*start >= listSize)
        co_return std::string { Replies::emptyArray };

    *stop = std::min(listSize - 1, *stop);

    if (*start > *stop)
        co_return std::string { Replies::emptyArray };

    std::string output { std::format("*{}\r\n", *stop - *start + 1) };

    for (; *start <= *stop; ++(*start))
        std::format_to(std::back_inserter(output), "${}\r\n{}\r\n", (*list)[static_cast<std::size_t>(*start)].size(),
                       (*list)[static_cast<std::size_t>(*start)]);

    co_return output;
}

asio::awaitable<std::string> Handlers::lpush(std::span<const std::string_view> data)
{
    if (data.size() < 2)
        co_return Replies::wrongNArgsErr("lpush");

    const std::string key { data.front() };
    const auto [it, inserted] { m_kvStore.try_emplace(key, Value { .data { List {} }, .expiresAt { std::nullopt } }) };

    if (auto& entry { it->second }; !inserted && isExpired(entry))
        entry = { .data { List {} }, .expiresAt { std::nullopt } };

    auto* list { std::get_if<List>(&it->second.data) };
    if (!list)
        co_return std::string { Replies::wrongTypeErr };

    for (const auto item : data.subspan(1))
        list->emplace_front(item);

    wakeWaiters(key, data.size() - 1);

    co_return std::format(":{}\r\n", list->size());
}

asio::awaitable<std::string> Handlers::llen(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        co_return Replies::wrongNArgsErr("llen");

    auto it { findLive(std::string { data[0] }) };
    if (it == m_kvStore.end())
        co_return ":0\r\n";

    const auto* list { std::get_if<List>(&it->second.data) };
    if (!list)
        co_return std::string { Replies::wrongTypeErr };

    co_return std::format(":{}\r\n", list->size());
}

asio::awaitable<std::string> Handlers::lpop(std::span<const std::string_view> data)
{
    if (data.size() < 1 || data.size() > 2)
        co_return Replies::wrongNArgsErr("lpop");

    const std::string key { data[0] };

    auto it { findLive(key) };
    if (it == m_kvStore.end())
        co_return std::string { Replies::nullBulkString };

    auto* list { std::get_if<List>(&it->second.data) };
    if (!list)
        co_return std::string { Replies::wrongTypeErr };

    if (data.size() == 1)
    {
        auto first { list->front() };
        list->pop_front();
        if (list->empty())
            m_kvStore.erase(it);

        co_return std::format("${}\r\n{}\r\n", first.size(), first);
    }

    auto numRemovals { Helpers::parseNum<std::size_t>(data[1]) };
    if (!numRemovals)
        co_return std::string { Replies::intOutOfRangeErr };

    *numRemovals = std::min(*numRemovals, list->size());

    std::string output { std::format("*{}\r\n", *numRemovals) };
    for (std::size_t i { 0 }; i < *numRemovals; ++i)
    {
        std::format_to(std::back_inserter(output), "${}\r\n{}\r\n", list->front().size(), list->front());
        list->pop_front();
    }

    if (list->empty())
        m_kvStore.erase(it);

    co_return output;
}

asio::awaitable<std::string> Handlers::blpop(std::span<const std::string_view> data)
{
    if (data.size() != 2)
        co_return Replies::wrongNArgsErr("blpop");

    const std::string key { data[0] };
    const auto num { Helpers::parseNum<double>(data[1]) };
    if (!num)
        co_return std::string { Replies::intOutOfRangeErr };

    auto executor { co_await asio::this_coro::executor };

    using Clock = asio::steady_timer::clock_type;
    const std::chrono::duration<double> timeout { *num };
    asio::steady_timer t { executor, *num == 0 ? Clock::time_point::max()
                                               : Clock::now() + std::chrono::duration_cast<Clock::duration>(timeout) };

    while (true)
    {

        if (auto it { findLive(key) }; it != m_kvStore.end())
        {
            auto* list { std::get_if<List>(&it->second.data) };
            if (!list)
                co_return std::string { Replies::wrongTypeErr };

            if (!list->empty())
            {
                const auto first { list->front() };
                list->pop_front();
                if (list->empty())
                    m_kvStore.erase(it);

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
