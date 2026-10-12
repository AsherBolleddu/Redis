#pragma once

#include "RESP.hpp"
#include <asio.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

class Handlers
{
private:
    struct StringHash
    {
        using is_transparent = void;
        std::size_t operator()(const char* txt) const { return std::hash<std::string_view> {}(txt); }
        std::size_t operator()(const std::string& str) const { return std::hash<std::string> {}(str); }
        std::size_t operator()(std::string_view sv) const { return std::hash<std::string_view> {}(sv); }
    };

    struct StreamEntry
    {
        struct StreamID
        {
            unsigned long long ms;
            unsigned long long seq;
            auto operator<=>(const StreamID&) const = default;
        };

        StreamID id;
        std::vector<std::pair<std::string, std::string>> fields;
    };

    using List = std::deque<std::string>;
    using Stream = std::vector<StreamEntry>;

    struct Value
    {
        std::variant<std::string, List, Stream> data;
        std::optional<std::chrono::steady_clock::time_point> expiresAt;
    };

    using Store = std::unordered_map<std::string, Value, StringHash, std::equal_to<>>;
    Store m_kvStore;

    std::unordered_map<std::string, std::deque<asio::steady_timer*>, StringHash, std::equal_to<>> m_waiters;

private:
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
    asio::awaitable<std::string> xadd(std::span<const std::string_view>);
    asio::awaitable<std::string> xrange(std::span<const std::string_view>);

    Store::iterator findLive(std::string_view key);
    void wakeWaiters(std::string_view key, std::size_t count);
    static bool isExpired(const Value& entry);

public:
    asio::awaitable<std::string> execute(const RESP::BulkString& request);
};
