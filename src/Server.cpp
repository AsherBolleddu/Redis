#include "Server.hpp"
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

asio::awaitable<void> Server::serve()
{

    auto executor { co_await asio::this_coro::executor };
    tcp::acceptor acceptor { executor, { tcp::v6(), m_cfg.PORT } };
    while (true)
    {

        tcp::socket socket { co_await acceptor.async_accept(asio::use_awaitable) };
        asio::co_spawn(executor, handleClient(std::move(socket)), asio::detached);
    }
}

asio::awaitable<void> Server::handleClient(tcp::socket socket)
{
    try
    {
        std::string input {};
        std::string output {};
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

            input.append(chunk.data(), nBytes);

            while (true)
            {
                auto request { RESP::parseRequest(input) };
                if (!request)
                {
                    if (request.error() == RESP::ParseError::Incomplete)
                        break;

                    if (request.error() == RESP::ParseError::Malformed)
                    {
                        constexpr std::string_view protocolError { "-ERR Protocol error\r\n" };
                        output += protocolError;
                        co_await asio::async_write(socket, asio::buffer(output), asio::use_awaitable);
                        output.clear();
                        co_return;
                    }
                }

                if (!request->info)
                {
                    input.erase(0, request->bytesConsumed);
                    continue;
                }

                output += m_handler.execute(*request->info);
                input.erase(0, request->bytesConsumed);
            }

            if (!output.empty())
            {
                co_await asio::async_write(socket, asio::buffer(output), asio::use_awaitable);
                output.clear();
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << std::format("client handler exception: {}\n", e.what());
    }
}
