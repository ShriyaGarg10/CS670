#include "utils.hpp"
#include "constants.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Helper to load cleartext queries (i, j) from file
std::vector<std::pair<uint32_t, uint32_t>> load_cleartext_queries(const std::string& filename, uint32_t expected_q) {
    std::ifstream in(filename);
    if (!in) {
        // Try to extract from binary queries if cleartext doesn't exist
        throw std::runtime_error("Cannot open " + filename + ". Note: queries_cleartext.txt may need to be generated.");
    }
    std::vector<std::pair<uint32_t, uint32_t>> queries;
    uint32_t i_idx, j_idx;
    uint32_t count = 0;
    while (count < expected_q && (in >> i_idx >> j_idx)) {
        queries.emplace_back(i_idx, j_idx);
        count++;
    }
    if (queries.size() != expected_q) {
        throw std::runtime_error("Query count mismatch in " + filename + ": expected " + 
                                 std::to_string(expected_q) + ", got " + std::to_string(queries.size()));
    }
    return queries;
}

// Helper to extract cleartext queries from binary query files
std::vector<std::pair<uint32_t, uint32_t>> extract_queries_from_binary(const std::string& p0_file, 
                                                                        const std::string& p1_file, 
                                                                        uint32_t expected_q) {
    std::ifstream q0_in(p0_file, std::ios::binary);
    std::ifstream q1_in(p1_file, std::ios::binary);
    
    if (!q0_in || !q1_in) {
        throw std::runtime_error("Cannot open binary query files: " + p0_file + " or " + p1_file);
    }
    
    std::vector<std::pair<uint32_t, uint32_t>> queries;
    
    for (uint32_t i = 0; i < expected_q; ++i) {
        uint32_t user_idx;
        int64_t j0, j1;
        DPFKey k0, k1;
        
        // Read from P0's file
        q0_in.read(reinterpret_cast<char*>(&user_idx), sizeof(user_idx));
        q0_in.read(reinterpret_cast<char*>(&j0), sizeof(j0));
        k0 = read_key(q0_in);
        
        // Read from P1's file
        q1_in.read(reinterpret_cast<char*>(&user_idx), sizeof(user_idx));
        q1_in.read(reinterpret_cast<char*>(&j1), sizeof(j1));
        k1 = read_key(q1_in);
        
        // Reconstruct item index: j = j0 + j1
        int64_t j_recon = j0 + j1;
        uint32_t item_idx;
        if (j_recon >= 0) {
            item_idx = j_recon;
        } else {
            // Handle negative values (though this shouldn't happen for valid indices)
            item_idx = 0;
        }
        
        queries.emplace_back(user_idx, item_idx);
    }
    
    return queries;
}

// Recombines shares to get cleartext matrix
ShareMat recombine_shares(const ShareMat& M0, const ShareMat& M1) {
    if (M0.size() != M1.size() || (M0.size() > 0 && M0[0].size() != M1[0].size())) {
        throw std::runtime_error("Matrix dimension mismatch in recombine_shares");
    }
    
    int rows = M0.size();
    int cols = rows > 0 ? M0[0].size() : 0;
    ShareMat M(rows, ShareVec(cols));
    
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            M[i][j] = M0[i][j] + M1[i][j];
        }
    }
    return M;
}

// Cleartext dot product
int64_t dot_product(const ShareVec& u, const ShareVec& v) {
    if (u.size() != v.size()) {
        throw std::runtime_error("Vector size mismatch in dot_product");
    }
    int64_t dot = 0;
    for (size_t i = 0; i < u.size(); ++i) {
        dot += u[i] * v[i];
    }
    return dot;
}

// Apply cleartext updates according to the protocol
void apply_cleartext_updates(ShareMat& U, ShareMat& V, 
                             const std::vector<std::pair<uint32_t, uint32_t>>& queries) {
    for (const auto& query : queries) {
        uint32_t i_idx = query.first;  // user index
        uint32_t j_idx = query.second; // item index
        
        if (i_idx >= U.size() || j_idx >= V.size()) {
            throw std::runtime_error("Query index out of bounds: i=" + std::to_string(i_idx) + 
                                     ", j=" + std::to_string(j_idx));
        }
        
        ShareVec ui = U[i_idx];
        ShareVec vj = V[j_idx];
        
        // --- A1: User Update (in cleartext) ---
        // delta = 1 - <u_i, v_j>
        int64_t dot = dot_product(ui, vj);
        int64_t delta = 1 - dot;
        
        // update_term = v_j * delta
        ShareVec user_update_term(vj.size());
        for (size_t f = 0; f < vj.size(); ++f) {
            user_update_term[f] = vj[f] * delta;
        }
        
        // u_i <- u_i + update_term
        for (size_t f = 0; f < ui.size(); ++f) {
            U[i_idx][f] += user_update_term[f];
        }
        
        // --- A3: Item Update (in cleartext) ---
        // M = u_i * (1 - <u_i, v_j>)
        // Both updates are computed in parallel based on original vectors
        ShareVec item_update_term_M(ui.size());
        for (size_t f = 0; f < ui.size(); ++f) {
            item_update_term_M[f] = ui[f] * delta;
        }
        
        // v_j <- v_j + M
        for (size_t f = 0; f < vj.size(); ++f) {
            V[j_idx][f] += item_update_term_M[f];
        }
    }
}

// Convert int64_t matrix to uint32_t matrix (matching MPC output format)
std::vector<std::vector<uint32_t>> convert_to_uint32_matrix(const ShareMat& M) {
    std::vector<std::vector<uint32_t>> result(M.size());
    for (size_t i = 0; i < M.size(); ++i) {
        result[i].resize(M[i].size());
        for (size_t j = 0; j < M[i].size(); ++j) {
            // Cast through int32_t first to preserve sign interpretation, then to uint32_t
            result[i][j] = static_cast<uint32_t>(static_cast<int32_t>(M[i][j]));
        }
    }
    return result;
}

int main(int argc, char* argv[]) {
    // Use hardcoded constants from constants.hpp
    uint32_t m = M, n = N, k = K, q = Q;
    
    std::cout << "========================================" << std::endl;
    std::cout << "MPC Correctness Verification" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Parameters: m=" << m << ", n=" << n << ", k=" << k << ", q=" << q << std::endl;
    std::cout << std::endl;
    
    try {
        // --- 1. Load Initial Shares and Recombine ---
        std::cout << "Loading initial shares..." << std::endl;
        // Try data/ directory first, then current directory
        std::string dataDir = "";
        std::ifstream test_init_file("data/U0.txt");
        if (test_init_file) {
            dataDir = "data/";
            test_init_file.close();
        }
        
        ShareMat U0 = load_matrix_shares(dataDir + "U0.txt", m, k);
        ShareMat U1 = load_matrix_shares(dataDir + "U1.txt", m, k);
        ShareMat V0 = load_matrix_shares(dataDir + "V0.txt", n, k);
        ShareMat V1 = load_matrix_shares(dataDir + "V1.txt", n, k);
        
        ShareMat U_initial = recombine_shares(U0, U1);
        ShareMat V_initial = recombine_shares(V0, V1);
        
        std::cout << "Initial shares loaded and recombined." << std::endl;
        
        // --- 2. Load Cleartext Queries ---
        std::cout << "Loading queries..." << std::endl;
        std::vector<std::pair<uint32_t, uint32_t>> queries;
        
        // Try to load cleartext queries first
        try {
            queries = load_cleartext_queries(dataDir + "queries_cleartext.txt", q);
            std::cout << "Loaded cleartext queries from " << dataDir << "queries_cleartext.txt" << std::endl;
        } catch (const std::exception& e) {
            // If cleartext doesn't exist, try to extract from binary files
            std::cout << "queries_cleartext.txt not found, extracting from binary query files..." << std::endl;
            queries = extract_queries_from_binary(dataDir + "queries_p0.bin", dataDir + "queries_p1.bin", q);
            std::cout << "Extracted " << queries.size() << " queries from binary files." << std::endl;
        }
        
        // --- 3. Run Cleartext Simulation ---
        std::cout << "Running cleartext simulation for " << q << " queries..." << std::endl;
        
        ShareMat U_cleartext = U_initial;
        ShareMat V_cleartext = V_initial;
        apply_cleartext_updates(U_cleartext, V_cleartext, queries);
        
        std::cout << "Cleartext simulation complete." << std::endl;
        
        // --- 4. Load Final MPC-Computed Shares ---
        std::cout << "Loading final MPC-computed shares..." << std::endl;
        
        // Try multiple possible locations for updated files
        std::vector<std::string> possible_paths = {
            "U0_updated.txt", "/app/data/U0_updated.txt",
            "V0_updated.txt", "/app/data/V0_updated.txt",
            "output/U0_updated.txt", "output/V0_updated.txt"
        };
        
        std::string u0_path = dataDir + "U0_updated.txt";
        std::string u1_path = dataDir + "U1_updated.txt";
        std::string v0_path = dataDir + "V0_updated.txt";
        std::string v1_path = dataDir + "V1_updated.txt";
        
        // Try to find the files in multiple locations
        std::ifstream test_updated_file(u0_path);
        if (!test_updated_file) {
            test_updated_file.open("U0_updated.txt");
            if (test_updated_file) {
                u0_path = "U0_updated.txt";
                u1_path = "U1_updated.txt";
                v0_path = "V0_updated.txt";
                v1_path = "V1_updated.txt";
            } else {
                test_updated_file.open("/app/data/U0_updated.txt");
                if (test_updated_file) {
                    u0_path = "/app/data/U0_updated.txt";
                    u1_path = "/app/data/U1_updated.txt";
                    v0_path = "/app/data/V0_updated.txt";
                    v1_path = "/app/data/V1_updated.txt";
                } else {
                    test_updated_file.open("output/U0_updated.txt");
                    if (test_updated_file) {
                        u0_path = "output/U0_updated.txt";
                        u1_path = "output/U1_updated.txt";
                        v0_path = "output/V0_updated.txt";
                        v1_path = "output/V1_updated.txt";
                    }
                }
            }
        }
        test_updated_file.close();
        
        // Load updated shares (as uint32_t, matching MPC output format)
        std::ifstream u0_file(u0_path);
        std::ifstream u1_file(u1_path);
        std::ifstream v0_file(v0_path);
        std::ifstream v1_file(v1_path);
        
        if (!u0_file || !u1_file || !v0_file || !v1_file) {
            throw std::runtime_error(std::string("Cannot open updated share files. Tried:\n") +
                                     "  " + u0_path + "\n  " + u1_path + "\n  " +
                                     v0_path + "\n  " + v1_path + "\n" +
                                     "Make sure the MPC protocol has been run and generated these files.");
        }
        
        std::vector<std::vector<uint32_t>> U0_updated(m, std::vector<uint32_t>(k));
        std::vector<std::vector<uint32_t>> U1_updated(m, std::vector<uint32_t>(k));
        std::vector<std::vector<uint32_t>> V0_updated(n, std::vector<uint32_t>(k));
        std::vector<std::vector<uint32_t>> V1_updated(n, std::vector<uint32_t>(k));
        
        for (uint32_t i = 0; i < m; ++i) {
            for (uint32_t f = 0; f < k; ++f) {
                u0_file >> U0_updated[i][f];
                u1_file >> U1_updated[i][f];
            }
        }
        
        for (uint32_t j = 0; j < n; ++j) {
            for (uint32_t f = 0; f < k; ++f) {
                v0_file >> V0_updated[j][f];
                v1_file >> V1_updated[j][f];
            }
        }
        
        u0_file.close();
        u1_file.close();
        v0_file.close();
        v1_file.close();
        
        std::cout << "MPC output shares loaded from:" << std::endl;
        std::cout << "  " << u0_path << std::endl;
        std::cout << "  " << v0_path << std::endl;
        
        // Recombine MPC results (as uint32_t)
        std::vector<std::vector<uint32_t>> U_mpc(m, std::vector<uint32_t>(k));
        std::vector<std::vector<uint32_t>> V_mpc(n, std::vector<uint32_t>(k));
        
        for (uint32_t i = 0; i < m; ++i) {
            for (uint32_t f = 0; f < k; ++f) {
                U_mpc[i][f] = U0_updated[i][f] + U1_updated[i][f]; // mod 2^32 (automatic in uint32_t)
            }
        }
        
        for (uint32_t j = 0; j < n; ++j) {
            for (uint32_t f = 0; f < k; ++f) {
                V_mpc[j][f] = V0_updated[j][f] + V1_updated[j][f]; // mod 2^32
            }
        }
        
        // Convert cleartext results to uint32_t format for comparison
        std::vector<std::vector<uint32_t>> U_cleartext_uint = convert_to_uint32_matrix(U_cleartext);
        std::vector<std::vector<uint32_t>> V_cleartext_uint = convert_to_uint32_matrix(V_cleartext);
        
        // --- 5. Compare Results ---
        std::cout << std::endl;
        std::cout << "Comparing cleartext results with MPC results..." << std::endl;
        
        bool u_ok = true;
        bool v_ok = true;
        int u_errors = 0;
        int v_errors = 0;
        const int MAX_ERRORS_TO_PRINT = 10;
        
        // Check U matrix
        for (uint32_t i = 0; i < m; ++i) {
            for (uint32_t f = 0; f < k; ++f) {
                if (U_mpc[i][f] != U_cleartext_uint[i][f]) {
                    if (u_errors < MAX_ERRORS_TO_PRINT) {
                        std::cerr << "!!! MISMATCH in U matrix at U[" << i << "][" << f << "]:\n"
                                  << "  - MPC Result   = " << U_mpc[i][f] << "\n"
                                  << "  - Cleartext    = " << U_cleartext_uint[i][f] << "\n"
                                  << "  - Difference   = " << (int64_t)U_mpc[i][f] - (int64_t)U_cleartext_uint[i][f] << "\n";
                    }
                    u_errors++;
                    u_ok = false;
                }
            }
        }
        
        // Check V matrix
        for (uint32_t j = 0; j < n; ++j) {
            for (uint32_t f = 0; f < k; ++f) {
                if (V_mpc[j][f] != V_cleartext_uint[j][f]) {
                    if (v_errors < MAX_ERRORS_TO_PRINT) {
                        std::cerr << "!!! MISMATCH in V matrix at V[" << j << "][" << f << "]:\n"
                                  << "  - MPC Result   = " << V_mpc[j][f] << "\n"
                                  << "  - Cleartext    = " << V_cleartext_uint[j][f] << "\n"
                                  << "  - Difference   = " << (int64_t)V_mpc[j][f] - (int64_t)V_cleartext_uint[j][f] << "\n";
                    }
                    v_errors++;
                    v_ok = false;
                }
            }
        }
        
        // --- 6. Print Results ---
        std::cout << std::endl;
        std::cout << "========================================" << std::endl;
        if (u_ok && v_ok) {
            std::cout << "   SUCCESS: MPC result matches cleartext." << std::endl;
            std::cout << "   All updates were computed correctly!" << std::endl;
        } else {
            std::cout << "   FAILURE: MPC result does NOT match." << std::endl;
            if (u_errors > 0) {
                std::cout << "   U matrix errors: " << u_errors << " mismatches" << std::endl;
            }
            if (v_errors > 0) {
                std::cout << "   V matrix errors: " << v_errors << " mismatches" << std::endl;
            }
            if (u_errors + v_errors > MAX_ERRORS_TO_PRINT) {
                std::cout << "   (Only first " << MAX_ERRORS_TO_PRINT << " errors shown above)" << std::endl;
            }
        }
        std::cout << "========================================" << std::endl;
        
        return (u_ok && v_ok) ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << std::endl;
        std::cerr << "An error occurred: " << e.what() << std::endl;
        return 1;
    }
}

