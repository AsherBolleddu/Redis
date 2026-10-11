#pragma once

#include "RESP.hpp"
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

class Handlers
{
private:
    struct Value
    {
        std::variant<std::string, std::vector<std::string>> data;
        std::optional<std::chrono::steady_clock::time_point> expiresAt;
    };

    using Store = std::unordered_map<std::string, Value>;

    Store m_kvStore;
    std::string ping(std::span<const std::string_view>);
    std::string echo(std::span<const std::string_view>);
    std::string set(std::span<const std::string_view>);
    std::string get(std::span<const std::string_view>);
    std::string rpush(std::span<const std::string_view>);
    std::string lrange(std::span<const std::string_view>);
    std::string lpush(std::span<const std::string_view>);
    std::string llen(std::span<const std::string_view>);

    Store::iterator findLive(const std::string& key);
    static bool isExpired(const Value& entry);

public:
    std::string execute(const RESP::BulkString& request);
};
