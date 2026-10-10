#pragma once
#include "Config.hpp"
#include <asio.hpp>

class Server
{
private:
    Config m_cfg;

public:
    Server(Config cfg);

    asio::awaitable<void> serve() const;

    asio::awaitable<void> handleClient(asio::ip::tcp::socket socket) const;
};
