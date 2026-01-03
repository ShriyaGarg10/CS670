#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <cmath>
#include <utility>
#include <stdexcept>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <openssl/sha.h>

using u64 = uint64_t;
using Seed = __int128;  // 128-bit seeds for cryptographic security

// Toggle logging for debugging DPF generation process
const bool ENABLE_LOGGING = false;
std::ofstream logFile;


// Helper function to print 128-bit seeds in hex format for debugging
void print_seed(std::ostream& os, Seed seed) {
    auto flags = os.flags();
    os << std::hex << std::setfill('0') 
       << std::setw(16) << (uint64_t)(seed >> 64) 
       << std::setw(16) << (uint64_t)seed;
    os.flags(flags);
}

// Correction word stores the adjustments needed at each level of the tree
struct CorrectionWord {
    Seed s_cw_left;   // Seed correction for left child
    Seed s_cw_right;  // Seed correction for right child
    bool t_cw_left;   // Flag correction for left child
    bool t_cw_right;  // Flag correction for right child
};

// A DPF key - one party gets k0, the other gets k1
struct DPFKey {
    Seed initial_seed;                           // Starting seed for this key
    bool initial_flag;                           // Starting control bit
    std::vector<CorrectionWord> correction_words; // One per level of the tree
    u64 final_correction_word;                   // Applied at leaf level
};

// Output of the PRG: expands one seed into two seeds with control bits
struct PRGOutput {
    Seed s_left, s_right;  // Seeds for left and right children
    bool t_left, t_right;  // Control bits for left and right children
};

// Pseudorandom generator: expands one seed into two child seeds + control bits
// Uses SHA256 to generate pseudorandom output from the seed
PRGOutput prg_expand(Seed seed) {
    // Convert 128-bit seed to byte array
    unsigned char input[16];
    for (int i = 0;i <16;i++) {
        input[i] = (unsigned char)(seed >> (8 * (15 - i)));
    }
    
    // Hash the seed to get 256 bits of pseudorandom output
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(input, sizeof(input), hash);
    
    PRGOutput out;
    out.s_left = 0;
    out.s_right = 0;
    
    // First 128 bits become left child seed
    for (int i = 0;i< 16;i++){
         out.s_left = (out.s_left<<8) | hash[i]; 
        }
    // Next 128 bits become right child seed
    for (int i = 0;i< 16;i++){
         out.s_right= (out.s_right<<8) | hash[i+16];
        }
    
    // Last two bits are the control bits
    out.t_left = hash[SHA256_DIGEST_LENGTH-2] & 1;
    out.t_right= hash[SHA256_DIGEST_LENGTH-1] & 1;

    return out;
}

// Generate a pair of DPF keys that reconstruct to 'value' at 'location', 0 elsewhere
// The two keys are given to two different parties for secure computation
std::pair<DPFKey, DPFKey> generateDPF(u64 location, u64 value, int domain_bits) {
    DPFKey k0, k1;
    std::random_device rd;
    std::mt19937_64 gen(rd());
    
    // Initialize with random seeds - this is the only randomness needed
    k0.initial_seed =((Seed)gen()<<64) | gen();
    k1.initial_seed =((Seed)gen()<<64) | gen();
    
    // Party 0 starts with flag=1, Party 1 starts with flag=0
    k0.initial_flag =true;
    k1.initial_flag =false;

    if (ENABLE_LOGGING) {
        logFile << "===== DPF GENERATION (Location: " << location << ") =====\n";
        logFile << "L0: s0=";
        print_seed(logFile, k0.initial_seed);
        logFile << ", t0=" << k0.initial_flag << "\n";
        logFile << "L0: s1=";
        print_seed(logFile, k1.initial_seed);
        logFile << ", t1=" << k1.initial_flag << "\n";
    }

    // Track current seeds and flags for both parties
    Seed s0 =k0.initial_seed, s1 =k1.initial_seed;
    bool t0 =k0.initial_flag, t1 =k1.initial_flag;

    // Build the tree level by level, following the path to the target location
    for (int i = 0; i < domain_bits; ++i) {
        // Extract the i-th bit of the location (which direction to go)
        bool path_bit = (location >> (domain_bits - 1 - i)) & 1;
        if (ENABLE_LOGGING) {
            logFile << "\n--- Level " <<i + 1 << " ---\n";
            logFile << "  Path bit: " <<path_bit << " (" << (path_bit ? "RIGHT" : "LEFT") << ")\n";
        }
        
        // Expand both seeds
        PRGOutput out0= prg_expand(s0);
        PRGOutput out1= prg_expand(s1);

        // Create correction word to keep the two parties' paths aligned
        CorrectionWord cw;
        if (path_bit == 0){
            // We're going left, so keep left path identical, correct right path
            cw.s_cw_left = 0;
            cw.s_cw_right = out0.s_right ^ out1.s_right;
            cw.t_cw_left = out0.t_left ^ out1.t_left ^ 1;  // XOR with 1 to maintain invariant
            cw.t_cw_right = out0.t_right ^ out1.t_right;
        } 
        else {
            // We're going right, so keep right path identical, correct left path
            cw.s_cw_left = out0.s_left ^ out1.s_left;
            cw.s_cw_right = 0;
            cw.t_cw_left = out0.t_left ^ out1.t_left;
            cw.t_cw_right = out0.t_right ^ out1.t_right ^ 1;  // XOR with 1 to maintain invariant
        }
        k0.correction_words.push_back(cw);
        k1.correction_words.push_back(cw);
        
        // Select the child on the path to the target location
        Seed s0_path = path_bit? out0.s_right : out0.s_left;
        Seed s1_path = path_bit? out1.s_right : out1.s_left;
        bool t0_path = path_bit? out0.t_right : out0.t_left;
        bool t1_path = path_bit? out1.t_right : out1.t_left;

        // Get the correction word for the path we're taking
        Seed s_cw_keep = path_bit? cw.s_cw_right : cw.s_cw_left;
        bool t_cw_keep = path_bit? cw.t_cw_right : cw.t_cw_left;

        // Apply corrections conditionally based on the control bit
        s0 = s0_path ^ (t0 ? s_cw_keep : 0);
        s1 = s1_path ^ (t1 ? s_cw_keep : 0);
        t0 = t0_path ^ (t0 ? t_cw_keep : false);
        t1 = t1_path ^ (t1 ? t_cw_keep : false);
    }

    // At the leaf, create a final correction to set the output value
    u64 final_s0_val= (u64)s0;
    u64 final_s1_val= (u64)s1;
    
    // The correction word ensures s0 XOR s1 XOR final_cw = value
    u64 final_cw=value ^ final_s0_val^ final_s1_val;

    k0.final_correction_word = final_cw;
    k1.final_correction_word = final_cw;

    return {k0, k1};
}

// Recursively evaluate the DPF tree to compute outputs for all leaves
void eval_recursive(const DPFKey& key, int level, Seed current_seed, bool current_flag, std::vector<u64>& result, u64 current_path_val) {
    // Base case: we've reached a leaf node
    if (level == key.correction_words.size()) {
        u64 final_val = (u64)current_seed;
        // Apply final correction word if the flag is set
        if (current_flag) { 
            final_val ^= key.final_correction_word;
        }
        result[current_path_val] = final_val;
        return;
    }

    // Get correction word for this level
    const CorrectionWord& cw = key.correction_words[level];
    PRGOutput out = prg_expand(current_seed);

    // Apply corrections to both children based on control bit
    Seed s_left_mod = out.s_left ^ (current_flag ? cw.s_cw_left : 0);
    Seed s_right_mod = out.s_right ^ (current_flag ? cw.s_cw_right : 0);
    bool t_left_mod = out.t_left ^ (current_flag ? cw.t_cw_left : 0);
    bool t_right_mod = out.t_right ^ (current_flag ? cw.t_cw_right : 0);

    // Recursively evaluate both subtrees
    eval_recursive(key, level + 1, s_left_mod, t_left_mod, result, current_path_val << 1);
    eval_recursive(key, level + 1, s_right_mod, t_right_mod, result, (current_path_val << 1) | 1);
}

// Evaluate the DPF on the entire domain and return all outputs
std::vector<u64> EvalFull(const DPFKey& key, u64 domain_size) {
    std::vector<u64> result(domain_size, 0);
    eval_recursive(key, 0, key.initial_seed, key.initial_flag, result, 0);
    return result;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <DPF_size> <num_DPFs>" << std::endl;
        return 1;
    }
    
    if (ENABLE_LOGGING) {
        logFile.open("dpf_debug.log");
        if (!logFile.is_open()) {
            std::cerr << "Failed to open dpf_debug.log for writing." << std::endl;
            return 1;
        }
    }

    u64 dpf_size = std::stoull(argv[1]);
    int num_dpfs = std::stoi(argv[2]);
    int domain_bits = static_cast<int>(log2(dpf_size));

    // DPF construction requires domain size to be a power of 2 (binary tree structure)
    if ((1ULL << domain_bits) != dpf_size) {
        std::cerr << "Error: DPF_size must be a power of 2." << std::endl;
        return 1;
    }
    
    // Random number generation for test cases
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<u64> loc_dist(0, dpf_size - 1);
    std::uniform_int_distribution<u64> val_dist;

    // Run multiple tests to verify correctness
    for (int i = 0; i < num_dpfs; ++i) {
        u64 location = loc_dist(gen);
        u64 value = val_dist(gen);
        if (value == 0) value = 1;  // Avoid zero values for clearer testing

        std::cout << "--- Test " << i + 1 << "/" << num_dpfs << " ---" << std::endl;
        std::cout << "Generating DPF of size " << dpf_size << " for location=" << location << ", value=" << value << std::endl;

        // Generate the key pair
        auto [k0, k1] = generateDPF(location, value, domain_bits);
        
        // Each party evaluates their key on the full domain
        std::vector<u64> eval0 = EvalFull(k0, dpf_size);
        std::vector<u64> eval1 = EvalFull(k1, dpf_size);

        // Verify correctness: XORing the outputs should give value at location, 0 elsewhere
        bool passed = true;
        for (u64 j = 0; j < dpf_size; ++j) {
            u64 reconstructed_value = eval0[j] ^ eval1[j];
            u64 expected = (j == location) ? value : 0;
            if (reconstructed_value != expected) {
                passed = false;
                break;
            }
        }

        if (passed) {
            std::cout << "Result:Test Passed" << std::endl;
        } else {
            std::cout << "Result:Test Failed" << std::endl;
        }
    }
    
    if (ENABLE_LOGGING) {
        logFile.close();
        std::cout << "\nDebug logs saved to dpf_debug.log" << std::endl;
    }

    return 0;
}