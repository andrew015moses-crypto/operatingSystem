#!/usr/bin/env python3
"""
scheduler_sim.py  —  EduOS Scheduling Simulator  (Part 3)
Module : 351 CS 2104 — Operating Systems

HOW THIS FILE IS ORGANISED:
============================
  SECTION 1  — Imports and constants
  SECTION 2  — Process loading  (random / CSV / JSON / pcb_snapshot)
  SECTION 3  — Scheduling algorithms  (FCFS / SJF / Priority / Round Robin)
  SECTION 4  — Metrics calculation    (TAT / WT / RT / utilisation)
  SECTION 5  — Gantt chart drawing    (matplotlib)
  SECTION 6  — Comparison charts      (bar charts)
  SECTION 7  — Comparison table       (Rich / tabulate)
  SECTION 8  — Thread mode            (--mode thread)
  SECTION 9  — Animation              (bonus)
  SECTION 10 — CLI entry point        (argparse)

RUN EXAMPLES:
  python scheduler_sim.py --random 8
  python scheduler_sim.py --random 8 --seed 42
  python scheduler_sim.py --file sample_processes.csv
  python scheduler_sim.py --file ../pcb_snapshot.json
  python scheduler_sim.py --random 6 --quantum 2
  python scheduler_sim.py --random 6 --quantum 4
  python scheduler_sim.py --random 8 --mode thread
"""

# ══════════════════════════════════════════════════════════════
# SECTION 1 — IMPORTS AND CONSTANTS
# ══════════════════════════════════════════════════════════════

import argparse
import copy
import csv
import json
import os
import random
import sys
from typing import List, Dict, Tuple

# Visualisation
import matplotlib
matplotlib.use("Agg")          # non-interactive backend (works without a display)
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.animation as animation

# Table printing
try:
    from rich.console import Console
    from rich.table   import Table
    from rich         import box
    HAS_RICH = True
except ImportError:
    HAS_RICH = False

try:
    from tabulate import tabulate
    HAS_TABULATE = True
except ImportError:
    HAS_TABULATE = False

# Output directory for all PNG charts
SCREENSHOTS_DIR = os.path.join(
    os.path.dirname(__file__), "docs", "screenshots")
os.makedirs(SCREENSHOTS_DIR, exist_ok=True)

# Colour palette for processes (cycles if more than 12)
COLOURS = [
    "#4E79A7","#F28E2B","#E15759","#76B7B2",
    "#59A14F","#EDC948","#B07AA1","#FF9DA7",
    "#9C755F","#BAB0AC","#86BCB6","#D37295",
]

# Context-switch overhead added in thread mode (time units)
CONTEXT_SWITCH_COST = 1

# ══════════════════════════════════════════════════════════════
# SECTION 2 — PROCESS LOADING
# ══════════════════════════════════════════════════════════════

"""
WHAT IS A PROCESS DICTIONARY?
Each process throughout this program is a plain Python dict:
{
    "pid"          : int   — unique ID
    "name"         : str   — process name
    "arrival_time" : int   — when it joins the ready queue
    "burst_time"   : int   — total CPU time it needs
    "priority"     : int   — 0 = highest urgency
    "memory_kb"    : int   — memory footprint (for display)
}
"""

def generate_random_processes(n: int, seed: int = None) -> List[Dict]:
    """
    Generate n processes with random arrival, burst, and priority values.
    If seed is given the results are reproducible — same seed = same processes.
    This lets you compare algorithms on identical workloads.
    """
    if seed is not None:
        random.seed(seed)

    processes = []
    for i in range(n):
        processes.append({
            "pid"          : i + 1,
            "name"         : f"P{i+1}",
            "arrival_time" : random.randint(0, n),
            "burst_time"   : random.randint(1, 15),
            "priority"     : random.randint(0, 5),
            "memory_kb"    : random.choice([128, 256, 512, 1024]),
        })
    return processes


def load_from_csv(filepath: str) -> List[Dict]:
    """
    Load processes from a CSV file.

    Required columns: pid, arrival_time, burst_time, priority
    Optional columns: name, memory_kb

    CSV schema (sample_processes.csv):
        pid,name,arrival_time,burst_time,priority,memory_kb
        1,init,0,8,0,512
    """
    processes = []
    with open(filepath, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            processes.append({
                "pid"          : int(row["pid"]),
                "name"         : row.get("name", f"P{row['pid']}"),
                "arrival_time" : int(row["arrival_time"]),
                "burst_time"   : int(row["burst_time"]),
                "priority"     : int(row["priority"]),
                "memory_kb"    : int(row.get("memory_kb", 256)),
            })
    return processes


def load_from_json(filepath: str) -> List[Dict]:
    """
    Load processes from either:
      1. A plain JSON list:  [ {"pid":1, ...}, ... ]
      2. A pcb_snapshot.json produced by the C process manager (Part 2).

    pcb_snapshot.json schema written by process_manager.c:
        {
          "timestamp": 1234567890,
          "process_count": 3,
          "processes": [
            { "pid":1000, "name":"init", "burst_time":15,
              "arrival_time":0, "priority":0, "memory_kb":1024,
              "state":"READY", ... },
            ...
          ]
        }
    """
    with open(filepath, encoding="utf-8") as f:
        data = json.load(f)

    # pcb_snapshot.json has a "processes" key
    if isinstance(data, dict) and "processes" in data:
        raw = data["processes"]
    else:
        raw = data

    processes = []
    for i, p in enumerate(raw):
        # Skip already-terminated processes from the C snapshot
        if p.get("state", "READY") == "TERMINATED":
            continue
        processes.append({
            "pid"          : p.get("pid", i + 1),
            "name"         : p.get("name", f"P{i+1}"),
            "arrival_time" : p.get("arrival_time", 0),
            "burst_time"   : p.get("burst_time", p.get("remaining_time", 5)),
            "priority"     : p.get("priority", 0),
            "memory_kb"    : p.get("memory_kb", p.get("memory_req_kb", 256)),
        })
    return processes


# ══════════════════════════════════════════════════════════════
# SECTION 3 — SCHEDULING ALGORITHMS
# ══════════════════════════════════════════════════════════════

"""
WHAT EVERY ALGORITHM RETURNS:
A list of [pid, start_time, end_time] tuples called a "schedule".
Example: [[1,0,5], [2,5,9], [1,9,12]]
This means:
  Process 1 ran from time 0 to 5
  Process 2 ran from time 5 to 9
  Process 1 ran again from time 9 to 12 (preemptive only)
"""

# ── ALGORITHM 1: FCFS ──────────────────────────────────────────
def fcfs(processes: List[Dict]) -> List[Tuple]:
    """
    FIRST COME FIRST SERVED (non-preemptive)

    HOW IT WORKS:
    - Sort all processes by arrival_time (ties broken by lower PID first)
    - Run each one to completion before starting the next
    - CPU is idle if no process has arrived yet

    ADVANTAGE : Simple, no starvation
    DISADVANTAGE: Convoy effect — one long job blocks all short ones behind it
    """
    # Sort: primary = arrival_time, secondary = pid (tie-break)
    procs = sorted(processes, key=lambda p: (p["arrival_time"], p["pid"]))

    schedule = []
    current_time = 0

    for p in procs:
        # If CPU is idle, jump to when this process arrives
        if current_time < p["arrival_time"]:
            current_time = p["arrival_time"]

        start = current_time
        end   = current_time + p["burst_time"]
        schedule.append([p["pid"], start, end])
        current_time = end

    return schedule


# ── ALGORITHM 2: SJF ──────────────────────────────────────────
def sjf(processes: List[Dict]) -> List[Tuple]:
    """
    SHORTEST JOB FIRST (non-preemptive)

    HOW IT WORKS:
    - At each decision point, pick the process with the smallest burst_time
      from ALL processes that have arrived by the current time
    - Ties in burst_time are broken by arrival_time, then by pid
    - Run the chosen process to completion (non-preemptive)

    ADVANTAGE : Minimises average waiting time
    DISADVANTAGE: Long processes can starve; requires knowing burst time in advance
    """
    procs     = sorted(processes, key=lambda p: (p["arrival_time"], p["pid"]))
    remaining = list(procs)   # processes not yet scheduled
    schedule  = []
    current_time = 0

    while remaining:
        # Collect all processes that have arrived by current_time
        available = [p for p in remaining if p["arrival_time"] <= current_time]

        if not available:
            # CPU idle — jump to the next arrival
            current_time = min(p["arrival_time"] for p in remaining)
            continue

        # Pick shortest job; tie-break by arrival_time then pid
        chosen = min(available,
                     key=lambda p: (p["burst_time"], p["arrival_time"], p["pid"]))
        remaining.remove(chosen)

        start = current_time
        end   = current_time + chosen["burst_time"]
        schedule.append([chosen["pid"], start, end])
        current_time = end

    return schedule


# ── ALGORITHM 3: PRIORITY (with Ageing) ───────────────────────
def priority_scheduling(processes: List[Dict]) -> List[Tuple]:
    """
    PRIORITY SCHEDULING (non-preemptive) with AGEING

    HOW IT WORKS:
    - At each decision point, pick the process with the LOWEST priority number
      (lower number = higher urgency, e.g. 0 is more urgent than 5)
    - Run the chosen process to completion (non-preemptive)

    AGEING — HOW IT PREVENTS STARVATION:
    Without ageing, a low-priority process might wait forever if high-priority
    processes keep arriving. Ageing fixes this by gradually raising the priority
    of waiting processes.

    AGEING RULE (implemented below):
    Every 3 time units that a process spends in the ready queue,
    its effective priority is reduced by 1 (making it more urgent).
    Example:
      Process X has priority=4, arrives at time=0
      At time=3  → effective priority = 3  (waited 3 units)
      At time=6  → effective priority = 2  (waited 6 units)
      At time=9  → effective priority = 1  (waited 9 units)
      Eventually it becomes priority=0 and gets picked first.

    ADVANTAGE : Prevents starvation
    DISADVANTAGE: More complex; priority inversion possible without ageing
    """
    procs     = sorted(processes, key=lambda p: (p["arrival_time"], p["pid"]))
    remaining = list(procs)
    schedule  = []
    current_time = 0

    while remaining:
        available = [p for p in remaining if p["arrival_time"] <= current_time]

        if not available:
            current_time = min(p["arrival_time"] for p in remaining)
            continue

        def aged_priority(p):
            """
            AGEING FORMULA:
            wait_time = how long this process has been in the ready queue
            age_bonus = wait_time // 3   (one level gained per 3 units waited)
            effective  = original_priority - age_bonus
            Clamped to 0 so priority never goes negative.
            """
            wait_time  = current_time - p["arrival_time"]
            age_bonus  = wait_time // 3
            effective  = max(0, p["priority"] - age_bonus)
            return (effective, p["arrival_time"], p["pid"])

        # Pick process with lowest effective priority (most urgent)
        chosen = min(available, key=aged_priority)
        remaining.remove(chosen)

        start = current_time
        end   = current_time + chosen["burst_time"]
        schedule.append([chosen["pid"], start, end])
        current_time = end

    return schedule


# ── ALGORITHM 4: ROUND ROBIN ───────────────────────────────────
def round_robin(processes: List[Dict], quantum: int = 2) -> List[Tuple]:
    """
    ROUND ROBIN (preemptive)

    HOW IT WORKS:
    - Processes take turns on the CPU, each getting at most `quantum` time units
    - If a process finishes before its quantum expires, it releases the CPU early
    - If it still has work left after the quantum, it goes to the BACK of the queue
    - New arrivals are added to the queue in arrival order

    READY QUEUE MANAGEMENT:
    1. Start with processes that have arrived at time 0
    2. After each time slice, add any new arrivals to the back of the queue
    3. If the running process has remaining_time > 0, add it back to the queue

    ADVANTAGE : Fair — every process gets regular CPU time, good response time
    DISADVANTAGE: High context-switch overhead if quantum is too small;
                  poor throughput if quantum is too large (degrades to FCFS)

    quantum=2 vs quantum=4 behaviour:
      Small quantum → more context switches, better response time
      Large quantum → fewer switches, higher throughput, longer wait for short jobs
    """
    # Work on copies so we don't modify the original list
    procs = sorted(processes, key=lambda p: (p["arrival_time"], p["pid"]))
    remaining = {p["pid"]: p["burst_time"] for p in procs}
    first_run  = {p["pid"]: True for p in procs}   # track first time on CPU

    schedule     = []
    ready_queue  = []
    current_time = 0
    proc_map     = {p["pid"]: p for p in procs}
    arrived      = set()

    # Seed the queue with processes arriving at time 0
    for p in procs:
        if p["arrival_time"] <= current_time:
            ready_queue.append(p["pid"])
            arrived.add(p["pid"])

    while ready_queue or any(remaining[pid] > 0 for pid in remaining):
        if not ready_queue:
            # CPU idle — jump to next arrival
            next_time = min(
                p["arrival_time"] for p in procs
                if p["pid"] not in arrived)
            current_time = next_time
            for p in procs:
                if p["arrival_time"] <= current_time and p["pid"] not in arrived:
                    ready_queue.append(p["pid"])
                    arrived.add(p["pid"])
            continue

        pid       = ready_queue.pop(0)     # take from front of queue
        proc      = proc_map[pid]
        rem       = remaining[pid]
        run_for   = min(quantum, rem)      # run for quantum or until done

        start = current_time
        end   = current_time + run_for
        schedule.append([pid, start, end])
        remaining[pid] -= run_for
        current_time    = end

        # Add newly arrived processes to back of queue
        for p in procs:
            if p["arrival_time"] <= current_time and p["pid"] not in arrived:
                ready_queue.append(p["pid"])
                arrived.add(p["pid"])

        # If not finished, re-queue at the back
        if remaining[pid] > 0:
            ready_queue.append(pid)

    return schedule


# ══════════════════════════════════════════════════════════════
# SECTION 4 — METRICS CALCULATION
# ══════════════════════════════════════════════════════════════

"""
KEY METRICS EXPLAINED:
  Completion Time (CT)  = when the process finishes on the CPU
  Turnaround Time (TAT) = CT - arrival_time
                        = total time from arrival to completion
  Waiting Time (WT)     = TAT - burst_time
                        = time spent waiting in the ready queue
  Response Time (RT)    = first_start - arrival_time
                        = how long until the process first touches the CPU

  CPU Utilisation (%)   = (busy_time / total_time) * 100
  Throughput            = n_processes / total_time  (processes per unit)
"""

def calculate_metrics(processes: List[Dict],
                       schedule: List[Tuple]) -> Tuple[List[Dict], Dict]:
    """
    Given the original process list and a schedule,
    compute per-process and aggregate metrics.

    Returns:
      per_process : list of dicts, one per process
      aggregate   : dict with avg_wt, avg_tat, avg_rt, utilisation, throughput
    """
    # Build lookup: pid → process dict
    proc_map = {p["pid"]: p for p in processes}

    # Per-process: find completion time and first start time
    completion  = {}   # pid → completion_time
    first_start = {}   # pid → first time on CPU

    for (pid, start, end) in schedule:
        completion[pid] = end
        if pid not in first_start:
            first_start[pid] = start

    per_process = []
    for p in processes:
        pid      = p["pid"]
        ct       = completion.get(pid, p["arrival_time"] + p["burst_time"])
        fs       = first_start.get(pid, p["arrival_time"])
        tat      = ct - p["arrival_time"]
        wt       = tat - p["burst_time"]
        rt       = fs - p["arrival_time"]
        per_process.append({
            "pid"             : pid,
            "name"            : p["name"],
            "arrival_time"    : p["arrival_time"],
            "burst_time"      : p["burst_time"],
            "priority"        : p["priority"],
            "completion_time" : ct,
            "turnaround_time" : tat,
            "waiting_time"    : wt,
            "response_time"   : rt,
        })

    n          = len(per_process)
    total_time = max(e for (_, _, e) in schedule) if schedule else 1
    start_time = min(s for (_, s, _) in schedule) if schedule else 0
    busy_time  = sum(e - s for (_, s, e) in schedule)

    # Deduplicate overlapping slices for utilisation
    # (in non-preemptive there are none, but in RR there can be gaps)
    busy_time = 0
    merged = []
    for (_, s, e) in sorted(schedule, key=lambda x: x[1]):
        if merged and s <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], e))
        else:
            merged.append((s, e))
    busy_time = sum(e - s for (s, e) in merged)

    span        = total_time - start_time
    utilisation = (busy_time / span * 100) if span > 0 else 100.0
    throughput  = n / total_time if total_time > 0 else 0.0

    aggregate = {
        "avg_wt"       : sum(r["waiting_time"]    for r in per_process) / n,
        "avg_tat"      : sum(r["turnaround_time"]  for r in per_process) / n,
        "avg_rt"       : sum(r["response_time"]    for r in per_process) / n,
        "utilisation"  : utilisation,
        "throughput"   : throughput,
    }

    return per_process, aggregate


def print_metrics_table(algo_name: str,
                         per_process: List[Dict],
                         aggregate:   Dict) -> None:
    """
    Print a formatted per-process table to the terminal.
    Uses Rich if available, otherwise falls back to tabulate,
    otherwise prints a plain text table.
    """
    headers = ["PID","Name","Arrival","Burst","Priority",
               "Completion","TAT","WT","RT"]
    rows = [[
        r["pid"], r["name"], r["arrival_time"], r["burst_time"],
        r["priority"], r["completion_time"],
        r["turnaround_time"], r["waiting_time"], r["response_time"]
    ] for r in per_process]

    print(f"\n{'='*60}")
    print(f"  {algo_name}")
    print(f"{'='*60}")

    if HAS_RICH:
        console = Console()
        table   = Table(title=algo_name, box=box.ROUNDED, show_lines=True)
        for h in headers:
            table.add_column(h, justify="right")
        for row in rows:
            table.add_row(*[str(x) for x in row])
        console.print(table)
    elif HAS_TABULATE:
        print(tabulate(rows, headers=headers, tablefmt="rounded_outline"))
    else:
        # Plain fallback
        print("  ".join(f"{h:>10}" for h in headers))
        print("-" * (12 * len(headers)))
        for row in rows:
            print("  ".join(f"{str(x):>10}" for x in row))

    print(f"\n  Avg Waiting Time    : {aggregate['avg_wt']:.2f}")
    print(f"  Avg Turnaround Time : {aggregate['avg_tat']:.2f}")
    print(f"  Avg Response Time   : {aggregate['avg_rt']:.2f}")
    print(f"  CPU Utilisation     : {aggregate['utilisation']:.1f}%")
    print(f"  Throughput          : {aggregate['throughput']:.4f} proc/unit")


# ══════════════════════════════════════════════════════════════
# SECTION 5 — GANTT CHART
# ══════════════════════════════════════════════════════════════

def draw_gantt(schedule:  List[Tuple],
               processes: List[Dict],
               algo_name: str,
               filename:  str,
               mode:      str = "process") -> None:
    """
    Draw a horizontal Gantt chart and save it as a PNG.

    Features:
      - Each process gets a unique colour
      - Idle CPU gaps shown in light grey
      - X-axis ticks at every time unit
      - Legend showing process names
      - Thread mode: labels show PID/thread group

    HOW GANTT CHARTS WORK:
    The X-axis is time. Each row is a "lane" (one per process or one total).
    A coloured bar from start to end shows when a process ran.
    Gaps between bars (grey) show when the CPU was idle.
    """
    pid_list = sorted(set(pid for (pid, _, _) in schedule))
    colours  = {pid: COLOURS[i % len(COLOURS)]
                for i, pid in enumerate(pid_list)}
    pid_map  = {p["pid"]: p for p in processes}

    fig, ax = plt.subplots(figsize=(max(12, len(schedule)*0.8), 4))

    max_time = max(e for (_, _, e) in schedule)
    min_time = min(s for (_, s, _) in schedule)

    # Draw idle gaps first (grey background bars)
    prev_end = min_time
    for (pid, start, end) in sorted(schedule, key=lambda x: x[1]):
        if start > prev_end:
            ax.barh(0, start - prev_end, left=prev_end,
                    height=0.5, color="#D3D3D3", edgecolor="white",
                    linewidth=0.5, label="_idle")
        prev_end = max(prev_end, end)

    # Draw process bars
    seen_pids = set()
    for (pid, start, end) in schedule:
        label = pid_map[pid]["name"] if pid in pid_map else f"P{pid}"
        if mode == "thread":
            # In thread mode, show which process group the thread belongs to
            label = f"{label}\n(PID {pid})"

        lbl = label if pid not in seen_pids else "_nolegend_"
        seen_pids.add(pid)

        ax.barh(0, end - start, left=start,
                height=0.5, color=colours[pid],
                edgecolor="white", linewidth=0.5,
                label=lbl)
        # Print PID inside bar if wide enough
        if (end - start) >= 1:
            ax.text((start + end) / 2, 0,
                    f"P{pid}", ha="center", va="center",
                    fontsize=8, fontweight="bold", color="white")

    # X-axis: ticks at every time unit
    ax.set_xlim(min_time, max_time)
    ax.set_xticks(range(min_time, max_time + 1))
    ax.set_xticklabels(range(min_time, max_time + 1), fontsize=7)
    ax.set_yticks([])
    ax.set_xlabel("Time (units)", fontsize=10)
    ax.set_title(f"Gantt Chart — {algo_name}", fontsize=12, fontweight="bold")

    # Legend
    handles, labels = ax.get_legend_handles_labels()
    filtered = [(h, l) for h, l in zip(handles, labels) if not l.startswith("_")]
    if filtered:
        hs, ls = zip(*filtered)
        # Add idle legend entry manually
        idle_patch = mpatches.Patch(color="#D3D3D3", label="Idle")
        ax.legend(list(hs) + [idle_patch], list(ls) + ["Idle"],
                  loc="upper right", fontsize=8, ncol=4)

    ax.grid(axis="x", linestyle="--", alpha=0.4)
    plt.tight_layout()
    path = os.path.join(SCREENSHOTS_DIR, filename)
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  [Saved] {path}")


# ══════════════════════════════════════════════════════════════
# SECTION 6 — COMPARISON BAR CHARTS
# ══════════════════════════════════════════════════════════════

def draw_comparison_charts(results: Dict[str, Dict]) -> None:
    """
    Side-by-side bar charts comparing all four algorithms on:
      - Average Waiting Time
      - Average Turnaround Time
      - CPU Utilisation

    results = { "FCFS": aggregate_dict, "SJF": aggregate_dict, ... }

    HOW TO READ THE CHART:
    Lower bars for WT and TAT = better algorithm for those metrics.
    Higher bar for Utilisation = better (CPU kept busier).
    """
    algos   = list(results.keys())
    metrics = {
        "Average Waiting Time"    : [results[a]["avg_wt"]      for a in algos],
        "Average Turnaround Time" : [results[a]["avg_tat"]      for a in algos],
        "CPU Utilisation (%)"     : [results[a]["utilisation"]  for a in algos],
    }

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    fig.suptitle("Algorithm Comparison", fontsize=14, fontweight="bold")

    bar_colours = COLOURS[:len(algos)]

    for ax, (metric_name, values) in zip(axes, metrics.items()):
        bars = ax.bar(algos, values, color=bar_colours, edgecolor="white",
                      linewidth=0.8, width=0.5)
        ax.set_title(metric_name, fontsize=11, fontweight="bold")
        ax.set_ylabel("Value")
        ax.set_ylim(0, max(values) * 1.25 if max(values) > 0 else 1)
        # Value labels on top of bars
        for bar, val in zip(bars, values):
            ax.text(bar.get_x() + bar.get_width() / 2,
                    bar.get_height() + max(values) * 0.02,
                    f"{val:.2f}", ha="center", va="bottom", fontsize=9)
        ax.grid(axis="y", linestyle="--", alpha=0.4)
        ax.set_xticks(range(len(algos)))
        ax.set_xticklabels(algos, fontsize=9)

    plt.tight_layout()
    path = os.path.join(SCREENSHOTS_DIR, "comparison_charts.png")
    plt.savefig(path, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"  [Saved] {path}")


# ══════════════════════════════════════════════════════════════
# SECTION 7 — COMPARISON TABLE
# ══════════════════════════════════════════════════════════════

def print_comparison_table(results: Dict[str, Tuple]) -> None:
    """
    Print a side-by-side comparison of all algorithms in one table.

    results = {
        "FCFS" : (per_process_list, aggregate_dict),
        "SJF"  : (per_process_list, aggregate_dict),
        ...
    }
    """
    print("\n" + "═"*70)
    print("  ALGORITHM COMPARISON TABLE")
    print("═"*70)

    headers = ["Metric"] + list(results.keys())
    rows = []
    metric_keys = [
        ("avg_wt",      "Avg Waiting Time"),
        ("avg_tat",     "Avg Turnaround Time"),
        ("avg_rt",      "Avg Response Time"),
        ("utilisation", "CPU Utilisation (%)"),
        ("throughput",  "Throughput (proc/unit)"),
    ]
    for key, label in metric_keys:
        row = [label]
        for algo in results:
            val = results[algo][1][key]
            row.append(f"{val:.2f}")
        rows.append(row)

    if HAS_RICH:
        console = Console()
        table   = Table(box=box.DOUBLE_EDGE, show_lines=True,
                        title="Algorithm Comparison")
        for h in headers:
            table.add_column(h, justify="center", style="bold" if h=="Metric" else "")
        for row in rows:
            table.add_row(*row)
        console.print(table)
    elif HAS_TABULATE:
        print(tabulate(rows, headers=headers, tablefmt="rounded_outline"))
    else:
        print("  ".join(f"{h:>22}" for h in headers))
        print("-" * (24 * len(headers)))
        for row in rows:
            print("  ".join(f"{str(x):>22}" for x in row))

    print()


# ══════════════════════════════════════════════════════════════
# SECTION 8 — THREAD MODE
# ══════════════════════════════════════════════════════════════

"""
THREAD MODE EXPLANATION:
In --mode thread, each "process" in the input actually represents a THREAD
belonging to a parent process. Multiple threads can share the same parent PID.

Thread group affinity:
  Threads from the same process (same parent_pid) are kept together on the
  Gantt chart with a shared colour shade so you can see which threads belong
  to the same process.

Context-switch cost:
  Every time the scheduler switches from one process group to another,
  CONTEXT_SWITCH_COST time units are added to simulate the OS saving and
  restoring the process context (registers, stack pointer, etc.).
  This makes the total execution time longer when many context switches happen.
"""

def apply_thread_mode(schedule: List[Tuple],
                       processes: List[Dict]) -> Tuple[List[Tuple], List[Dict]]:
    """
    Insert context-switch overhead whenever the running process GROUP changes.
    Returns a new schedule with extra "IDLE" (pid=0) slices for context switches.
    """
    if not schedule:
        return schedule, processes

    proc_map  = {p["pid"]: p for p in processes}
    new_sched = []
    prev_pid  = None
    offset    = 0   # accumulated time added by context switches

    for (pid, start, end) in schedule:
        adjusted_start = start + offset
        adjusted_end   = end   + offset

        if prev_pid is not None and prev_pid != pid:
            # Context switch: insert CONTEXT_SWITCH_COST idle time
            cs_start = adjusted_start
            cs_end   = cs_start + CONTEXT_SWITCH_COST
            new_sched.append([0, cs_start, cs_end])   # pid=0 = idle/switch
            offset         += CONTEXT_SWITCH_COST
            adjusted_start  = cs_end
            adjusted_end    = adjusted_start + (end - start)

        new_sched.append([pid, adjusted_start, adjusted_end])
        prev_pid = pid

    # Add a fake "context switch" process for the legend
    cs_proc = {
        "pid": 0, "name": "CTX-SW",
        "arrival_time": 0, "burst_time": 0,
        "priority": 0, "memory_kb": 0
    }
    return new_sched, processes + [cs_proc]


# ══════════════════════════════════════════════════════════════
# SECTION 9 — GANTT ANIMATION (BONUS)
# ══════════════════════════════════════════════════════════════

def draw_gantt_animation(schedule:  List[Tuple],
                          processes: List[Dict],
                          algo_name: str,
                          filename:  str) -> None:
    """
    BONUS: Animated Gantt chart that reveals one time unit at a time.

    Uses matplotlib.animation.FuncUpdate.
    Each frame shows the schedule up to time = frame number.
    Saved as a GIF file.
    """
    pid_list = sorted(set(pid for (pid, _, _) in schedule))
    colours  = {pid: COLOURS[i % len(COLOURS)] for i, pid in enumerate(pid_list)}
    pid_map  = {p["pid"]: p for p in processes}
    max_time = max(e for (_, _, e) in schedule)

    fig, ax = plt.subplots(figsize=(max(12, max_time * 0.6), 4))

    def update(frame):
        ax.clear()
        ax.set_xlim(0, max_time)
        ax.set_xticks(range(0, max_time + 1))
        ax.set_yticks([])
        ax.set_xlabel("Time (units)")
        ax.set_title(f"Gantt Animation — {algo_name}  (t={frame})",
                     fontsize=11, fontweight="bold")
        ax.grid(axis="x", linestyle="--", alpha=0.3)

        prev_end = 0
        for (pid, start, end) in sorted(schedule, key=lambda x: x[1]):
            if start >= frame:
                break
            visible_end = min(end, frame)
            # Idle gap
            if start > prev_end:
                ax.barh(0, min(start, frame) - prev_end,
                        left=prev_end, height=0.5,
                        color="#D3D3D3", edgecolor="white")
            ax.barh(0, visible_end - start,
                    left=start, height=0.5,
                    color=colours.get(pid, "#999"),
                    edgecolor="white", linewidth=0.5)
            prev_end = visible_end

    ani = animation.FuncAnimation(
        fig, update, frames=range(0, max_time + 2),
        interval=400, repeat=False)

    path = os.path.join(SCREENSHOTS_DIR, filename)
    try:
        ani.save(path, writer="pillow", fps=3)
        print(f"  [Saved animation] {path}")
    except Exception as e:
        print(f"  [Animation skipped — pillow not installed: {e}]")
    plt.close()


# ══════════════════════════════════════════════════════════════
# SECTION 10 — CLI ENTRY POINT
# ══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="EduOS Scheduling Simulator — Part 3",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python scheduler_sim.py --random 8
  python scheduler_sim.py --random 8 --seed 42
  python scheduler_sim.py --file sample_processes.csv
  python scheduler_sim.py --file ../pcb_snapshot.json
  python scheduler_sim.py --random 6 --quantum 2
  python scheduler_sim.py --random 6 --quantum 4
  python scheduler_sim.py --random 8 --mode thread
        """
    )

    # Input source (mutually exclusive)
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--random", type=int, metavar="N",
                     help="Generate N random processes")
    src.add_argument("--file",   type=str, metavar="PATH",
                     help="Load processes from CSV or JSON file")

    # Options
    parser.add_argument("--seed",    type=int,   default=None,
                        help="Random seed for reproducibility")
    parser.add_argument("--quantum", type=int,   default=2,
                        help="Round Robin time quantum (default 2)")
    parser.add_argument("--mode",    type=str,   default="process",
                        choices=["process","thread"],
                        help="Scheduling mode: process or thread")
    parser.add_argument("--animate", action="store_true",
                        help="(Bonus) Save animated Gantt GIFs")

    args = parser.parse_args()

    # ── Load processes ──────────────────────────────────────
    print("\n" + "═"*60)
    print("  EduOS Scheduling Simulator")
    print("═"*60)

    if args.random:
        processes = generate_random_processes(args.random, seed=args.seed)
        print(f"\n  Generated {args.random} random processes"
              + (f" (seed={args.seed})" if args.seed else ""))
    else:
        path = args.file
        if path.endswith(".json"):
            processes = load_from_json(path)
            print(f"\n  Loaded {len(processes)} processes from JSON: {path}")
        else:
            processes = load_from_csv(path)
            print(f"\n  Loaded {len(processes)} processes from CSV: {path}")

    if not processes:
        print("  ERROR: No processes loaded. Exiting.")
        sys.exit(1)

    # Print what we loaded
    print(f"\n  {'PID':>4}  {'Name':<12}  {'Arrival':>7}  "
          f"{'Burst':>5}  {'Priority':>8}  {'Mem(KB)':>7}")
    print("  " + "-"*50)
    for p in processes:
        print(f"  {p['pid']:>4}  {p['name']:<12}  {p['arrival_time']:>7}  "
              f"{p['burst_time']:>5}  {p['priority']:>8}  {p['memory_kb']:>7}")

    mode = args.mode
    print(f"\n  Mode    : {mode}")
    print(f"  Quantum : {args.quantum} (used for Round Robin)")
    print(f"  Charts  → {SCREENSHOTS_DIR}\n")

    # ── Run all four algorithms ─────────────────────────────
    algo_results  = {}   # name → (per_process, aggregate)
    algo_schedules = {}  # name → schedule

    algos = [
        ("FCFS",                    lambda p: fcfs(p)),
        ("SJF",                     lambda p: sjf(p)),
        ("Priority (with Ageing)",  lambda p: priority_scheduling(p)),
        (f"Round Robin (q={args.quantum})", lambda p: round_robin(p, args.quantum)),
    ]

    # Also run RR with a second quantum to show comparison
    second_q = 4 if args.quantum != 4 else 2
    algos.append(
        (f"Round Robin (q={second_q})", lambda p, q=second_q: round_robin(p, q))
    )

    for algo_name, algo_fn in algos:
        print(f"\n{'─'*60}")
        print(f"  Running: {algo_name}")
        print(f"{'─'*60}")

        procs_copy = copy.deepcopy(processes)
        schedule   = algo_fn(procs_copy)

        if mode == "thread":
            schedule, procs_copy = apply_thread_mode(schedule, procs_copy)

        per_proc, agg = calculate_metrics(procs_copy, schedule)
        print_metrics_table(algo_name, per_proc, agg)

        # Only include the 4 main algorithms in comparison
        short_name = algo_name.split(" (")[0]
        if short_name not in algo_results or "q=" in algo_name:
            algo_results[algo_name]   = (per_proc, agg)
            algo_schedules[algo_name] = schedule

        # Gantt chart
        safe_name = algo_name.lower().replace(" ", "_").replace("(","").replace(")","").replace("=","")
        gantt_file = f"gantt_{safe_name}.png"
        draw_gantt(schedule, procs_copy, algo_name, gantt_file, mode=mode)

        # Bonus animation
        if args.animate:
            anim_file = f"anim_{safe_name}.gif"
            draw_gantt_animation(schedule, procs_copy, algo_name, anim_file)

    # ── Comparison charts and table ─────────────────────────
    print(f"\n{'─'*60}")
    print("  Generating comparison charts and table...")
    print(f"{'─'*60}")

    # Use only the 4 main algorithms for comparison
    main_algos = {
        "FCFS"     : algo_results.get("FCFS"),
        "SJF"      : algo_results.get("SJF"),
        "Priority" : algo_results.get("Priority (with Ageing)"),
        f"RR q={args.quantum}" : algo_results.get(
            f"Round Robin (q={args.quantum})"),
    }
    # Remove None entries
    main_algos = {k: v for k, v in main_algos.items() if v is not None}

    draw_comparison_charts({k: v[1] for k, v in main_algos.items()})
    print_comparison_table(main_algos)

    print("\n" + "═"*60)
    print("  Simulation complete!")
    print(f"  All charts saved to: {SCREENSHOTS_DIR}")
    print("═"*60 + "\n")


if __name__ == "__main__":
    main()
