#include "Handlers.hpp"
#include <format>
#include <span>
#include <string>
#include <unordered_map>

namespace Handlers
{

    namespace
    {
        std::string pingHandler(std::span<const std::string>)
        {
            return "+PONG\r\n";
        }

        std::string echoHandler(std::span<const std::string> data)
        {
            if (data.size() != 1)
                return "-ERR wrong number of arguments for 'echo' command\r\n";

            return std::format("${}\r\n{}\r\n", data.front().size(), data.front());
        }

        std::unordered_map<std::string, std::string (*)(std::span<const std::string>)> handlerTable {
            { "ping", pingHandler }, { "echo", echoHandler }
        };

    } // namespace

    std::string execute(const RESP::BulkString& request)
    {
        if (handlerTable.contains(request.command))
            return handlerTable[request.command](request.data);

        return std::format("-ERR unknown command '{}'\r\n", request.command);
    }

} // namespace Handlers
