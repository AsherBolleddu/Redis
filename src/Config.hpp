#pragma once

#include <asio/ip/basic_endpoint.hpp>

struct Config
{
    static constexpr asio::ip::port_type PORT { 6379 };
};
