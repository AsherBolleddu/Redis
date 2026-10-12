#include "Handlers.hpp"
#include "Helpers.hpp"
#include "Replies.hpp"
#include <asio.hpp>
#include <cctype>
#include <chrono>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

asio::awaitable<std::string> Handlers::set(std::span<const std::string_view> data)
{
    if (data.size() != 2 && data.size() != 4)
        co_return Replies::wrongNArgsErr("set");

    std::optional<std::chrono::steady_clock::time_point> expiresAt {};
    if (data.size() == 4)
    {
        const auto unit { data[2] | std::ranges::views::transform([](unsigned char c) {
                              return static_cast<char>(std::tolower(c));
                          }) |
                          std::ranges::to<std::string>() };
        if (unit != "px" && unit != "ex")
            co_return std::string { Replies::syntaxError };

        const auto durationSV { data[3] };
        const auto duration { Helpers::parseNum<long long>(durationSV) };
        if (!duration)
            co_return std::string { Replies::intOutOfRangeErr };

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
        co_return Replies::wrongNArgsErr("get");

    const auto it { findLive(data.front()) };
    if (it == m_kvStore.end())
        co_return std::string { Replies::nullBulkString };

    const auto* str { std::get_if<std::string>(&it->second.data) };
    if (!str)
        co_return std::string { Replies::wrongTypeErr };

    co_return std::format("${}\r\n{}\r\n", str->size(), *str);
}
