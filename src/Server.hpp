#pragma once
#include "Config.hpp"
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

class Server
{
private:
    using tcp = asio::ip::tcp;
    Config m_cfg;
    asio::io_context& m_ioCtx;
    tcp::acceptor m_acceptor;

public:
    Server(Config cfg, asio::io_context& ioCtx);

    void serve();
};
