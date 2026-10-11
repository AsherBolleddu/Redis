#include "Handlers.hpp"
#include "RESP.hpp"
#include "Replies.hpp"
#include <asio.hpp>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

bool Handlers::isExpired(const Value& entry)
{
    return entry.expiresAt && std::chrono::steady_clock::now() > *entry.expiresAt;
}

void Handlers::wakeWaiters(std::string_view key, std::size_t count)
{
    const auto it { m_waiters.find(key) };
    if (it == m_waiters.end())
        return;

    for (; count > 0 && !it->second.empty(); --count)
    {
        it->second.front()->cancel_one();
        it->second.pop_front();
    }

    if (it->second.empty())
        m_waiters.erase(it);
}

Handlers::Store::iterator Handlers::findLive(std::string_view key)
{
    const auto it { m_kvStore.find(key) };
    if (it == m_kvStore.end())
        return it;

    if (isExpired(it->second))
    {
        m_kvStore.erase(it);
        return m_kvStore.end();
    }

    return it;
}

asio::awaitable<std::string> Handlers::ping(std::span<const std::string_view>)
{
    co_return "+PONG\r\n";
}

asio::awaitable<std::string> Handlers::echo(std::span<const std::string_view> data)
{
    if (data.size() != 1)
        co_return Replies::wrongNArgsErr("echo");

    co_return std::format("${}\r\n{}\r\n", data.front().size(), data.front());
}

asio::awaitable<std::string> Handlers::execute(const RESP::BulkString& request)
{
    static const std::unordered_map<std::string,
                                    asio::awaitable<std::string> (Handlers::*)(std::span<const std::string_view>)>
        dispatchTable { { "ping", &Handlers::ping },   { "echo", &Handlers::echo },   { "set", &Handlers::set },
                        { "get", &Handlers::get },     { "rpush", &Handlers::rpush }, { "lrange", &Handlers::lrange },
                        { "lpush", &Handlers::lpush }, { "llen", &Handlers::llen },   { "lpop", &Handlers::lpop },
                        { "blpop", &Handlers::blpop }, { "type", &Handlers::type },   { "xadd", &Handlers::xadd } };

    if (const auto it { dispatchTable.find(request.command) }; it != dispatchTable.end())
        co_return co_await std::invoke(it->second, this, request.data);

    co_return std::format("-ERR unknown command '{}'\r\n", request.command);
}
