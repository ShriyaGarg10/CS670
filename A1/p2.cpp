#include "common.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <random>


using boost::asio::ip::tcp;

// Random number generator
 
// Send a number to a client
boost::asio::awaitable<void> handle_client(tcp::socket socket, const std::string& name) {
    uint32_t rnd = random_uint32();
    std::cout << "P2 sending to " << name << ": " << rnd << "\n";
    co_await boost::asio::async_write(socket, boost::asio::buffer(&rnd, sizeof(rnd)), boost::asio::use_awaitable);
}

// Run multiple coroutines in parallel
template <typename... Fs>
void run_in_parallel(boost::asio::io_context& io, Fs&&... funcs) {
    (boost::asio::co_spawn(io, funcs, boost::asio::detached), ...);
}

int main() {
    try {
        boost::asio::io_context io_context;

        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 9002));

        // Accept clients
        tcp::socket socket_p0(io_context);
        acceptor.accept(socket_p0);

        tcp::socket socket_p1(io_context);
        acceptor.accept(socket_p1);

        // Launch all coroutines in parallel
        run_in_parallel(io_context,
            [&]() -> boost::asio::awaitable<void> {
                co_await handle_client(std::move(socket_p0), "P0");
            },
            [&]() -> boost::asio::awaitable<void> {
                co_await handle_client(std::move(socket_p1), "P1");
            }
        );

        io_context.run();

    } catch (std::exception& e) {
        std::cerr << "Exception in P2: " << e.what() << "\n";
    }
}
