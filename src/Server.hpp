#pragma once
#include "Config.hpp"
#include "Handlers.hpp"
#include <asio.hpp>

class Server
{
private:
    Config m_cfg;
    Handlers m_handler;

public:
    Server(Config cfg);

    asio::awaitable<void> serve();

    asio::awaitable<void> handleClient(asio::ip::tcp::socket socket);
};
