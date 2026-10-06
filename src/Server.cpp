#include "Server.hpp"
#include <array>
#include <asio.hpp>
#include <iostream>
#include <utility>

Server::Server(Config cfg, asio::io_context& ioCtx)
    : m_cfg { std::move(cfg) }, m_ioCtx { ioCtx }, m_acceptor { m_ioCtx, tcp::endpoint { tcp::v6(), m_cfg.PORT } }
{
}

void Server::serve()
{
    std::cout << "Waiting for a client to connect...\n";

    tcp::socket client { m_ioCtx };
    m_acceptor.accept(client);

    std::cout << "Client connected\n";

    while (true)
    {
        std::array<char, 1024> chunk {};
        client.read_some(asio::buffer(chunk));
        const std::string reply { "+PONG\r\n" };
        asio::write(client, asio::buffer(reply));
    }
}
