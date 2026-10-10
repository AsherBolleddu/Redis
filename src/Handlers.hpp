#pragma once

#include "RESP.hpp"
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

class Handlers
{
private:
    std::unordered_map<std::string, std::string> m_kvStore;
    std::string ping(std::span<const std::string_view>);
    std::string echo(std::span<const std::string_view>);
    std::string set(std::span<const std::string_view>);
    std::string get(std::span<const std::string_view>);

public:
    std::string execute(const RESP::BulkString& request);
};
