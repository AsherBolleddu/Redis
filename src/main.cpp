#include "Config.hpp"
#include "Server.hpp"
#include <asio/io_context.hpp>
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
        asio::io_context ioCtx;
        Server server { Config {}, ioCtx };
        server.serve();
    }
    catch (const std::exception& e)
    {
        std::cerr << typeid(e).name() << ": " << e.what() << '\n';
    }

    return 0;
}
