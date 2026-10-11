#pragma once

#include "RESP.hpp"
#include <asio.hpp>
#include <asio/steady_timer.hpp>
#include <chrono>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

class Handlers
{
private:
    using List = std::deque<std::string>;
    struct Value
    {
        std::variant<std::string, List> data;
        std::optional<std::chrono::steady_clock::time_point> expiresAt;
    };

    using Store = std::unordered_map<std::string, Value>;

    Store m_kvStore;
    std::unordered_map<std::string, std::deque<asio::steady_timer*>> m_waiters;
    asio::awaitable<std::string> ping(std::span<const std::string_view>);
    asio::awaitable<std::string> echo(std::span<const std::string_view>);
    asio::awaitable<std::string> set(std::span<const std::string_view>);
    asio::awaitable<std::string> get(std::span<const std::string_view>);
    asio::awaitable<std::string> rpush(std::span<const std::string_view>);
    asio::awaitable<std::string> lrange(std::span<const std::string_view>);
    asio::awaitable<std::string> lpush(std::span<const std::string_view>);
    asio::awaitable<std::string> llen(std::span<const std::string_view>);
    asio::awaitable<std::string> lpop(std::span<const std::string_view>);
    asio::awaitable<std::string> blpop(std::span<const std::string_view>);
    asio::awaitable<std::string> type(std::span<const std::string_view>);

    Store::iterator findLive(const std::string& key);
    void wakeWaiters(const std::string& key, std::size_t count);
    static bool isExpired(const Value& entry);

public:
    asio::awaitable<std::string> execute(const RESP::BulkString& request);
};
