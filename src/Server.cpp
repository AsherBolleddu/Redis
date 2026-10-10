#include "Server.hpp"
#include "Handlers.hpp"
#include "RESP.hpp"
#include <array>
#include <asio.hpp>
#include <exception>
#include <format>
#include <iostream>
#include <string_view>
#include <utility>

using asio::ip::tcp;

Server::Server(Config cfg) : m_cfg { std::move(cfg) } {}

asio::awaitable<void> Server::serve() const
{

    auto executor { co_await asio::this_coro::executor };
    tcp::acceptor acceptor { executor, { tcp::v6(), m_cfg.PORT } };
    while (true)
    {

        tcp::socket socket { co_await acceptor.async_accept(asio::use_awaitable) };
        asio::co_spawn(executor, handleClient(std::move(socket)), asio::detached);
    }
}

asio::awaitable<void> Server::handleClient(tcp::socket socket) const
{
    try
    {
        std::string buffer {};
        std::array<char, 1024> chunk {};
        while (true)
        {
            auto [ec,
                  nBytes] { co_await socket.async_read_some(asio::buffer(chunk), asio::as_tuple(asio::use_awaitable)) };
            if (ec)
            {
                if (ec != asio::error::eof)
                    std::cerr << std::format("read error: {}\n", ec.message());

                break;
            }

            buffer.append(chunk.data(), nBytes);

            while (true)
            {
                auto request { RESP::parseRequest(buffer) };
                if (!request)
                {
                    if (request.error() == RESP::ParseError::Incomplete)
                        break;

                    if (request.error() == RESP::ParseError::Malformed)
                    {
                        constexpr std::string_view protocolError { "-ERR Protocol error\r\n" };
                        co_await asio::async_write(socket, asio::buffer(protocolError), asio::use_awaitable);
                        co_return;
                    }
                }

                if (!request->info)
                {
                    buffer.erase(0, request->bytesConsumed);
                    continue;
                }

                /*
                 * 1. Find the command in the unordered_map
                 * 2. If it exists, execute the handler associated with that command, and return the output
                 * 3. If it doesn't exist, send -ERR unknown command '{Command}\r\n' and return
                 */
                auto reply { Handlers::execute(*request->info) };
                co_await asio::async_write(socket, asio::buffer(reply), asio::use_awaitable);
                buffer.erase(0, request->bytesConsumed);
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << std::format("client handler exception: {}\n", e.what());
    }
}
