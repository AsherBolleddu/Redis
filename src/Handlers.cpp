#include "Handlers.hpp"
#include "Helpers.hpp"
#include "RESP.hpp"
#include <algorithm>
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
} // namespace

Handlers::Store::iterator Handlers::findLive(const std::string& key)
{
    const auto it { m_kvStore.find(key) };
    if (it == m_kvStore.end())
        return it;

    const auto& entry { it->second };
    if (entry.expiresAt && std::chrono::steady_clock::now() > *entry.expiresAt)
    {
        m_kvStore.erase(it);
        return m_kvStore.end();
    }

    return it;
}

std::string Handlers::ping(std::span<const std::string_view>)
{
    return "+PONG\r\n";
}

std::string Handlers::echo(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        return wrongNArgsErr("echo");

    return std::format("${}\r\n{}\r\n", data.front().size(), data.front());
}

std::string Handlers::set(std::span<const std::string_view> data)
{
    if (data.size() != 2 && data.size() != 4)
        return wrongNArgsErr("set");

    std::optional<std::chrono::steady_clock::time_point> expiresAt {};
    if (data.size() == 4)
    {
        const auto unit { data[2] | std::ranges::views::transform([](unsigned char c) {
                              return static_cast<char>(std::tolower(c));
                          }) |
                          std::ranges::to<std::string>() };
        if (unit != "px" && unit != "ex")
            return "-ERR syntax error\r\n";

        const auto durationSV { data[3] };
        const auto duration { Helpers::parseNum<long long>(durationSV) };
        if (!duration)
            return std::string { intOutOfRangeErr };

        if (*duration <= 0)
            return "-ERR invalid expire time in 'set' command\r\n";

        expiresAt = std::chrono::steady_clock::now() +
                    (unit == "px" ? std::chrono::milliseconds { *duration } : std::chrono::seconds { *duration });
    }

    Value val { .data { std::string { data[1] } }, .expiresAt { expiresAt } };
    m_kvStore.insert_or_assign(std::string { data.front() }, std::move(val));

    return "+OK\r\n";
}

std::string Handlers::get(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        return wrongNArgsErr("get");

    const std::string key { data.front() };
    const auto it { findLive(key) };
    if (it == m_kvStore.end())
        return "$-1\r\n";

    const auto* str { std::get_if<std::string>(&it->second.data) };
    if (!str)
        return std::string { wrongTypeErr };

    return std::format("${}\r\n{}\r\n", str->size(), *str);
}

std::string Handlers::rpush(std::span<const std::string_view> data)
{
    if (data.size() < 2)
        return wrongNArgsErr("rpush");

    const std::string key { data.front() };
    auto it { findLive(key) };
    if (it == m_kvStore.end())
        it = m_kvStore.try_emplace(key, Value { .data { std::vector<std::string> {} }, .expiresAt {} }).first;

    auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        return std::string { wrongTypeErr };

    for (auto elem : data.subspan(1, data.size() - 1))
        list->emplace_back(elem);

    return std::format(":{}\r\n", list->size());
}

std::string Handlers::lrange(std::span<const std::string_view> data)
{
    if (data.size() != 3)
        return wrongNArgsErr("lrange");

    const std::string key { data.front() };
    const auto it { findLive(key) };
    if (it == m_kvStore.end())
        return std::string { emptyArray };

    auto start { Helpers::parseNum<std::ptrdiff_t>(data[1]) };
    auto stop { Helpers::parseNum<std::ptrdiff_t>(data[2]) };

    if (!stop || !start)
        return std::string { intOutOfRangeErr };

    const auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        return std::string { wrongTypeErr };

    const auto listSize { std::ssize(*list) };

    if (*start < 0)
        *start = std::max(*start + listSize, std::ptrdiff_t { 0 });

    if (*stop < 0)
        *stop += listSize;

    if (*start >= listSize)
        return std::string { emptyArray };

    *stop = std::min(listSize - 1, *stop);

    if (*start > *stop)
        return std::string { emptyArray };

    std::string output { std::format("*{}\r\n", *stop - *start + 1) };

    for (; *start <= *stop; ++(*start))
        std::format_to(std::back_inserter(output), "${}\r\n{}\r\n", (*list)[static_cast<std::size_t>(*start)].size(),
                       (*list)[static_cast<std::size_t>(*start)]);

    return output;
}

std::string Handlers::execute(const RESP::BulkString& request)
{
    static const std::unordered_map<std::string, std::string (Handlers::*)(std::span<const std::string_view>)>
        dispatchTable { { "ping", &Handlers::ping }, { "echo", &Handlers::echo },   { "set", &Handlers::set },
                        { "get", &Handlers::get },   { "rpush", &Handlers::rpush }, { "lrange", &Handlers::lrange } };

    if (const auto it { dispatchTable.find(request.command) }; it != dispatchTable.end())
        return std::invoke(it->second, this, request.data);

    return std::format("-ERR unknown command '{}'\r\n", request.command);
}
