#include "Handlers.hpp"
#include "RESP.hpp"
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <system_error>
#include <utility>
#include <variant>

namespace
{
    std::string wrongNArgsErr(std::string_view command)
    {
        return std::format("-ERR wrong number of arguments for '{}' command\r\n", command);
    }
} // namespace

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
        long long duration {};
        const auto [ptr, ec] { std::from_chars(durationSV.data(), durationSV.data() + durationSV.size(), duration) };
        if (ec != std::errc {} || ptr != durationSV.data() + durationSV.size())
            return "-ERR value is not an integer or out of range\r\n";

        if (duration <= 0)
            return "-ERR invalid expire time in 'set' command\r\n";

        expiresAt = std::chrono::steady_clock::now() +
                    (unit == "px" ? std::chrono::milliseconds { duration } : std::chrono::seconds { duration });
    }

    Value val { .data { std::string { data[1] } }, .expiresAt { expiresAt } };
    m_kvStore.insert_or_assign(std::string { data.front() }, std::move(val));

    return "+OK\r\n";
}

std::string Handlers::get(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        return wrongNArgsErr("get");

    if (const auto it { m_kvStore.find(std::string { data.front() }) }; it != m_kvStore.end())
    {
        const auto& entry { it->second };
        if (entry.expiresAt && std::chrono::steady_clock::now() > *entry.expiresAt)
            return "$-1\r\n";

        const auto* str { std::get_if<std::string>(&entry.data) };
        if (!str)
            return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

        return std::format("${}\r\n{}\r\n", str->size(), *str);
    }

    return "$-1\r\n";
}

std::string Handlers::rpush(std::span<const std::string_view> data)
{
    if (data.size() != 2)
        return wrongNArgsErr("rpush");

    const auto [it, res] { m_kvStore.insert(
        std::make_pair(data[0], std::vector<std::string> { std::string { data[1] } })) };
    auto* list { std::get_if<std::vector<std::string>>(&it->second.data) };
    if (!list)
        return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

    if (!res)
        list->emplace_back(data[1]);

    return std::format(":{}\r\n", list->size());
}

std::string Handlers::execute(const RESP::BulkString& request)
{
    static const std::unordered_map<std::string, std::string (Handlers::*)(std::span<const std::string_view>)>
        dispatchTable { { "ping", &Handlers::ping },
                        { "echo", &Handlers::echo },
                        { "set", &Handlers::set },
                        { "get", &Handlers::get },
                        { "rpush", &Handlers::rpush } };

    if (const auto it { dispatchTable.find(request.command) }; it != dispatchTable.end())
        return std::invoke(it->second, this, request.data);

    return std::format("-ERR unknown command '{}'\r\n", request.command);
}
