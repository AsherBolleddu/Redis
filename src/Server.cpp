#include "Server.hpp"
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

    constexpr std::string_view PONG { "+PONG\r\n" };
    asio::write(client, asio::buffer(PONG));
}
