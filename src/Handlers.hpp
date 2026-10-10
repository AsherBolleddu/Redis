#pragma once

#include "RESP.hpp"
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

class Handlers
{
private:
    struct Value
    {
        std::string data;
        std::optional<std::chrono::steady_clock::time_point> expiresAt;
    };

    std::unordered_map<std::string, Value> m_kvStore;
    std::string ping(std::span<const std::string_view>);
    std::string echo(std::span<const std::string_view>);
    std::string set(std::span<const std::string_view>);
    std::string get(std::span<const std::string_view>);

public:
    std::string execute(const RESP::BulkString& request);
};
