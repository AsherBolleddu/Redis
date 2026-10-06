#include "Server.hpp"
#include <array>
#include <asio.hpp>
#include <iostream>
#include <string_view>
#include <utility>

using asio::ip::tcp;

namespace
{
    asio::awaitable<void> pingHandler(tcp::socket client)
    {
        try
        {
            std::array<char, 1024> chunk {};
            while (true)
            {
                auto [ec, nBytes] { co_await client.async_read_some(asio::buffer(chunk),
                                                                    asio::as_tuple(asio::use_awaitable)) };
                if (ec)
                {
                    if (ec != asio::error::eof)
                        std::cerr << std::format("read error: {}\n", ec.message());
                    co_return;
                }

                constexpr std::string_view PONG { "+PONG\r\n" };
                co_await asio::async_write(client, asio::buffer(PONG), asio::use_awaitable);
            }
        }
        catch (const asio::system_error& e)
        {
            std::cerr << "Code: " << e.code() << ", Message: " << e.what() << '\n';
        }
    }
} // namespace

Server::Server(Config cfg) : m_cfg { std::move(cfg) } {}

asio::awaitable<void> Server::serve()
{

    auto executor { co_await asio::this_coro::executor };
    tcp::acceptor acceptor { executor, { tcp::v6(), m_cfg.PORT } };
    while (true)
    {

        tcp::socket client { co_await acceptor.async_accept(asio::use_awaitable) };
        asio::co_spawn(executor, pingHandler(std::move(client)), asio::detached);
    }
}
