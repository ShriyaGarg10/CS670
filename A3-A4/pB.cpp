#include "common.hpp"
#include "constants.hpp"
#include <fstream> 
#include <iomanip>

#if !defined(ROLE_p0) && !defined(ROLE_p1)
#error "ROLE must be defined as ROLE_p0 or ROLE_p1"
#endif

#ifdef ROLE_p0
const int ROLE = 0;
const char* ROLE_STR = "P0";
#else
const int ROLE = 1;
const char* ROLE_STR = "P1";
#endif

awaitable<tcp::socket> connect_to_helper(boost::asio::io_context& io_ctx, tcp::resolver& resolver) {
    tcp::socket helper_socket(io_ctx);
    auto endpoints = resolver.resolve("p2", "9002");
    co_await boost::asio::async_connect(helper_socket, endpoints, use_awaitable);
    co_return helper_socket;
}

awaitable<tcp::socket> establish_peer_link(boost::asio::io_context& io_ctx, tcp::resolver& resolver) {
    tcp::socket peer_socket(io_ctx);
#ifdef ROLE_p0
    auto peer_endpoints = resolver.resolve("p1", "9001");
    std::cout << ROLE_STR << ": Connecting to P1 at p1:9001..." << std::endl;
    co_await boost::asio::async_connect(peer_socket, peer_endpoints, use_awaitable);
#else
    tcp::acceptor listener(io_ctx, tcp::endpoint(tcp::v4(), 9001));
    std::cout << ROLE_STR << ": Waiting for P0 on port 9001..." << std::endl;
    peer_socket = co_await listener.async_accept(use_awaitable);
#endif
    co_return peer_socket;
}

awaitable<int64_t> compute_secure_inner_product(const std::vector<int64_t>& my_x_share, 
                                                 const std::vector<int64_t>& my_y_share,
                                                 tcp::socket& peer_link, 
                                                 tcp::socket& helper_link) {
    std::vector<int64_t> beaver_x_share = co_await recv_vector(helper_link);
    std::vector<int64_t> beaver_y_share = co_await recv_vector(helper_link);
    int64_t beaver_c_share = co_await recv_value(helper_link);

    std::vector<int64_t> masked_x = vec_add(my_x_share, beaver_x_share);
    std::vector<int64_t> masked_y = vec_add(my_y_share, beaver_y_share);

    std::vector<int64_t> peer_masked_x, peer_masked_y;
    if (ROLE == 1) {
        peer_masked_x = co_await recv_vector(peer_link);
        peer_masked_y = co_await recv_vector(peer_link);
        co_await send_vector(peer_link, masked_x);
        co_await send_vector(peer_link, masked_y);
    } else {
        co_await send_vector(peer_link, masked_x);
        co_await send_vector(peer_link, masked_y);
        peer_masked_x = co_await recv_vector(peer_link);
        peer_masked_y = co_await recv_vector(peer_link);
    }

    int64_t my_result = vec_dot_product(my_x_share, vec_add(my_y_share, peer_masked_y)) 
                       - vec_dot_product(beaver_y_share, peer_masked_x) + beaver_c_share;

    co_return my_result;
}

awaitable<std::vector<int64_t>> compute_secure_scalar_vector_product(int64_t scalar_share,
                                                                      const std::vector<int64_t>& vector_share,
                                                                      tcp::socket& peer_link,
                                                                      tcp::socket& helper_link) {
    int64_t beaver_scalar_share = co_await recv_value(helper_link);
    std::vector<int64_t> beaver_vector_share = co_await recv_vector(helper_link);
    std::vector<int64_t> beaver_result_share = co_await recv_vector(helper_link);

    int64_t masked_scalar = scalar_share + beaver_scalar_share;
    std::vector<int64_t> masked_vector = vec_add(vector_share, beaver_vector_share);

    int64_t peer_masked_scalar;
    std::vector<int64_t> peer_masked_vector;
    if (ROLE == 0) {
        peer_masked_scalar = co_await recv_value(peer_link);
        peer_masked_vector = co_await recv_vector(peer_link);
        co_await send_value(peer_link, masked_scalar);
        co_await send_vector(peer_link, masked_vector);
    } else {
        co_await send_value(peer_link, masked_scalar);
        co_await send_vector(peer_link, masked_vector);
        peer_masked_scalar = co_await recv_value(peer_link);
        peer_masked_vector = co_await recv_vector(peer_link);
    }
    
    std::vector<int64_t> result = vec_add(
        vec_sub(
            vec_scalar_mul(vec_add(vector_share, peer_masked_vector), scalar_share),
            vec_scalar_mul(beaver_vector_share, peer_masked_scalar)
        ),
        beaver_result_share
    );
    
    co_return result;
}

awaitable<std::vector<int64_t>> retrieve_item_profile_shares(int64_t item_share,
                                                             const std::vector<std::vector<int64_t>>& item_matrix,
                                                             tcp::socket& peer_link,
                                                             tcp::socket& helper_link) {
    uint32_t num_items = item_matrix.size();
    uint32_t feature_dim = item_matrix[0].size();
    
    int64_t rotation_base = co_await recv_value(helper_link);
    std::vector<int64_t> rotation_vector = co_await recv_vector(helper_link);

    int64_t rotation_offset = item_share - rotation_base;
    int64_t peer_rotation_offset;
    
    if (ROLE == 1) {
        peer_rotation_offset = co_await recv_value(peer_link);
        co_await send_value(peer_link, rotation_offset);
    } else {
        co_await send_value(peer_link, rotation_offset);
        peer_rotation_offset = co_await recv_value(peer_link);
    }

    uint32_t total_rotation;
    int64_t combined_offset = rotation_offset + peer_rotation_offset;
    if (combined_offset >= 0) {
        total_rotation = combined_offset % num_items;
    } else {
        total_rotation = (num_items + (combined_offset % (int64_t)num_items)) % num_items;
    }

    std::vector<int64_t> selector_vector = rotation_vector;
    std::rotate(selector_vector.begin(), 
                selector_vector.begin() + (selector_vector.size() - total_rotation) % selector_vector.size(),
                selector_vector.end());

    std::vector<int64_t> item_profile(feature_dim);
    for (uint32_t feat_idx = 0; feat_idx < feature_dim; feat_idx++) {
        std::vector<int64_t> item_matrix_column(num_items);
        for (uint32_t item_idx = 0; item_idx < num_items; ++item_idx) {
            item_matrix_column[item_idx] = item_matrix[item_idx][feat_idx];
        }
        item_profile[feat_idx] = co_await compute_secure_inner_product(item_matrix_column, selector_vector, peer_link, helper_link);
    }
    
    co_return item_profile;
}

awaitable<void> execute_protocol(boost::asio::io_context& io_ctx, uint32_t num_users, uint32_t num_items, uint32_t feature_dim, uint32_t num_queries) {
    tcp::resolver resolver(io_ctx);

    tcp::socket helper_connection = co_await connect_to_helper(io_ctx, resolver);
    std::cout << ROLE_STR << ": Connected to P2." << std::endl;

    tcp::socket peer_connection = co_await establish_peer_link(io_ctx, resolver);
    std::cout << ROLE_STR << ": Peer connection established." << std::endl;

    ShareMat user_matrix = load_matrix_shares(std::string("/app/data/U") + std::to_string(ROLE) + ".txt", num_users, feature_dim);
    ShareMat item_matrix = load_matrix_shares(std::string("/app/data/V") + std::to_string(ROLE) + ".txt", num_items, feature_dim);
    std::cout << ROLE_STR << ": Loaded U and V matrix shares from files." << std::endl;

    std::vector<Query> query_list = read_queries(std::string("/app/data/queries_p") + std::to_string(ROLE) + ".bin");
    std::cout << ROLE_STR << ": Loaded " << query_list.size() << " queries." << std::endl;

    std::vector<double> user_update_timings(query_list.size());
    std::vector<double> item_update_timings(query_list.size());

    double cumulative_user_time = 0.0;
    double cumulative_item_time = 0.0;

    for (size_t query_idx = 0; query_idx < query_list.size(); ++query_idx) {
        const auto& current_query = query_list[query_idx];
        uint32_t user_id = current_query.user_index;
        int64_t item_share_value = current_query.item_share;
        DPFKey dpf_key_share = current_query.dpf_key;
        std::cout << ROLE_STR << ": Starting query " << query_idx << " (user=" << user_id << ", item_share=" << item_share_value << ")" << std::endl;

        ShareVec user_profile = user_matrix[user_id];

        auto user_timer_start = std::chrono::high_resolution_clock::now();

        ShareVec item_profile = co_await retrieve_item_profile_shares(item_share_value, item_matrix, peer_connection, helper_connection);
        int64_t inner_product_share = co_await compute_secure_inner_product(user_profile, item_profile, peer_connection, helper_connection);
        ShareVec scaled_item_profile = co_await compute_secure_scalar_vector_product(inner_product_share, item_profile, peer_connection, helper_connection);
        user_matrix[user_id] = vec_sub(vec_add(user_matrix[user_id], item_profile), scaled_item_profile);

        auto user_timer_end = std::chrono::high_resolution_clock::now();
        user_update_timings[query_idx] = std::chrono::duration_cast<std::chrono::nanoseconds>(user_timer_end - user_timer_start).count();
        cumulative_user_time += user_update_timings[query_idx];

        auto item_timer_start = std::chrono::high_resolution_clock::now();
        
        int64_t complement_share = ROLE - inner_product_share;
        ShareVec update_vector = co_await compute_secure_scalar_vector_product(complement_share, user_profile, peer_connection, helper_connection);
        
        for (uint32_t feat_idx = 0; feat_idx < feature_dim; ++feat_idx) {
            int64_t update_component = update_vector[feat_idx];
            int64_t original_fcw = dpf_key_share.FCW;
            int64_t masked_update = update_component - original_fcw;
            
            int64_t peer_masked_update;
            if (ROLE == 0) {
                peer_masked_update = co_await recv_value(peer_connection);
                co_await send_value(peer_connection, masked_update);
            } else {
                co_await send_value(peer_connection, masked_update);
                peer_masked_update = co_await recv_value(peer_connection);
            }
            
            int64_t adjusted_fcw = masked_update + peer_masked_update;
            DPFKey modified_key = dpf_key_share;
            modified_key.FCW = adjusted_fcw;
            
            std::vector<int64_t> dpf_evaluation_result = EvalFull(modified_key, num_items);
            
            for (uint32_t item_idx = 0; item_idx < num_items; ++item_idx) {
                item_matrix[item_idx][feat_idx] += dpf_evaluation_result[item_idx];
            }
        }
        std::cout << ROLE_STR << ": Finished query " << query_idx << std::endl;

        auto item_timer_end = std::chrono::high_resolution_clock::now();
        item_update_timings[query_idx] = std::chrono::duration_cast<std::chrono::nanoseconds>(item_timer_end - item_timer_start).count();
        cumulative_item_time += item_update_timings[query_idx];
    }

    std::cout << ROLE_STR << ": All queries processed." << std::endl;

    std::ofstream updated_user_file(std::string("/app/data/U") + std::to_string(ROLE) + "_updated.txt");
    std::ofstream updated_item_file(std::string("/app/data/V") + std::to_string(ROLE) + "_updated.txt");
    
    if (updated_user_file.is_open()) {
        for (uint32_t user_idx = 0; user_idx < num_users; ++user_idx) {
            for (uint32_t feat_idx = 0; feat_idx < feature_dim; ++feat_idx) {
                uint32_t output_val = static_cast<uint32_t>(static_cast<int32_t>(user_matrix[user_idx][feat_idx]));
                updated_user_file << output_val;
                if (feat_idx < feature_dim - 1) updated_user_file << " ";
            }
            updated_user_file << "\n";
        }
        updated_user_file.close();
        std::cout << ROLE_STR << ": Saved updated U shares to U" << ROLE << "_updated.txt" << std::endl;
    }
    
    if (updated_item_file.is_open()) {
        for (uint32_t item_idx = 0; item_idx < num_items; ++item_idx) {
            for (uint32_t feat_idx = 0; feat_idx < feature_dim; ++feat_idx) {
                uint32_t output_val = static_cast<uint32_t>(static_cast<int32_t>(item_matrix[item_idx][feat_idx]));
                updated_item_file << output_val;
                if (feat_idx < feature_dim - 1) updated_item_file << " ";
            }
            updated_item_file << "\n";
        }
        updated_item_file.close();
        std::cout << ROLE_STR << ": Saved updated V shares to V" << ROLE << "_updated.txt" << std::endl;
    }

    if (ROLE == 0) {
        double avg_user_time_seconds = (cumulative_user_time / query_list.size()) * 1e-9;
        double avg_item_time_seconds = (cumulative_item_time / query_list.size()) * 1e-9;
        
        std::cout << "\n--- Performance Metrics ---" << std::endl;
        std::cout << "Parameters: m=" << num_users << ", n=" << num_items << ", k=" << feature_dim << ", q=" << num_queries << std::endl;
        std::cout << "Average user profile update time: " << avg_user_time_seconds << " seconds" << std::endl;
        std::cout << "Average item profile update time: " << avg_item_time_seconds << " seconds" << std::endl;
        std::cout << "user_update_time: " << avg_user_time_seconds << std::endl;
        std::cout << "item_update_time: " << avg_item_time_seconds << std::endl;
        
        std::cout << std::fixed << std::setprecision(9);
        for (size_t idx = 0; idx < user_update_timings.size(); ++idx) {
            double user_time_sec = user_update_timings[idx] * 1e-9;
            double item_time_sec = item_update_timings[idx] * 1e-9;
            std::cout << "Query " << idx << ": user=" << user_time_sec << "s, item=" << item_time_sec << "s" << std::endl;
        }
    }
    
    co_return;
}

int main(int argc, char* argv[]) {
    uint32_t num_users = M, num_items = N, feature_dim = K, num_queries = Q;

    std::cout.setf(std::ios::unitbuf);
    boost::asio::io_context io_ctx(1);
    co_spawn(io_ctx, execute_protocol(io_ctx, num_users, num_items, feature_dim, num_queries), 
        [&](std::exception_ptr exc) {
            if (exc) {
                try {
                    std::rethrow_exception(exc);
                } catch (std::exception& e) {
                    std::cerr << ROLE_STR << " Coroutine error: " << e.what() << std::endl;
                }
            }
        });
    io_ctx.run();
    return 0;
}
