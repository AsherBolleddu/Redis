#include "Handlers.hpp"
#include "RESP.hpp"
#include <format>
#include <functional>

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
    if (data.size() != 2)
        return wrongNArgsErr("set");

    m_kvStore.insert_or_assign(std::string { data.front() }, data.back());

    return "+OK\r\n";
}

std::string Handlers::get(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        return wrongNArgsErr("get");

    if (auto it { m_kvStore.find(std::string { data.front() }) }; it != m_kvStore.end())
        return std::format("${}\r\n{}\r\n", it->second.size(), it->second);

    return "-1\r\n";
}

std::string Handlers::execute(const RESP::BulkString& request)
{
    static const std::unordered_map<std::string, std::string (Handlers::*)(std::span<const std::string_view>)>
        dispatchTable { { "ping", &Handlers::ping },
                        { "echo", &Handlers::echo },
                        { "set", &Handlers::set },
                        { "get", &Handlers::get } };

    if (auto it { dispatchTable.find(request.command) }; it != dispatchTable.end())
        return std::invoke(it->second, this, request.data);

    return std::format("-ERR unknown command '{}'\r\n", request.command);
}
