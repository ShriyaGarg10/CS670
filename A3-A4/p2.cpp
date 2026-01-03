#include "common.hpp"
#include "constants.hpp"

template <typename... Funcs>
void spawn_parallel_tasks(boost::asio::io_context& io_ctx, Funcs&&... tasks) {
    (boost::asio::co_spawn(io_ctx, tasks, boost::asio::detached), ...);
}

awaitable<void> generate_dot_product_material(tcp::socket& socket_p0, tcp::socket& socket_p1, size_t vector_length) {
    std::vector<int64_t> X0_shares(vector_length), Y0_shares(vector_length);
    std::vector<int64_t> X1_shares(vector_length), Y1_shares(vector_length);
    
    for (size_t idx = 0; idx < vector_length; ++idx) {
        X0_shares[idx] = random_int8();
        Y0_shares[idx] = random_int8();
        X1_shares[idx] = random_int8();
        Y1_shares[idx] = random_int8();
    }
    
    int64_t randomness_term = random_int8();

    co_await send_vector(socket_p0, X0_shares);
    co_await send_vector(socket_p0, Y0_shares);
    co_await send_value(socket_p0, vec_dot_product(X0_shares, Y1_shares) + randomness_term);

    co_await send_vector(socket_p1, X1_shares);
    co_await send_vector(socket_p1, Y1_shares);
    co_await send_value(socket_p1, vec_dot_product(X1_shares, Y0_shares) - randomness_term);
}

awaitable<void> generate_scalar_vector_material(tcp::socket& socket_p0, tcp::socket& socket_p1, size_t vector_length) {
    int64_t X0_value = random_int8();
    int64_t X1_value = random_int8();
    std::vector<int64_t> Y0_shares(vector_length), Y1_shares(vector_length), randomness_vector(vector_length);
    
    for (size_t idx = 0; idx < vector_length; ++idx) {
        Y0_shares[idx] = random_int8();
        Y1_shares[idx] = random_int8();
        randomness_vector[idx] = random_int8();
    }

    co_await send_value(socket_p0, X0_value);
    co_await send_vector(socket_p0, Y0_shares);
    co_await send_vector(socket_p0, vec_add(vec_scalar_mul(Y0_shares, X1_value), randomness_vector));

    co_await send_value(socket_p1, X1_value);
    co_await send_vector(socket_p1, Y1_shares);
    co_await send_vector(socket_p1, vec_sub(vec_scalar_mul(Y1_shares, X0_value), randomness_vector));
}

boost::asio::awaitable<void> process_query_session(tcp::socket socket_p0, tcp::socket socket_p1, uint32_t num_users, uint32_t num_items, uint32_t feature_dim, uint32_t num_queries) {
    std::cout << "P2: Starting session for " << num_queries << " queries." << std::endl;
    
    for (uint32_t query_num = 0; query_num < num_queries; ++query_num) {
        std::cout << "P2: Sending materials for query " << query_num << std::endl;
        int64_t random_index = random_uint32() % num_items;
        std::vector<int64_t> one_hot_vector(num_items, 0);
        one_hot_vector[random_index] = 1;
        
        std::vector<int64_t> r0_shares(num_items);
        for (uint32_t idx = 0; idx < num_items; ++idx) {
            r0_shares[idx] = random_int8();
        }
        std::vector<int64_t> r1_shares = vec_sub(one_hot_vector, r0_shares);
        int64_t rotation_offset_share = random_int32();

        co_await send_value(socket_p0, rotation_offset_share);
        co_await send_vector(socket_p0, r0_shares);
        co_await send_value(socket_p1, random_index - rotation_offset_share);
        co_await send_vector(socket_p1, r1_shares);

        for (uint32_t feat_idx = 0; feat_idx < feature_dim; feat_idx++) {
            co_await generate_dot_product_material(socket_p0, socket_p1, num_items);
        }

        co_await generate_dot_product_material(socket_p0, socket_p1, feature_dim);
        co_await generate_scalar_vector_material(socket_p0, socket_p1, feature_dim);
        co_await generate_scalar_vector_material(socket_p0, socket_p1, feature_dim);
    }
    
    std::cout << "P2: Session finished." << std::endl;
}

int main(int argc, char* argv[]) {
    uint32_t num_users = M, num_items = N, feature_dim = K, num_queries = Q;

    try {
        boost::asio::io_context io_ctx;
        tcp::acceptor server_acceptor(io_ctx, tcp::endpoint(tcp::v4(), 9002));
        
        std::cout << "P2: Waiting for P0 on port 9002..." << std::endl;
        tcp::socket socket_p0 = server_acceptor.accept();
        std::cout << "P2: P0 connected." << std::endl;
        
        std::cout << "P2: Waiting for P1 on port 9002..." << std::endl;
        tcp::socket socket_p1 = server_acceptor.accept();
        std::cout << "P2: P1 connected." << std::endl;
        
        co_spawn(io_ctx, process_query_session(std::move(socket_p0), std::move(socket_p1), num_users, num_items, feature_dim, num_queries), detached);
        io_ctx.run();
    } catch (std::exception& e) {
        std::cerr << "Exception in P2: " << e.what() << "\n";
    }
    return 0;
}
