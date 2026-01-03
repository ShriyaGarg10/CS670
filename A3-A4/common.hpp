#pragma once

#include <utility>
#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <string>
#include <fstream>
#include <chrono>
#include <numeric>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/this_coro.hpp>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
namespace this_coro = boost::asio::this_coro;

using u64 = uint64_t;
using ShareVec = std::vector<int64_t>;
using ShareMat = std::vector<ShareVec>;

struct ChildSeed {
    u64 s_left, s_right;
    bool f_left, f_right;
};

struct CorrectionWord {
    u64 scw;
    bool fcw_0;
    bool fcw_1;
};

struct DPFKey {
    u64 s_root;
    bool f_root;
    std::vector<CorrectionWord> cws;
    int64_t FCW;
    int sign;
};

inline int8_t random_int8() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int16_t> dis(-128, 127);
    return (int8_t)dis(gen);
}

inline int32_t random_int32() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int32_t> dis(-128, 127);
    return dis(gen);
}

inline uint8_t random_uint8() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint8_t> dis;
    return dis(gen);
}

inline uint32_t random_uint32() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;
    return dis(gen);
}

inline ChildSeed PRG(u64 seed) {
    std::mt19937 engine(seed);
    ChildSeed child;
    child.s_left = (uint8_t)engine();
    child.s_right = (uint8_t)engine();
    child.f_left = (engine() % 2 == 1);
    child.f_right = (engine() % 2 == 1);
    return child;
}

inline std::mt19937& get_prg_engine() {
    static std::random_device rd;
    static std::mt19937 engine(rd());
    return engine;
}

inline std::pair<DPFKey, DPFKey> generateDPF(u64 index, int64_t value, u64 domain_size) {
    int depth = (domain_size == 0) ? 0 : ceil(log2(domain_size));
    if (depth == 0) { depth = 1; domain_size = 2; }

    DPFKey k0, k1;

    u64 s0_curr = random_uint8();
    u64 s1_curr = random_uint8();
    bool f0_curr = 0;
    bool f1_curr = 1;

    k0.s_root = s0_curr;
    k1.s_root = s1_curr;
    k0.f_root = f0_curr;
    k1.f_root = f1_curr;

    for(int i=0;i<depth;i++) {
        ChildSeed c0 = PRG(s0_curr);
        ChildSeed c1 = PRG(s1_curr);
        bool path_bit = (index >> (depth - 1 - i)) & 1;
        bool f0_next, f1_next;
        CorrectionWord cw;

        if (path_bit == 0) {
            cw.scw = c0.s_right ^ c1.s_right;
            cw.fcw_1 = c0.f_right ^ c1.f_right;
            cw.fcw_0 = c0.f_left ^ c1.f_left ^ 1;
            s0_curr = c0.s_left; s1_curr = c1.s_left;
            f0_next = c0.f_left; f1_next = c1.f_left;
        } else {
            cw.scw = c0.s_left ^ c1.s_left;
            cw.fcw_0 = c0.f_left ^ c1.f_left;
            cw.fcw_1 = c0.f_right ^ c1.f_right ^ 1;
            s0_curr = c0.s_right; s1_curr = c1.s_right;
            f0_next = c0.f_right; f1_next = c1.f_right;
        }
        if (f0_curr) {
            s0_curr ^= cw.scw;
            f0_next ^= (path_bit == 0) ? cw.fcw_0 : cw.fcw_1;
        }
        if (f1_curr) {
            s1_curr ^= cw.scw;
            f1_next ^= (path_bit == 0) ? cw.fcw_0 : cw.fcw_1;
        }
        f0_curr = f0_next; f1_curr = f1_next;
        k0.cws.push_back(cw); k1.cws.push_back(cw);
    }

    int64_t s0_final = (int64_t)s0_curr;
    int64_t s1_final = (int64_t)s1_curr;
    int64_t R = random_int8();

    k0.sign = f0_curr * 1 + (1-f0_curr) * (-1);
    k1.sign = f1_curr * 1 + (1-f1_curr) * (-1);

    k0.FCW = R + k0.sign * s0_final;
    k1.FCW = (value - R) + k1.sign * s1_final;

    return {k0, k1};
}

inline int64_t evalDPF(const DPFKey& key, u64 index, u64 domain_size) {
    int depth = (domain_size == 0) ? 0 : ceil(log2(domain_size));
    if (depth == 0) { depth = 1; domain_size = 2; }

    u64 s_curr = key.s_root;
    bool f_curr = key.f_root;

    for(int i=0; i<depth; i++) {
        ChildSeed ch = PRG(s_curr);
        bool path_bit = (index >> (depth - 1 - i)) & 1;
        bool f_next;
        if(path_bit == 0){ s_curr = ch.s_left; f_next = ch.f_left; }
        else { s_curr = ch.s_right; f_next = ch.f_right; }
        if(f_curr){
            s_curr ^= key.cws[i].scw;
            f_next ^= (path_bit == 0) ? key.cws[i].fcw_0 : key.cws[i].fcw_1;
        }
        f_curr = f_next;
    }

    int64_t value = (int64_t)s_curr;

    if(f_curr) {
        value += key.FCW;
    }

    return value * key.sign;
}

inline std::vector<int64_t> EvalFull(const DPFKey& k, u64 domain_size) {
    std::vector<int64_t> result(domain_size);
    for (u64 i=0; i<domain_size; i++) {
        result[i] = evalDPF(k, i, domain_size);
    }
    return result;
}

inline void write_key(std::ostream& out, const DPFKey& key) {
    out.write(reinterpret_cast<const char*>(&key.s_root), sizeof(key.s_root));
    out.write(reinterpret_cast<const char*>(&key.f_root), sizeof(key.f_root));
    out.write(reinterpret_cast<const char*>(&key.FCW), sizeof(key.FCW));
    out.write(reinterpret_cast<const char*>(&key.sign), sizeof(key.sign));
    
    size_t cw_size = key.cws.size();
    out.write(reinterpret_cast<const char*>(&cw_size), sizeof(cw_size));
    if (cw_size > 0) {
        out.write(reinterpret_cast<const char*>(key.cws.data()), cw_size * sizeof(CorrectionWord));
    }
}

inline DPFKey read_key(std::istream& in) {
    DPFKey key;
    in.read(reinterpret_cast<char*>(&key.s_root), sizeof(key.s_root));
    in.read(reinterpret_cast<char*>(&key.f_root), sizeof(key.f_root));
    in.read(reinterpret_cast<char*>(&key.FCW), sizeof(key.FCW));
    in.read(reinterpret_cast<char*>(&key.sign), sizeof(key.sign));
    
    size_t cw_size;
    in.read(reinterpret_cast<char*>(&cw_size), sizeof(cw_size));
    key.cws.resize(cw_size);
    if (cw_size > 0) {
        in.read(reinterpret_cast<char*>(key.cws.data()), cw_size * sizeof(CorrectionWord));
    }
    return key;
}

inline ShareVec vec_add(const ShareVec& a, const ShareVec& b) {
    ShareVec result(a.size());
    for (size_t i = 0; i < a.size(); ++i) result[i] = a[i] + b[i];
    return result;
}

inline ShareVec vec_sub(const ShareVec& a, const ShareVec& b) {
    ShareVec result(a.size());
    for (size_t i = 0; i < a.size(); ++i) result[i] = a[i] - b[i];
    return result;
}

inline int64_t vec_dot_product(const ShareVec& a, const ShareVec& b) {
    int64_t result = 0;
    for (size_t i = 0; i < a.size(); ++i) result += a[i] * b[i];
    return result;
}

inline ShareVec vec_scalar_mul(const ShareVec& a, int64_t scalar) {
    ShareVec result(a.size());
    for (size_t i = 0; i < a.size(); ++i) result[i] = a[i] * scalar;
    return result;
}

awaitable<void> send_value(tcp::socket& sock, int64_t value) {
    auto buffer = boost::asio::buffer(&value, sizeof(value));
    co_await boost::asio::async_write(sock, buffer, use_awaitable);
}

awaitable<int64_t> recv_value(tcp::socket& sock) {
    int64_t value;
    auto buffer = boost::asio::buffer(&value, sizeof(value));
    co_await boost::asio::async_read(sock, buffer, use_awaitable);
    co_return value;
}

awaitable<void> send_vector(tcp::socket& sock, const std::vector<int64_t>& vec) {
    int64_t size = vec.size();
    co_await send_value(sock, size);
    if (size > 0) {
        auto buffer = boost::asio::buffer(vec.data(), size * sizeof(int64_t));
        co_await boost::asio::async_write(sock, buffer, use_awaitable);
    }
}

awaitable<std::vector<int64_t>> recv_vector(tcp::socket& sock) {
    int64_t size = co_await recv_value(sock);
    std::vector<int64_t> vec(size);
    if (size > 0) {
        auto buffer = boost::asio::buffer(vec.data(), size * sizeof(int64_t));
        co_await boost::asio::async_read(sock, buffer, use_awaitable);
    }
    co_return vec;
}

awaitable<int64_t> exchange_value(tcp::socket& peer_sock, int64_t value, int ROLE) {
    int64_t other_value;
    if (ROLE == 0) {
        co_await send_value(peer_sock, value);
        other_value = co_await recv_value(peer_sock);
    } else {
        other_value = co_await recv_value(peer_sock);
        co_await send_value(peer_sock, value);
    }
    co_return other_value;
}

struct Query {
    uint32_t user_index;
    int64_t item_share;
    DPFKey dpf_key;
};

inline std::vector<Query> read_queries(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open file for reading: " << filename << std::endl;
        exit(1);
    }
    std::vector<Query> queries;
    while (in.peek() != EOF) {
        Query q;
        in.read(reinterpret_cast<char*>(&q.user_index), sizeof(q.user_index));
        in.read(reinterpret_cast<char*>(&q.item_share), sizeof(q.item_share));
        q.dpf_key = read_key(in);
        if (in.gcount() > 0) {
             queries.push_back(q);
        }
    }
    return queries;
}

inline ShareMat load_matrix_shares(const std::string& filename, uint32_t rows, uint32_t cols) {
    std::ifstream in(filename);
    if (!in) {
        std::cerr << "Cannot open file for reading: " << filename << std::endl;
        exit(1);
    }
    ShareMat M(rows, ShareVec(cols));
    for (uint32_t i = 0; i < rows; ++i) {
        for (uint32_t j = 0; j < cols; ++j) {
            uint32_t val;
            in >> val;
            M[i][j] = static_cast<int64_t>(static_cast<int32_t>(val));
        }
    }
    return M;
}
