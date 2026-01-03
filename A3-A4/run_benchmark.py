#!/usr/bin/env python3
"""
Assignment 4: Benchmark Script
Runs MPC protocol with varying parameters and plots results directly (no CSV files).

This script:
1. Modifies constants.hpp with different parameter values
2. Runs the full workflow (gen_queries -> docker -> extract results)
3. Parses timing data from Docker console output
4. Plots results directly in memory without saving CSVs
"""

import subprocess
import re
import matplotlib
# Use non-interactive backend (better for Windows)
matplotlib.use('Agg')  # Use before importing pyplot
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
import sys
import os
import time

def update_constants(m, n, k, q):
    """Update constants.hpp with new parameter values"""
    constants_file = Path("constants.hpp")
    content = constants_file.read_text()
    
    # Replace the constant values
    content = re.sub(r'constexpr uint32_t M = \d+;', f'constexpr uint32_t M = {m};', content)
    content = re.sub(r'constexpr uint32_t N = \d+;', f'constexpr uint32_t N = {n};', content)
    content = re.sub(r'constexpr uint32_t K = \d+;', f'constexpr uint32_t K = {k};', content)
    content = re.sub(r'constexpr uint32_t Q = \d+;', f'constexpr uint32_t Q = {q};', content)
    
    constants_file.write_text(content)
    print(f"Updated constants.hpp: m={m}, n={n}, k={k}, q={q}")

def run_command(cmd, cwd=None, shell=False):
    """Run a command and return stdout"""
    if isinstance(cmd, str) and not shell:
        cmd = cmd.split()
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, shell=shell)
    if result.returncode != 0:
        print(f"Error running command: {cmd}")
        print(result.stderr)
    return result.stdout, result.stderr

def parse_timing_from_logs(log_output):
    """Parse timing data from Docker console output"""
    # Pattern: Query 0: user=0.000123s, item=0.000456s
    query_pattern = r'Query (\d+): user=([\d.]+)s, item=([\d.]+)s'
    
    queries = []
    user_times = []
    item_times = []
    
    for line in log_output.split('\n'):
        match = re.search(query_pattern, line)
        if match:
            query_idx = int(match.group(1))
            user_time = float(match.group(2))
            item_time = float(match.group(3))
            queries.append(query_idx)
            user_times.append(user_time)
            item_times.append(item_time)
    
    # Also look for average times
    avg_user_match = re.search(r'Average user profile update time: ([\d.]+)', log_output)
    avg_item_match = re.search(r'Average item profile update time: ([\d.]+)', log_output)
    
    # Also check for the single-line format: user_update_time: <value>
    if not avg_user_match:
        avg_user_match = re.search(r'user_update_time: ([\d.]+)', log_output)
    if not avg_item_match:
        avg_item_match = re.search(r'item_update_time: ([\d.]+)', log_output)
    
    avg_user = float(avg_user_match.group(1)) if avg_user_match else None
    avg_item = float(avg_item_match.group(1)) if avg_item_match else None
    
    return {
        'queries': queries,
        'user_times': user_times,
        'item_times': item_times,
        'avg_user': avg_user,
        'avg_item': avg_item
    }

def run_single_benchmark(m, n, k, q, work_dir):
    """Run a single benchmark with given parameters"""
    print(f"\n{'='*60}")
    print(f"Running benchmark: m={m}, n={n}, k={k}, q={q}")
    print(f"{'='*60}")
    
    # Update constants
    update_constants(m, n, k, q)
    
    # Step 1: Generate data and queries
    print("\n[1/3] Generating initial data and queries...")
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    
    gen_queries_exe = f"gen_queries{exe_suffix}"
    
    run_command(["g++", "-std=c++20", "gen_queries.cpp", "-o", gen_queries_exe], cwd=work_dir)
    
    data_dir = work_dir / "data"
    data_dir.mkdir(exist_ok=True)
    
    run_command([gen_queries_exe, str(data_dir)], cwd=work_dir)
    
    # Step 2: Run Docker
    print("\n[2/3] Running MPC protocol in Docker...")
    docker_compose_path = work_dir / "docker-compose.yml"
    
    # Clean up first
    run_command(["docker-compose", "down"], cwd=work_dir)
    run_command(["docker", "rm", "-f", "p2", "p1", "p0"], cwd=work_dir)
    
    # Run docker-compose and capture output
    # Use shell=True on Windows for better compatibility
    use_shell = sys.platform == "win32"
    result = subprocess.run(
        ["docker-compose", "up", "--build", "--force-recreate"],
        cwd=work_dir,
        capture_output=True,
        text=True,
        shell=use_shell
    )
    
    log_output = result.stdout + result.stderr
    
    # Step 3: Parse timing data
    print("\n[3/3] Parsing timing data...")
    timing_data = parse_timing_from_logs(log_output)
    
    if not timing_data['queries']:
        print(f"Warning: No timing data found for m={m}, n={n}, k={k}, q={q}")
        return None
    
    print(f"Found {len(timing_data['queries'])} timing measurements")
    if timing_data['avg_user']:
        print(f"Average User Update Time: {timing_data['avg_user']:.9f} seconds")
    if timing_data['avg_item']:
        print(f"Average Item Update Time: {timing_data['avg_item']:.9f} seconds")
    
    return timing_data

def plot_results(all_results, output_dir):
    """Plot all benchmark results (deprecated - kept for compatibility)

    This project now produces individual plots for each sweep. This
    function remains for backwards compatibility but will simply notify
    the user and return.
    """
    print("plot_results() is deprecated; individual plots are saved per-sweep.")
    return


def save_single_plot(x_vals, y_vals, xlabel, ylabel, title, outpath, marker='o'):
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(x_vals, y_vals, marker=marker)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    plt.savefig(outpath, dpi=300, bbox_inches='tight')
    plt.close(fig)

def plot_time_vs_queries(all_results, ax):
    """Plot average time vs number of queries"""
    # Group by q (queries), keep other params constant
    q_results = {}
    for params, data in all_results.items():
        m, n, k, q = params
        if data and data['avg_user'] and data['avg_item']:
            if (m, n, k) not in q_results:
                q_results[(m, n, k)] = {'q': [], 'user_time': [], 'item_time': []}
            q_results[(m, n, k)]['q'].append(q)
            q_results[(m, n, k)]['user_time'].append(data['avg_user'])
            q_results[(m, n, k)]['item_time'].append(data['avg_item'])
    
    for (m, n, k), values in q_results.items():
        if len(values['q']) > 1:
            sorted_data = sorted(zip(values['q'], values['user_time'], values['item_time']))
            q_vals, user_vals, item_vals = zip(*sorted_data)
            ax.plot(q_vals, user_vals, marker='o', label=f'User (m={m},n={n},k={k})')
            ax.plot(q_vals, item_vals, marker='s', linestyle='--', label=f'Item (m={m},n={n},k={k})')
    
    ax.set_xlabel('Number of Queries (q)')
    ax.set_ylabel('Average Time (seconds)')
    ax.set_title('Time vs Number of Queries')
    ax.legend()
    ax.grid(True, alpha=0.3)

def plot_time_vs_users(all_results, ax):
    """Plot average time vs number of users"""
    # Group by m (users), keep other params constant
    m_results = {}
    for params, data in all_results.items():
        m, n, k, q = params
        if data and data['avg_user'] and data['avg_item']:
            if (n, k, q) not in m_results:
                m_results[(n, k, q)] = {'m': [], 'user_time': [], 'item_time': []}
            m_results[(n, k, q)]['m'].append(m)
            m_results[(n, k, q)]['user_time'].append(data['avg_user'])
            m_results[(n, k, q)]['item_time'].append(data['avg_item'])
    
    for (n, k, q), values in m_results.items():
        if len(values['m']) > 1:
            sorted_data = sorted(zip(values['m'], values['user_time'], values['item_time']))
            m_vals, user_vals, item_vals = zip(*sorted_data)
            ax.plot(m_vals, user_vals, marker='o', label=f'User (n={n},k={k},q={q})')
            ax.plot(m_vals, item_vals, marker='s', linestyle='--', label=f'Item (n={n},k={k},q={q})')
    
    ax.set_xlabel('Number of Users (m)')
    ax.set_ylabel('Average Time (seconds)')
    ax.set_title('Time vs Number of Users')
    ax.legend()
    ax.grid(True, alpha=0.3)

def plot_time_vs_items(all_results, ax):
    """Plot average time vs number of items"""
    # Group by n (items), keep other params constant
    n_results = {}
    for params, data in all_results.items():
        m, n, k, q = params
        if data and data['avg_user'] and data['avg_item']:
            if (m, k, q) not in n_results:
                n_results[(m, k, q)] = {'n': [], 'user_time': [], 'item_time': []}
            n_results[(m, k, q)]['n'].append(n)
            n_results[(m, k, q)]['user_time'].append(data['avg_user'])
            n_results[(m, k, q)]['item_time'].append(data['avg_item'])
    
    for (m, k, q), values in n_results.items():
        if len(values['n']) > 1:
            sorted_data = sorted(zip(values['n'], values['user_time'], values['item_time']))
            n_vals, user_vals, item_vals = zip(*sorted_data)
            ax.plot(n_vals, user_vals, marker='o', label=f'User (m={m},k={k},q={q})')
            ax.plot(n_vals, item_vals, marker='s', linestyle='--', label=f'Item (m={m},k={k},q={q})')
    
    ax.set_xlabel('Number of Items (n)')
    ax.set_ylabel('Average Time (seconds)')
    ax.set_title('Time vs Number of Items')
    ax.legend()
    ax.grid(True, alpha=0.3)

def plot_comparison(all_results, ax):
    """Plot comparison of user vs item update times"""
    user_times = []
    item_times = []
    labels = []
    
    for params, data in all_results.items():
        m, n, k, q = params
        if data and data['avg_user'] and data['avg_item']:
            user_times.append(data['avg_user'])
            item_times.append(data['avg_item'])
            labels.append(f"m={m}\nn={n}\nk={k}\nq={q}")
    
    x = np.arange(len(labels))
    width = 0.35
    
    ax.bar(x - width/2, user_times, width, label='User Update', alpha=0.8)
    ax.bar(x + width/2, item_times, width, label='Item Update', alpha=0.8)
    
    ax.set_ylabel('Average Time (seconds)')
    ax.set_title('User vs Item Update Time Comparison')
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=45, ha='right', fontsize=8)
    ax.legend()
    ax.grid(True, alpha=0.3, axis='y')

def main():
    """Main benchmark function"""
    # Parse command-line arguments
    skip_prompt = "--yes" in sys.argv or "-y" in sys.argv
    dry_run = "--dry-run" in sys.argv or "-d" in sys.argv
    
    work_dir = Path(__file__).parent
    os.chdir(work_dir)
    
    print("="*60)
    print("Assignment 4: MPC Protocol Benchmark")
    print("="*60)
    print("\nThis script will run the protocol with different parameters")
    print("and plot results directly (no CSV files needed).\n")
    
    # We'll run three separate sweeps (5 points each):
    #  - Vary q (queries) from 5 to 40
    #  - Vary m (users) from 1 to 50
    #  - Vary n (items) from 1 to 50
    # For each sweep, keep the other parameters fixed at sensible defaults.

    default_m = 10
    default_n = 20
    default_k = 3
    default_q = 10

    qs = np.linspace(5, 40, 5, dtype=int)
    ms = np.linspace(1, 50, 5, dtype=int)
    ns = np.linspace(1, 50, 5, dtype=int)

    output_dir = work_dir / "A4"
    output_dir.mkdir(exist_ok=True)

    print("Sweep parameters:")
    print(f"  queries (q): {list(qs)}")
    print(f"  users (m): {list(ms)}")
    print(f"  items (n): {list(ns)}")

    if not skip_prompt:
        response = input("\nProceed with all benchmarks? This will run multiple full protocol runs and may take a long time. (y/n): ")
        if response.lower() != 'y':
            print("Cancelled.")
            return
    else:
        print("\nProceeding automatically (--yes flag provided)...")

    # Helper to run a sweep and return arrays
    def run_sweep(param_name, values):
        user_times = []
        item_times = []
        for idx, v in enumerate(values, 1):
            print(f"\n[{param_name} sweep] {idx}/{len(values)}: {param_name}={v}")
            if param_name == 'q':
                m, n, k, q = default_m, default_n, default_k, int(v)
            elif param_name == 'm':
                m, n, k, q = int(v), default_n, default_k, default_q
            elif param_name == 'n':
                m, n, k, q = default_m, int(v), default_k, default_q
            else:
                raise ValueError('Unknown sweep param')

            if dry_run:
                # Generate synthetic timings for quick verification
                base = 0.0005
                # simple monotonic function so plots look reasonable
                user_t = base * (1 + float(v) / max(1.0, float(values[-1])))
                item_t = base * (1 + 0.5 * float(v) / max(1.0, float(values[-1])))
                # add tiny deterministic noise
                user_t += 1e-6 * idx
                item_t += 2e-6 * idx
                user_times.append(user_t)
                item_times.append(item_t)
            else:
                td = run_single_benchmark(m, n, k, q, work_dir)
                if td and td.get('avg_user') is not None:
                    user_times.append(td['avg_user'])
                else:
                    user_times.append(float('nan'))
                if td and td.get('avg_item') is not None:
                    item_times.append(td['avg_item'])
                else:
                    item_times.append(float('nan'))

            # small pause between runs
            time.sleep(1)

        return user_times, item_times

    # Run the three sweeps
    print("\nRunning q sweep...")
    q_user_times, q_item_times = run_sweep('q', qs)
    print("\nRunning m sweep...")
    m_user_times, m_item_times = run_sweep('m', ms)
    print("\nRunning n sweep...")
    n_user_times, n_item_times = run_sweep('n', ns)

    # Save the six required graphs (3 variables * 2 update types)
    print("\nSaving plots to A4/ folder...")

    save_single_plot(qs, q_user_times,
                     'Number of Queries (q)', 'Average Time (s)',
                     'User Update Time vs Number of Queries',
                     output_dir / 'q_vs_user_update.png')

    save_single_plot(qs, q_item_times,
                     'Number of Queries (q)', 'Average Time (s)',
                     'Item Update Time vs Number of Queries',
                     output_dir / 'q_vs_item_update.png')

    save_single_plot(ms, m_user_times,
                     'Number of Users (m)', 'Average Time (s)',
                     'User Update Time vs Number of Users',
                     output_dir / 'm_vs_user_update.png')

    save_single_plot(ms, m_item_times,
                     'Number of Users (m)', 'Average Time (s)',
                     'Item Update Time vs Number of Users',
                     output_dir / 'm_vs_item_update.png')

    save_single_plot(ns, n_user_times,
                     'Number of Items (n)', 'Average Time (s)',
                     'User Update Time vs Number of Items',
                     output_dir / 'n_vs_user_update.png')

    save_single_plot(ns, n_item_times,
                     'Number of Items (n)', 'Average Time (s)',
                     'Item Update Time vs Number of Items',
                     output_dir / 'n_vs_item_update.png')

    print(f"\nPlots saved in: {output_dir}")
    print("\nBenchmark complete!")

if __name__ == "__main__":
    main()

