#include "utils.hpp"
#include "constants.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <cstdlib>
#include <random>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <output_dir>" << std::endl;
        return 1;
    }

    uint32_t num_users = M;
    uint32_t num_items = N;
    uint32_t feature_dim = K;
    uint32_t num_queries = Q;
    std::string output_directory = argv[1];

    ShareMat user_matrix_p0(num_users, ShareVec(feature_dim));
    ShareMat user_matrix_p1(num_users, ShareVec(feature_dim));
    ShareMat item_matrix_p0(num_items, ShareVec(feature_dim));
    ShareMat item_matrix_p1(num_items, ShareVec(feature_dim));

    for (uint32_t user_idx = 0; user_idx < num_users; ++user_idx) {
        for (uint32_t feat_idx = 0; feat_idx < feature_dim; ++feat_idx) {
            int64_t actual_value = random_int8();
            int64_t share_p0 = random_int8();
            int64_t share_p1 = actual_value - share_p0;
            user_matrix_p0[user_idx][feat_idx] = share_p0;
            user_matrix_p1[user_idx][feat_idx] = share_p1;
        }
    }

    for (uint32_t item_idx = 0; item_idx < num_items; ++item_idx) {
        for (uint32_t feat_idx = 0; feat_idx < feature_dim; ++feat_idx) {
            int64_t actual_value = random_int8();
            int64_t share_p0 = random_int8();
            int64_t share_p1 = actual_value - share_p0;
            item_matrix_p0[item_idx][feat_idx] = share_p0;
            item_matrix_p1[item_idx][feat_idx] = share_p1;
        }
    }

    auto save_matrix_to_file = [&](const std::string& filename, const ShareMat& matrix) {
        std::ofstream output_stream(output_directory + "/" + filename);
        if (!output_stream) {
            std::cerr << "Error opening " << filename << " for writing" << std::endl;
            exit(1);
        }
        for (const auto& matrix_row : matrix) {
            for (size_t col_idx = 0; col_idx < matrix_row.size(); ++col_idx) {
                uint32_t output_value = static_cast<uint32_t>(static_cast<int32_t>(matrix_row[col_idx]));
                output_stream << output_value;
                if (col_idx < matrix_row.size() - 1) output_stream << " ";
            }
            output_stream << "\n";
        }
        output_stream.close();
    };

    save_matrix_to_file("U0.txt", user_matrix_p0);
    save_matrix_to_file("U1.txt", user_matrix_p1);
    save_matrix_to_file("V0.txt", item_matrix_p0);
    save_matrix_to_file("V1.txt", item_matrix_p1);

    std::cout << "Successfully generated initial matrix shares in " << output_directory << std::endl;

    std::ofstream query_file_p0(output_directory + "/queries_p0.bin", std::ios::binary);
    std::ofstream query_file_p1(output_directory + "/queries_p1.bin", std::ios::binary);
    std::ofstream cleartext_query_file(output_directory + "/queries_cleartext.txt");

    if (!query_file_p0 || !query_file_p1 || !cleartext_query_file) {
        std::cerr << "Error opening output files in " << output_directory << std::endl;
        exit(1);
    }

    std::mt19937& random_engine = get_prg_engine();
    std::uniform_int_distribution<uint32_t> user_distribution(0, num_users - 1);
    std::uniform_int_distribution<uint32_t> item_distribution(0, num_items - 1);
    std::uniform_int_distribution<int32_t> share_distribution;

    std::cout << "Generating " << num_queries << " queries for m=" << num_users << ", n=" << num_items << ", k=" << feature_dim << "..." << std::endl;

    for (uint32_t query_num = 0; query_num < num_queries; ++query_num) {
        uint32_t selected_user = user_distribution(random_engine);
        uint32_t selected_item = item_distribution(random_engine);

        int64_t item_share_p0 = share_distribution(random_engine);
        int64_t item_share_p1 = (int64_t)selected_item - item_share_p0;

        auto dpf_key_pair = generateDPF(selected_item, 0, num_items);
        DPFKey dpf_key_p0 = dpf_key_pair.first;
        DPFKey dpf_key_p1 = dpf_key_pair.second;
        
        query_file_p0.write(reinterpret_cast<const char*>(&selected_user), sizeof(selected_user));
        query_file_p0.write(reinterpret_cast<const char*>(&item_share_p0), sizeof(item_share_p0));
        write_key(query_file_p0, dpf_key_p0);

        query_file_p1.write(reinterpret_cast<const char*>(&selected_user), sizeof(selected_user));
        query_file_p1.write(reinterpret_cast<const char*>(&item_share_p1), sizeof(item_share_p1));
        write_key(query_file_p1, dpf_key_p1);
        
        cleartext_query_file << selected_user << " " << selected_item << "\n";

        if (query_num % (num_queries/10 + 1) == 0) {
             std::cout << "  Generated query " << query_num << " (User: " << selected_user << ", Item: " << selected_item << ")" << std::endl;
        }
    }

    std::cout << "Successfully generated query files in " << output_directory << std::endl;
    query_file_p0.close();
    query_file_p1.close();
    cleartext_query_file.close();

    return 0;
}
