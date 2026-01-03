# CS670: Assignment 3 & 4 - Secure Item Profile Updates using MPC

**Course:** CS670 - Cryptographic Techniques for Privacy Preservation  
**Instructor:** Adithya Vadapalli  
**Author:** Shriya Garg(221038)

This project implements a secure Multi-Party Computation (MPC) protocol for updating item profiles in a recommendation system using Distributed Point Functions (DPF). The protocol allows servers to update item profiles without revealing which item was queried or the update value itself.

## Problem Statement

In a recommendation system, when a user issues a query, item profiles need to be updated according to:

$$v_j \leftarrow v_j + u_i(1 - \langle u_i, v_j \rangle)$$

where:
- $u_i$ is the user's profile vector (of dimension $k$)
- $v_j$ is the profile vector of the queried item $j$ (of dimension $k$)
- $\langle u_i, v_j \rangle$ denotes the dot product

**Security Requirements:**
1. The servers (who hold secret shares of item profiles) must not know which item $j$ is being updated
2. The user (who knows which item was queried) must not know the update value $M = u_i(1 - \langle u_i, v_j \rangle)$, since it depends on $v_j$ which is shared between servers

## Overview

This implementation uses a three-party architecture:
- **P0 and P1 (Servers):** Hold additive secret shares of user profiles $U$ and item profiles $V$, perform secure computation
- **P2 (Helper Party):** Provides correlated randomness (Beaver triples) for secure multiplication operations
- **Client (Data Generator):** Generates initial secret shares and queries, including DPF keys for private updates

The protocol securely computes the update without revealing sensitive information to any party.

## Implementation Details

### 1. Secret Sharing Scheme

We use **additive secret sharing** over 64-bit integers:
- A secret value $x$ is split into shares $(x_0, x_1)$ such that $x = x_0 + x_1$
- P0 holds share $x_0$, P1 holds share $x_1$
- Arithmetic is performed naturally (using integer overflow as modulo $2^{64}$)

All matrices ($U$ and $V$) are stored as additive shares:
- $U_0$, $U_1$: Shares of user profile matrix $U$ (dimensions $m \times k$)
- $V_0$, $V_1$: Shares of item profile matrix $V$ (dimensions $n \times k$)

### 2. Distributed Point Function (DPF)

DPF allows us to encode a value $M$ at a specific index $j$ in a vector, such that:
- Each party receives a key that they can evaluate independently
- The evaluation outputs XOR shares: $(r_0, r_1)$ where $r_0 \oplus r_1 = e_j$ (one-hot vector with 1 at position $j$)
- The encoded value is distributed additively in the final correction word (FCW)

**DPF Key Structure:**
- `s_root`: Root seed
- `f_root`: Root flag bit
- `cws`: Vector of correction words (one per level of the tree)
- `FCW`: Final correction word (additively shared: $FCW_0 + FCW_1 = FCW$)
- `sign`: Sign field used for XOR-to-additive conversion

### 3. Protocol Steps

For each query $(i, j)$:

#### Step 1: User Profile Update (from Assignment 1)
The user profile is updated first:
$$u_i \leftarrow u_i + v_j - v_j \cdot \langle u_i, v_j \rangle$$

This involves:
1. **Oblivious Lookup:** Securely retrieve shares of $v_j$ using rotation trick
2. **Secure Dot Product:** Compute $\langle u_i, v_j \rangle$ using Du-Atallah protocol
3. **Secure Scalar-Vector Multiplication:** Compute $v_j \cdot \langle u_i, v_j \rangle$
4. **Local Update:** Add the update term to $u_i$

#### Step 2: Server-side Computation of Update Value
Each server computes its share of the update term:
$$M = u_i(1 - \langle u_i, v_j \rangle)$$

Since $1 - \langle u_i, v_j \rangle$ is already computed, servers compute:
- $M_0 = u_i^0 \cdot (1 - \langle u_i, v_j \rangle)_0$ using secure scalar-vector multiplication
- $M_1 = u_i^1 \cdot (1 - \langle u_i, v_j \rangle)_1$ (same computation)

#### Step 3: Adjusting DPF Final Correction Word
The client initially generates DPF keys with $FCW = 0$ (pointing to index $j$ with value 0). Servers now adjust the FCW to encode the actual update value $M$:

1. Each server computes masked difference: $masked\_diff_b = M_b - FCW_b$
2. Servers exchange these masked differences
3. Both compute: $FCW_m = (M_0 - FCW_0) + (M_1 - FCW_1)$
4. Each server modifies its DPF key: $k_b.FCW = FCW_m$

#### Step 4: Applying the Update
Each server evaluates its modified DPF key using `EvalFull()`:
- This outputs a vector with the update value at position $j$ and zeros elsewhere
- Due to the sign field, the output is already in additive form
- For each feature $f \in [0, k)$, servers update: $V_b[:, f] \leftarrow V_b[:, f] + EvalFull(k_b, n)[:]$

### 4. Secure Multiplications

We use the **Du-Atallah protocol** (with P2 providing Beaver triples) for secure multiplications:

**Secure Dot Product:** $\langle x, y \rangle$
1. P2 provides Beaver triples $(a, b, c)$ where $c = a \cdot b$
2. Parties compute and exchange masked values: $d = x - a$, $e = y - b$
3. Each party computes its share: $z_b = x_b \cdot (y_b + e) - b_b \cdot d + c_b$

**Secure Scalar-Vector Multiplication:** $\alpha \cdot v$
1. P2 provides triples $(A, B, C)$ where $C = A \cdot B$ (element-wise)
2. Similar masking and reconstruction protocol

### 5. Oblivious Lookup (Rotation Trick)

To securely retrieve $v_j$ without revealing $j$:
1. P2 provides a random one-hot vector $r$ (rotated)
2. Parties exchange $diff_b = j_b - a_b$ where $a$ is the rotation amount
3. Both reconstruct rotation amount $d = diff_0 + diff_1$
4. Each applies rotation: $e_j = rotate(r, d)$
5. Compute $v_j = V^T \cdot e_j$ using secure dot products

## File Structure

```
A3-A4/
├── constants.hpp   # Configuration: M, N, K, Q values
├── common.hpp       # Shared code for P0/P1/P2 (DPF, networking, MPC functions)
├── utils.hpp       # Utilities for local tools (DPF without Boost dependencies)
├── gen_queries.cpp   # Generate initial matrices and queries (runs locally)
├── check_correctness.cpp  # Verify MPC correctness (runs locally)
├── pB.cpp     # Implementation for parties P0 and P1 (runs in Docker)
├── p2.cpp               # Implementation for helper party P2 (runs in Docker)
├── Dockerfile           # Docker build configuration
├── docker-compose.yml   # Docker orchestration for all parties
├── run_benchmark.py     # Benchmarking script for Assignment 4
├── A4/       # Contains result graphs for assignment 4
└── data/     # Generated data files (matrices, queries, updated shares)
```

## Prerequisites

- **Operating System:** Windows 10/11 (PowerShell) or Linux/Mac (bash)
- **Docker Desktop** installed and running
- **C++ Compiler** with C++20 support (g++ 12+ or MSVC 2019+)
- **Python 3.7+** (optional, for benchmarking)

## Configuration

All protocol parameters are defined in `constants.hpp`:

```cpp
constexpr uint32_t M = 3;  // Number of users
constexpr uint32_t N = 5;  // Number of items
constexpr uint32_t K = 3;  // Number of features (latent dimension)
constexpr uint32_t Q = 10; // Number of queries
```

Modify these values as needed for your experiments.

## Running Instructions

### Step 1: Generate Initial Data and Queries

Compile and run the data generator:

```powershell
# On Windows (PowerShell)
g++ -std=c++20 gen_queries.cpp -o gen_queries.exe
New-Item -ItemType Directory -Force -Path "data" | Out-Null
.\gen_queries.exe .\data
```

```bash
# On Linux/Mac
g++ -std=c++20 gen_queries.cpp -o gen_queries
mkdir -p data
./gen_queries ./data
```

This generates:
- `data/U0.txt`, `data/U1.txt`: Initial shares of user profile matrix
- `data/V0.txt`, `data/V1.txt`: Initial shares of item profile matrix
- `data/queries_p0.bin`, `data/queries_p1.bin`: Binary query files (contain user index, item share, and DPF key)
- `data/queries_cleartext.txt`: Cleartext queries for correctness checking

### Step 2: Run MPC Protocol

Start all parties using Docker Compose:

```powershell
# On Windows
docker-compose down
docker rm -f p2 p1 p0 2>$null
docker-compose up --build --force-recreate
```

```bash
# On Linux/Mac
docker-compose down
docker rm -f p2 p1 p0 2>/dev/null
docker-compose up --build --force-recreate
```

**What happens:**
- P0, P1, and P2 containers start and establish network connections
- Each party loads its shares and queries from `./data/`
- Parties process all queries sequentially, performing secure updates
- Updated shares are written to `data/U0_updated.txt`, `data/U1_updated.txt`, `data/V0_updated.txt`, `data/V1_updated.txt`
- Performance metrics are printed to console (parsed directly by benchmark script)

**Wait for completion:** Look for "P0: All queries processed" message in the console.

### Step 3: Verify Correctness

Run the correctness checker:

```powershell
# On Windows
g++ -std=c++20 check_correctness.cpp -o check_correctness.exe
.\check_correctness.exe
```

```bash
# On Linux/Mac
g++ -std=c++20 check_correctness.cpp -o check_correctness
./check_correctness
```

**Expected output:**
```
========================================
MPC Correctness Verification
========================================
Parameters: m=3, n=5, k=3, q=10
Loading initial shares...
Loading updated shares...
Processing queries...
========================================
   SUCCESS: MPC result matches cleartext.
   All updates were computed correctly!
========================================
```

The checker:
1. Loads initial and updated shares
2. Loads cleartext queries
3. Performs cleartext simulation of both user and item profile updates
4. Compares results with MPC output
5. Reports any mismatches


### Quick Benchmark

The provided `run_benchmark.py` script automates benchmarking:

```powershell
python run_benchmark.py --yes
```

This will:
- Run multiple configurations with varying parameters
- Parse timing data from Docker console output
- Generate plots in `benchmark_results.png`

See the script for configuration options.

## Code Organization

### Header Files

- **`constants.hpp`:** Centralized configuration parameters
- **`common.hpp`:** Shared code for Docker containers (DPF, secure computation primitives, Boost networking)
- **`utils.hpp`:** Utilities for local programs (DPF without Boost, file I/O helpers)

### Source Files

- **`gen_queries.cpp`:** 
  - Generates random user/item profiles
  - Creates additive shares by splitting values
  - Generates random queries $(i, j)$
  - Creates DPF keys pointing to item $j$ with initial value 0
  - Writes binary query files and cleartext query file

- **`pB.cpp`:** 
  - Implements parties P0 and P1
  - Compiled twice with different `-DROLE_p0` or `-DROLE_p1` flags
  - Performs secure user and item profile updates
  - Uses coroutines (C++20) for asynchronous networking

- **`p2.cpp`:** 
  - Implements helper party P2
  - Generates and distributes Beaver triples for secure multiplications
  - Provides correlated randomness for oblivious lookup

- **`check_correctness.cpp`:** 
  - Loads initial and updated shares
  - Performs cleartext simulation
  - Verifies correctness of MPC protocol

## Key Design Decisions

1. **Additive Secret Sharing:** Simple and efficient for additions; requires secure multiplications
2. **DPF for Private Updates:** Allows updating a specific item without revealing which one
3. **Beaver Triples:** Offloads secure multiplication to a helper party (P2)
4. **Asynchronous Networking:** Uses C++20 coroutines with Boost.Asio for efficient I/O
5. **File-based I/O:** Data files are read/written directly, simplifying deployment

## Security Model

We assume a **semi-honest adversary model**:
- Parties follow the protocol but may try to infer information from received messages
- No party colludes with others
- Network is authenticated (parties know who they're communicating with)

The protocol ensures:
- **Input Privacy:** P0 and P1 cannot determine which item $j$ is being updated
- **Update Privacy:** The user cannot determine the update value $M$
- **Profile Privacy:** User/item profiles remain secret-shared throughout
