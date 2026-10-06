#include "Server.hpp"
#include <iostream>
#include <utility>

Server::Server(Config cfg, asio::io_context& ioCtx)
    : m_cfg { std::move(cfg) }, m_ioCtx { ioCtx }, m_acceptor { m_ioCtx, tcp::endpoint { tcp::v6(), m_cfg.PORT } }
{
}

void Server::serve()
{

    std::cout << "Waiting for a client to connect...\n";

    tcp::socket socket { m_ioCtx };
    m_acceptor.accept(socket);

    std::cout << "Client connected\n";
}
