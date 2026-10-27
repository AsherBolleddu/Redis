[![progress-banner](https://backend.codecrafters.io/progress/redis/17880355-9d1e-4b45-a27b-6b57e7ec2704)](https://app.codecrafters.io/users/AsherBolleddu?r=2qF)

# Redis Server

A Redis-compatible in-memory key-value server written in C++23 on a single-threaded Asio event loop with C++20 coroutines. Built as a solution to the CodeCrafters ["Build Your Own Redis"](https://app.codecrafters.io/courses/redis/overview) challenge.

## Features

- **Coroutine-per-connection** on one `asio::io_context` thread: every client is an `asio::awaitable<void>` coroutine, so concurrency costs a suspended frame rather than a thread
- **Incremental RESP parser** that distinguishes *incomplete* input (wait for more bytes) from *malformed* input (reply with an error and close), so partial TCP reads and pipelined commands both work
- **Zero-copy arguments**: command arguments are `std::string_view`s into the receive buffer; only the (lowercased) command name is copied
- **Pipelining**: all complete commands in a read are executed and their replies written in a single `async_write`
- **Strings** with `EX`/`PX` expiry, **lists**, and **streams** stored in a `std::variant` value store with lazy expiry and Redis-style `WRONGTYPE` errors
- **Blocking commands without threads**: `BLPOP` and `XREAD BLOCK` suspend the client's coroutine on an `asio::steady_timer` that doubles as the timeout; a push or `XADD` wakes waiters in FIFO order
- **Streams** with auto-generated, strictly increasing entry IDs and binary-searched range queries over ID-ordered entries
- **Table-driven dispatch**: a map from command name to member-function pointer, so an unknown command is a one-line error
- **Clean shutdown** on `SIGINT`/`SIGTERM` via `asio::signal_set`

## Performance

Measured with `redis-benchmark` over loopback on an Apple Silicon laptop, Release build, 50 concurrent clients:

| Mode                     | Requests/sec |
| ------------------------ | ------------ |
| Unpipelined              | ~250k        |
| Pipelined (`-P 16`)      | 2.5M+        |

```sh
redis-benchmark -c 50 -n 200000 -t ping,set,get
redis-benchmark -c 50 -n 2000000 -t ping,set,get -P 16
```

Loopback numbers measure the server's per-command overhead, not real network behavior.

## Quick start

### Requirements

- Linux or macOS
- CMake 3.13 or newer and Ninja
- A compiler with C++23 library support (tested with Apple clang on macOS arm64; the code uses `std::expected`, `std::ranges::to` and `std::format`)
- [vcpkg](https://github.com/microsoft/vcpkg), with `VCPKG_ROOT` set (it supplies Asio)

### Build and run

```sh
./your_program.sh
```

The script configures CMake from `CMakePresets.json`, builds the Debug configuration, and starts the server on port `6379`. To do the steps by hand:

```sh
cmake --preset default
cmake --build --preset default --config Release
./build/Release/redis
```

### Try it

```sh
redis-cli PING
redis-cli SET name asher PX 5000
redis-cli GET name
redis-cli RPUSH tasks a b c
redis-cli LRANGE tasks 0 -1
redis-cli BLPOP tasks 5
redis-cli XADD events '*' temp 21
redis-cli XRANGE events - +
redis-cli XREAD BLOCK 5000 STREAMS events '$'
```

## Commands

| Command  | Syntax                                                   | Notes                                                                                    |
| -------- | -------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `PING`   | `PING`                                                   | `+PONG`                                                                                  |
| `ECHO`   | `ECHO message`                                           | Returns the message as a bulk string                                                     |
| `SET`    | `SET key value [EX seconds \| PX milliseconds]`          | Overwrites any existing value; expiry is optional                                        |
| `GET`    | `GET key`                                                | Null bulk string if missing or expired; `WRONGTYPE` if the key holds a list or stream    |
| `TYPE`   | `TYPE key`                                               | `string`, `list`, `stream` or `none`                                                     |
| `RPUSH`  | `RPUSH key value [value ...]`                            | Creates the list if needed; returns the new length                                       |
| `LPUSH`  | `LPUSH key value [value ...]`                            | Same, pushing to the head                                                                |
| `LRANGE` | `LRANGE key start stop`                                  | Negative indices count from the end; out-of-range bounds are clamped                     |
| `LLEN`   | `LLEN key`                                               | `0` for a missing key                                                                    |
| `LPOP`   | `LPOP key [count]`                                       | Single element, or an array of up to `count`; the key is deleted when the list empties   |
| `BLPOP`  | `BLPOP key timeout`                                      | Blocks until an element is available or `timeout` seconds pass (`0` = forever)           |
| `XADD`   | `XADD key id field value [field value ...]`              | `id` may be `*`, `ms-*`, or explicit `ms-seq`; must be greater than the last entry       |
| `XRANGE` | `XRANGE key start end`                                   | Inclusive range; `-` and `+` for the ends; a bare `ms` means `ms-0` / `ms-max`           |
| `XREAD`  | `XREAD [BLOCK ms] STREAMS key [key ...] id [id ...]`     | Entries strictly after each `id`; `$` means "only new entries"; `BLOCK 0` waits forever  |

Error replies follow Redis: wrong argument counts, non-integer arguments, `WRONGTYPE` on type mismatches, invalid or non-increasing stream IDs, and `-ERR unknown command` for anything else.

## How it works

```
io_context (one thread)
        │
        ▼
  Server::serve ──── async_accept ────► co_spawn(handleClient)   (one coroutine per client)
                                                 │
                                                 ▼
          async_read_some ► append to buffer ► parse all complete commands ► execute ► async_write replies
                 ▲                                                                           │
                 └─────────────────────────────── loop until EOF ◄───────────────────────────┘

  blocking commands:  BLPOP / XREAD BLOCK ──► register steady_timer in m_waiters ──► co_await timer
                      RPUSH / LPUSH / XADD ──► cancel the first N waiting timers ──► waiter resumes and retries
```

1. `main` builds a single-threaded `io_context`, hooks `SIGINT`/`SIGTERM` to stop it, and spawns `Server::serve`.
2. `Server::serve` accepts connections on an IPv6 listener (dual-stack on macOS and Linux) and spawns `handleClient` for each socket.
3. `handleClient` reads into a 1 KiB chunk, appends to a per-connection buffer, and calls `RESP::parseRequest` in a loop. `Incomplete` breaks out to read more; `Malformed` replies `-ERR Protocol error` and closes; otherwise the parsed command is executed and the consumed bytes are erased. Replies for every command in the batch are written with one `async_write`.
4. `Handlers::execute` looks the lowercased command up in a static table of member-function pointers and invokes it with the argument span. Every handler is itself a coroutine, which is what lets `BLPOP` and `XREAD` suspend without changing the call site.
5. Expiry is lazy: `findLive` deletes an expired key the next time it is touched, so no background sweeper is needed.

### Project layout

| File                          | Responsibility                                                               |
| ----------------------------- | ---------------------------------------------------------------------------- |
| `src/main.cpp`                | `io_context`, signal handling, server startup                                |
| `src/Server.{hpp,cpp}`        | Accept loop and the per-connection read → parse → execute → write loop       |
| `src/RESP.{hpp,cpp}`          | Incremental RESP array/bulk-string parser returning `std::expected`          |
| `src/Handlers.{hpp,cpp}`      | Value store, dispatch table, waiter bookkeeping, `PING`/`ECHO`               |
| `src/StringHandlers.cpp`      | `SET`, `GET`                                                                 |
| `src/ListHandlers.cpp`        | `RPUSH`, `LPUSH`, `LRANGE`, `LLEN`, `LPOP`, `BLPOP`                          |
| `src/StreamHandlers.cpp`      | `TYPE`, `XADD`, `XRANGE`, `XREAD`                                            |
| `src/Helpers.{hpp,cpp}`       | `parseNum` (`std::from_chars` wrapper), stream ID parsing, case-insensitive compare |
| `src/Replies.hpp`             | Canonical RESP error and empty replies                                       |
| `src/Config.hpp`              | Port                                                                         |

### Design notes

- **Errors as values in the parser.** `parseRequest` returns `std::expected<Request, ParseError>` with two error states. `Incomplete` is the normal case under TCP framing and costs nothing; `Malformed` is a protocol violation and ends the connection. The caller never has to guess which happened.
- **The buffer owns the bytes; the request borrows them.** A `Request` holds `string_view`s into the connection buffer plus `bytesConsumed`, so execution happens before the buffer is erased and no argument is copied unless a handler stores it.
- **One value type, three payloads.** `Value` is a `std::variant<std::string, std::deque<std::string>, std::vector<StreamEntry>>` plus an optional expiry. Handlers `std::get_if` the type they need and return `WRONGTYPE` otherwise, which is exactly Redis's behavior.
- **Blocking via timers, not threads.** A blocked client registers a pointer to its own `steady_timer` in `m_waiters[key]` and `co_await`s it. A push calls `cancel_one()` on the first *n* timers for that key (one per element pushed); the woken coroutine loops, finds the element, and returns. If the timer expires on its own, the client gets a null reply. `XADD` wakes every waiter on its key, since one entry can satisfy any number of readers.
- **Stream IDs are a sortable pair.** `StreamID { ms, seq }` with defaulted `operator<=>`, so `XRANGE` and `XREAD` are `std::ranges::lower_bound` / `upper_bound` over the entry vector, and "strictly greater than the last entry" is a single comparison in `XADD`.
- **Single-threaded by design.** `io_context { 1 }` means handlers touch the store without locks. Throughput comes from never blocking the loop, not from parallelism.
