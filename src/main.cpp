#include "Config.hpp"
#include "Server.hpp"
#include <asio.hpp>
#include <csignal>
#include <exception>
#include <iostream>

int main()
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cout << "Logs from your program will appear here!\n";

    try
    {
        asio::io_context ioCtx { 1 };
        asio::signal_set signals { ioCtx, SIGINT, SIGTERM };
        signals.async_wait([&](auto, auto) { ioCtx.stop(); });

        Server server { Config {} };
        asio::co_spawn(ioCtx, server.serve(), asio::detached);
        ioCtx.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << typeid(e).name() << ": " << e.what() << '\n';
    }

    return 0;
}
