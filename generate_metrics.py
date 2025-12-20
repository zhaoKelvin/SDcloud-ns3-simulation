import os
import json
import glob
import argparse
import xml.etree.ElementTree as ET
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
from itertools import combinations

INITIAL_ENERGY_J = 300.0
SIMULATION_TIME = 1

def convert_time_to_seconds(val):
    val = val.strip()
    if val.endswith("ns"): return float(val[:-2]) * 1e-9
    if val.endswith("us"): return float(val[:-2]) * 1e-6
    if val.endswith("ms"): return float(val[:-2]) * 1e-3
    if val.endswith("s"):  return float(val[:-1])
    return float(val)

def parse_flowmon_xml(filename):
    tree = ET.parse(filename)
    root = tree.getroot()

    classifier = {}
    for flow in root.findall("FlowClassifier/Flow"):
        fid = int(flow.attrib["flowId"])
        classifier[fid] = {
            "src": flow.attrib.get("sourceAddress", "unknown"),
            "dst": flow.attrib.get("destinationAddress", "unknown"),
        }

    stats = {}
    for flow in root.findall("FlowStats/Flow"):
        fid = int(flow.attrib["flowId"])

        tx = int(flow.attrib["txPackets"])
        rx = int(flow.attrib["rxPackets"])
        lost = int(flow.attrib["lostPackets"])

        delay_sum = convert_time_to_seconds(flow.attrib["delaySum"])
        jitter_sum = convert_time_to_seconds(flow.attrib["jitterSum"])

        tx_bytes = int(flow.attrib["txBytes"])
        rx_bytes = int(flow.attrib["rxBytes"])

        t0 = convert_time_to_seconds(flow.attrib["timeFirstTxPacket"])
        t1 = convert_time_to_seconds(flow.attrib["timeLastRxPacket"])
        dur = max(1e-12, t1 - t0)

        stats[fid] = {
            "tx_packets": tx,
            "rx_packets": rx,
            "loss_ratio": lost / max(1, tx),
            "avg_delay": delay_sum / rx if rx > 0 else 0.0,
            "avg_jitter": jitter_sum / (rx - 1) if rx > 1 else 0.0,
            "throughput_bps": (rx_bytes * 8),
            "rx_bytes": rx_bytes,
        }

    return stats

def parse_metrics_json(filename="metrics.json", duration=None):
    # If duration not provided, try metadata in same folder
    if duration is None:
        meta_path = os.path.join(os.path.dirname(filename), "metadata.json")
        if os.path.exists(meta_path):
            try:
                meta = json.load(open(meta_path))
                duration = meta.get("simTimeSec", SIMULATION_TIME)
            except Exception:
                duration = SIMULATION_TIME
        else:
            duration = SIMULATION_TIME
    with open(filename, "r") as f:
        data = json.load(f)

    bytes_per_packet = data.get("bytesPerPacket", 0)

    node_tx = {int(k): int(v) for k, v in data["nodePacketsSent"].items()}
    node_rx = {int(k): int(v) for k, v in data["nodePacketsReceived"].items()}
    node_latency = {int(k): float(v) for k, v in data.get("nodeAverageLatency", {}).items()}

    stats = {}

    for node_id in node_tx:
        tx = node_tx.get(node_id, 0)
        rx = node_rx.get(node_id, 0)
        loss_ratio = (tx - rx) / max(1, tx)

        # Latency
        avg_delay = node_latency.get(node_id, 0.0)

        # No per-packet jitter available → 0
        avg_jitter = 0.0

        # Throughput: only counts successfully received bytes
        rx_bytes = rx * bytes_per_packet

        # You may adjust duration if needed — here assume entire sim or unknown
        # Use 1 second to avoid divide-by-zero and remain consistent
        duration = max(duration, 1e-12)
        throughput_bps = (rx_bytes * 8) / duration

        stats[node_id] = {
            "tx_packets": tx,
            "rx_packets": rx,
            "loss_ratio": loss_ratio,
            "avg_delay": avg_delay,
            "avg_jitter": avg_jitter,
            "throughput_bps": throughput_bps,
            "rx_bytes": rx_bytes,
        }

    return stats


def load_energy_csv(path):
    data = np.genfromtxt(path, delimiter=",", skip_header=1)
    # last value per node is remaining energy
    remaining = {}
    for t, node, rem in data:
        remaining[int(node)] = rem
    return remaining

# ============================================================
# Compute single-run metrics
# ============================================================
def compute_bits_per_joule(stats, final_energy):
    bpj = {}

    for node, remaining_j in final_energy.items():
        energy_used = max(1e-12, INITIAL_ENERGY_J - remaining_j)

        # Node i → Flow ID = i+1
        flow_id = node + 1

        if flow_id in stats:
            delivered_bits = stats[flow_id]["rx_bytes"] * 8
        else:
            delivered_bits = 0

        bpj[node] = delivered_bits / energy_used

    return bpj

def compute_single_run(flow_stats, energy_remaining, duration):
    rx_bits = sum(fs["throughput_bps"] * 1e6 / 8 for fs in flow_stats.values())
    bits_delivered = sum(fs["throughput_bps"] * 1e6 * 1 for fs in flow_stats.values())  # per sec approx

    avg_pdr = np.mean([fs["rx_packets"] / max(1, fs["tx_packets"]) for fs in flow_stats.values()])
    avg_thr = np.mean([fs["throughput_bps"] for fs in flow_stats.values()])
    avg_jitter = np.mean([fs["avg_jitter"] for fs in flow_stats.values()])
    avg_delay = np.mean([fs["avg_delay"] for fs in flow_stats.values()])

    # Total initial energy = 300 J for each node
    duration = max(duration, 1e-12)
    bpj = compute_bits_per_joule(flow_stats, energy_remaining)
    
    average_aggregate_bpj = sum(bpj.values()) / len(bpj)
    
    valid_bpj = [bpj for node, bpj in bpj.items() if bpj > 0]
    average_valid_bpj = sum(valid_bpj) / max(len(valid_bpj), 1)

    return {
        "avg_pdr": avg_pdr,
        "avg_throughput": avg_thr,
        "avg_delay": avg_delay,
        "avg_jitter": avg_jitter,
        "average_aggregate_bpj": average_aggregate_bpj,
        "average_valid_bpj": average_valid_bpj,
    }

# ============================================================
# Plot single-run metrics (your original plots)
# ============================================================
def plot_single_run(stats, final_energy, duration=SIMULATION_TIME):
    metrics = [
        ("throughput_bps", "Throughput (bps)"),
        ("avg_delay", "Average Delay (s)"),
        ("avg_jitter", "Average Jitter (s)"),
        ("loss_ratio", "Loss Ratio")
    ]
    flow_ids = sorted(stats.keys())

    fig, axs = plt.subplots(3, 2, figsize=(10, 8))
    axs = axs.flatten()

    # ---- FlowMonitor metrics ----
    for i, (metric, ylabel) in enumerate(metrics):
        values = [stats[f][metric] for f in flow_ids]
        axs[i].bar(flow_ids, values)
        axs[i].set_title(f"{ylabel} per Flow")
        axs[i].set_xlabel("Flow ID")
        axs[i].set_ylabel(ylabel)
        axs[i].grid(axis="y", linestyle="--", alpha=0.5)

    # ---- Energy: Consumed ----
    node_ids = sorted(final_energy.keys())
    consumed = [INITIAL_ENERGY_J - final_energy[n] for n in node_ids]

    axs[4].bar(node_ids, consumed, color="red")
    axs[4].set_title("Energy Consumed per Node (Joules)")
    axs[4].set_xlabel("Node ID")
    axs[4].set_ylabel("Consumed J")
    axs[4].grid(axis="y", linestyle="--", alpha=0.5)
    
    # ---- Bits-per-Joule efficiency ----
    bpj = compute_bits_per_joule(stats, final_energy)

    node_ids = sorted(bpj.keys())
    bpj_values = [bpj[n] for n in node_ids]

    axs[5].bar(node_ids, bpj_values, color="purple")
    axs[5].set_title("Bits-per-Joule Efficiency per Node")
    axs[5].set_xlabel("Node ID")
    axs[5].set_ylabel("Bits per Joule")
    axs[5].grid(axis="y", linestyle="--", alpha=0.5)

    plt.tight_layout()
    plt.show()

# ============================================================
# Batch loading & aggregation
# ============================================================
def load_experiment_runs(folder, lora=False):
    runs = glob.glob(os.path.join(folder, "run_*"))
    out = []
    for r in runs:
        metrics_json = os.path.join(r, "metrics.json")
        flowmon_xml = os.path.join(r, "flowmon.xml")
        energy = os.path.join(r, "energy.csv")
        meta = os.path.join(r, "metadata.json")

        # Require metadata + energy always
        if not (os.path.exists(energy) and os.path.exists(meta)):
            continue

        meta_obj = json.load(open(meta))
        duration = meta_obj.get("simTimeSec", SIMULATION_TIME)
        # print(duration)

        # Decide which metrics file to use
        stats = None
        if lora or (os.path.exists(metrics_json) and not os.path.exists(flowmon_xml)):
            if os.path.exists(metrics_json):
                stats = parse_metrics_json(metrics_json, duration)
        if stats is None and os.path.exists(flowmon_xml):
            stats = parse_flowmon_xml(flowmon_xml)

        if stats is None:
            continue

        energy_remaining = load_energy_csv(energy)
        out.append((stats, energy_remaining, meta_obj))
    return out

# ============================================================
# Batch visualization (Distance vs Connectivity)
# ============================================================
def plot_batch(runs):
    # tech_maps[technology][environment]["pdr"|"bpj"][distance] = [values]
    tech_maps = {}
    # interval_maps[technology][environment][interval] = [pdr values]
    interval_pdr = {}
    interval_bpj = {}
    interval_energy_per_msg = {}
    interval_throughput = {}
    interval_total_energy = {}
    interval_avg_power = {}
    payload_pdr = {}
    payload_bpj = {}
    simtime_bpj = {}

    for flow_stats, energy_remaining, meta in runs:
        env = meta.get("environment", "field")
        tech = meta.get("technology", "unknown")
        dist = meta["distance"]
        interval_val = meta.get("intervalSec", meta.get("interval", None))
        payload_val = meta.get("payloadBytes", None)

        duration = meta.get("simTimeSec", SIMULATION_TIME)
        metrics = compute_single_run(flow_stats, energy_remaining, duration)

        if tech not in tech_maps:
            tech_maps[tech] = {}
            interval_pdr[tech] = {}
            interval_bpj[tech] = {}
            interval_energy_per_msg[tech] = {}
            interval_throughput[tech] = {}
            interval_total_energy[tech] = {}
            interval_avg_power[tech] = {}
            payload_pdr[tech] = {}
            payload_bpj[tech] = {}
            simtime_bpj[tech] = {}
        if env not in tech_maps[tech]:
            tech_maps[tech][env] = {"pdr": {}, "bpj": {}}
        if env not in interval_pdr[tech]:
            interval_pdr[tech][env] = {}
            interval_bpj[tech][env] = {}
            interval_energy_per_msg[tech][env] = {}
            interval_throughput[tech][env] = {}
            interval_total_energy[tech][env] = {}
            interval_avg_power[tech][env] = {}
            payload_pdr[tech][env] = {}
            payload_bpj[tech][env] = {}
            simtime_bpj[tech][env] = {}
        if dist not in tech_maps[tech][env]["pdr"]:
            tech_maps[tech][env]["pdr"][dist] = []
            tech_maps[tech][env]["bpj"][dist] = []

        tech_maps[tech][env]["pdr"][dist].append(metrics["avg_pdr"])
        tech_maps[tech][env]["bpj"][dist].append(metrics["average_aggregate_bpj"])
        simtime_bpj[tech][env].setdefault(duration, []).append(metrics["average_aggregate_bpj"])

        total_energy_used = sum(INITIAL_ENERGY_J - rem for rem in energy_remaining.values())
        total_tx_packets = sum(fs["tx_packets"] for fs in flow_stats.values())
        energy_per_msg = total_energy_used / max(total_tx_packets, 1)
        avg_power = total_energy_used / max(duration, 1e-12)

        if interval_val is not None:
            if interval_val not in interval_pdr[tech][env]:
                interval_pdr[tech][env][interval_val] = []
                interval_bpj[tech][env][interval_val] = []
                interval_energy_per_msg[tech][env][interval_val] = []
                interval_throughput[tech][env][interval_val] = []
                interval_total_energy[tech][env][interval_val] = []
                interval_avg_power[tech][env][interval_val] = []
            interval_pdr[tech][env][interval_val].append(metrics["avg_pdr"])
            interval_bpj[tech][env][interval_val].append(metrics["average_aggregate_bpj"])
            interval_energy_per_msg[tech][env][interval_val].append(energy_per_msg)
            interval_throughput[tech][env][interval_val].append(metrics["avg_throughput"])
            interval_total_energy[tech][env][interval_val].append(total_energy_used)
            interval_avg_power[tech][env][interval_val].append(avg_power)
        if payload_val is not None:
            if payload_val not in payload_pdr[tech][env]:
                payload_pdr[tech][env][payload_val] = []
                payload_bpj[tech][env][payload_val] = []
            payload_pdr[tech][env][payload_val].append(metrics["avg_pdr"])
            payload_bpj[tech][env][payload_val].append(metrics["average_aggregate_bpj"])

    # Single set of figures with all technologies/environments
    fig, axs = plt.subplots(2, 1, figsize=(10, 8))
    axs = axs.flatten()

    # deterministic color assignment
    series = []
    for tech, env_maps in tech_maps.items():
        for env, metrics in env_maps.items():
            series.append((tech, env, metrics))

    # If no data, bail early
    if not series:
        print("No runs to plot.")
        return

    color_map = {(tech, env): plt.cm.tab20(i % 20) for i, (tech, env, _) in enumerate(series)}

    def print_pdr_distance_comparisons():
        print("=== PDR vs Distance: Percentage Differences by Environment ===")
        # Gather env -> list of techs
        env_to_techs = {}
        for tech, env, _ in series:
            env_to_techs.setdefault(env, []).append(tech)
        for env, tech_list in env_to_techs.items():
            unique_techs = sorted(set(tech_list))
            if len(unique_techs) < 2:
                continue
            # Intersection of distances across techs for this env
            common_dists = None
            per_tech_medians = {}
            for tech in unique_techs:
                dvals = tech_maps.get(tech, {}).get(env, {}).get("pdr", {})
                medians = {d: np.median(vals) for d, vals in dvals.items() if len(vals) > 0}
                per_tech_medians[tech] = medians
                if common_dists is None:
                    common_dists = set(medians.keys())
                else:
                    common_dists &= set(medians.keys())
            if not common_dists:
                continue
            print(f"-- Environment: {env} (common distances: {sorted(common_dists)})")
            for dist in sorted(common_dists):
                row = []
                for t1, t2 in combinations(unique_techs, 2):
                    base = per_tech_medians[t2].get(dist, 0.0)
                    num = per_tech_medians[t1].get(dist, 0.0)
                    if base > 0:
                        pct = 100.0 * (num - base) / base
                        row.append(f"{t1} vs {t2} @ {dist} m: {pct:+.1f}%")
                if row:
                    print("   " + " | ".join(row))
        print("==============================================================")

    def print_bpj_distance_comparisons():
        print("=== Bits-per-Joule vs Distance: Percentage Differences by Environment ===")
        env_to_techs = {}
        for tech, env, _ in series:
            env_to_techs.setdefault(env, []).append(tech)
        for env, tech_list in env_to_techs.items():
            unique_techs = sorted(set(tech_list))
            if len(unique_techs) < 2:
                continue
            common_dists = None
            per_tech_medians = {}
            for tech in unique_techs:
                dvals = tech_maps.get(tech, {}).get(env, {}).get("bpj", {})
                medians = {d: np.median(vals) for d, vals in dvals.items() if len(vals) > 0}
                per_tech_medians[tech] = medians
                if common_dists is None:
                    common_dists = set(medians.keys())
                else:
                    common_dists &= set(medians.keys())
            if not common_dists:
                continue
            print(f"-- Environment: {env} (common distances: {sorted(common_dists)})")
            for dist in sorted(common_dists):
                row = []
                for t1, t2 in combinations(unique_techs, 2):
                    base = per_tech_medians[t2].get(dist, 0.0)
                    num = per_tech_medians[t1].get(dist, 0.0)
                    if base > 0:
                        pct = 100.0 * (num - base) / base
                        row.append(f"{t1} vs {t2} @ {dist} m: {pct:+.1f}%")
                if row:
                    print("   " + " | ".join(row))
        print("==============================================================")

    def print_pdr_interval_comparisons():
        print("=== PDR vs Interval: Percentage Differences by Environment ===")
        env_to_techs = {}
        for tech, env, _ in series:
            env_to_techs.setdefault(env, []).append(tech)
        for env, tech_list in env_to_techs.items():
            unique_techs = sorted(set(tech_list))
            if len(unique_techs) < 2:
                continue
            common_intervals = None
            per_tech_medians = {}
            for tech in unique_techs:
                ivals = interval_pdr.get(tech, {}).get(env, {})
                medians = {i: np.median(vals) for i, vals in ivals.items() if len(vals) > 0}
                per_tech_medians[tech] = medians
                if common_intervals is None:
                    common_intervals = set(medians.keys())
                else:
                    common_intervals &= set(medians.keys())
            if not common_intervals:
                continue
            print(f"-- Environment: {env} (common intervals: {sorted(common_intervals)})")
            for iv in sorted(common_intervals):
                row = []
                for t1, t2 in combinations(unique_techs, 2):
                    base = per_tech_medians[t2].get(iv, 0.0)
                    num = per_tech_medians[t1].get(iv, 0.0)
                    if base > 0:
                        pct = 100.0 * (num - base) / base
                        row.append(f"{t1} vs {t2} @ {iv}s: {pct:+.1f}%")
                if row:
                    print("   " + " | ".join(row))
        print("==============================================================")

    def print_bpj_interval_comparisons():
        print("=== Bits-per-Joule vs Interval: Percentage Differences by Environment ===")
        env_to_techs = {}
        for tech, env, _ in series:
            env_to_techs.setdefault(env, []).append(tech)
        for env, tech_list in env_to_techs.items():
            unique_techs = sorted(set(tech_list))
            if len(unique_techs) < 2:
                continue
            common_intervals = None
            per_tech_medians = {}
            for tech in unique_techs:
                ivals = interval_bpj.get(tech, {}).get(env, {})
                medians = {i: np.median(vals) for i, vals in ivals.items() if len(vals) > 0}
                per_tech_medians[tech] = medians
                if common_intervals is None:
                    common_intervals = set(medians.keys())
                else:
                    common_intervals &= set(medians.keys())
            if not common_intervals:
                continue
            print(f"-- Environment: {env} (common intervals: {sorted(common_intervals)})")
            for iv in sorted(common_intervals):
                row = []
                for t1, t2 in combinations(unique_techs, 2):
                    base = per_tech_medians[t2].get(iv, 0.0)
                    num = per_tech_medians[t1].get(iv, 0.0)
                    if base > 0:
                        pct = 100.0 * (num - base) / base
                        row.append(f"{t1} vs {t2} @ {iv}s: {pct:+.1f}%")
                if row:
                    print("   " + " | ".join(row))
        print("==============================================================")

    def compare_envs_distance(metric_key, title):
        print(f"=== {title}: Field vs Forest (and other env pairs) per Tech ===")
        tech_to_envs = {}
        for tech, env, _ in series:
            tech_to_envs.setdefault(tech, set()).add(env)
        for tech, envs in tech_to_envs.items():
            env_list = sorted(envs)
            if len(env_list) < 2:
                continue
            for e1, e2 in combinations(env_list, 2):
                dvals1 = tech_maps.get(tech, {}).get(e1, {}).get(metric_key, {})
                dvals2 = tech_maps.get(tech, {}).get(e2, {}).get(metric_key, {})
                med1 = {d: np.median(v) for d, v in dvals1.items() if v}
                med2 = {d: np.median(v) for d, v in dvals2.items() if v}
                common = set(med1.keys()) & set(med2.keys())
                if not common:
                    continue
                rows = []
                for dist in sorted(common):
                    base = med2.get(dist, 0.0)
                    num = med1.get(dist, 0.0)
                    if base > 0:
                        pct = 100.0 * (num - base) / base
                        rows.append(f"{e1} vs {e2} @ {dist} m: {pct:+.1f}%")
                if rows:
                    print(f"-- Tech: {tech} | " + " | ".join(rows))
        print("==============================================================")

    def compare_envs_interval(metric_map, title):
        print(f"=== {title}: Field vs Forest (and other env pairs) per Tech ===")
        tech_to_envs = {}
        for tech, env, _ in series:
            tech_to_envs.setdefault(tech, set()).add(env)
        for tech, envs in tech_to_envs.items():
            env_list = sorted(envs)
            if len(env_list) < 2:
                continue
            for e1, e2 in combinations(env_list, 2):
                ivals1 = metric_map.get(tech, {}).get(e1, {})
                ivals2 = metric_map.get(tech, {}).get(e2, {})
                med1 = {i: np.median(v) for i, v in ivals1.items() if v}
                med2 = {i: np.median(v) for i, v in ivals2.items() if v}
                common = set(med1.keys()) & set(med2.keys())
                if not common:
                    continue
                rows = []
                for iv in sorted(common):
                    base = med2.get(iv, 0.0)
                    num = med1.get(iv, 0.0)
                    if base > 0:
                        pct = 100.0 * (num - base) / base
                        rows.append(f"{e1} vs {e2} @ {iv}s: {pct:+.1f}%")
                if rows:
                    print(f"-- Tech: {tech} | " + " | ".join(rows))
        print("==============================================================")

    # Print comparison tables
    # print_pdr_distance_comparisons()
    # print_bpj_distance_comparisons()
    # print_pdr_interval_comparisons()
    # print_bpj_interval_comparisons()
    # compare_envs_distance("pdr", "PDR vs Distance")
    # compare_envs_distance("bpj", "Bits-per-Joule vs Distance")
    # compare_envs_interval(interval_pdr, "PDR vs Interval")
    # compare_envs_interval(interval_bpj, "Bits-per-Joule vs Interval")

    def make_boxplot(ax, metric_key, title, ylabel, logy=False):
        for idx, (tech, env, metrics) in enumerate(series):
            color = color_map[(tech, env)]
            dvals = metrics[metric_key]
            distances = sorted(dvals.keys())
            dist_positions = distances

            if not distances:
                continue

            medians = [np.median(dvals[d]) for d in distances]
            ax.plot(
                dist_positions,
                medians,
                color=color,
                marker="o",
                linewidth=2.0,
                label=f"{tech}-{env}"
            )

        ax.set_xlabel("Distance (m)")
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.grid(True, linestyle="--", alpha=0.5)
        ax.set_xscale("log")
        if logy:
            ax.set_yscale("symlog")
            ax.set_ylim(0, 1e4)
        formatter = ScalarFormatter()
        formatter.set_scientific(False)
        ax.xaxis.set_major_formatter(formatter)
        ax.legend()

    make_boxplot(
        axs[0],
        metric_key="pdr",
        title="Packet Delivery Ratio (PDR) vs Distance",
        ylabel="PDR"
    )

    make_boxplot(
        axs[1],
        metric_key="bpj",
        title="Bits-per-Joule vs Distance",
        ylabel="Bits per Joule",
        logy=True
    )

    plt.tight_layout()
    plt.show()

    def plot_interval_metric(title, ylabel, data_map):
        fig_int, ax_int = plt.subplots(1, 1, figsize=(8, 4))
        for idx, (tech, env, _) in enumerate(series):
            color = color_map[(tech, env)]
            intervals = data_map.get(tech, {}).get(env, {})
            if not intervals:
                continue
            xs = sorted(intervals.keys())
            medians = [np.median(intervals[x]) for x in xs]
            ax_int.plot(xs, medians, color=color, marker="o", linewidth=2.0, label=f"{tech}-{env}")

        ax_int.set_xlabel("Message Interval (s)")
        ax_int.set_ylabel(ylabel)
        ax_int.set_title(title)
        ax_int.grid(True, linestyle="--", alpha=0.5)
        ax_int.set_xscale("log")
        formatter = ScalarFormatter()
        formatter.set_scientific(False)
        ax_int.xaxis.set_major_formatter(formatter)
        ax_int.legend()
        plt.tight_layout()
        plt.show()

    # Interval-based figures
    plot_interval_metric("PDR vs Message Interval", "PDR", interval_pdr)
    plot_interval_metric("Bits-per-Joule vs Message Interval", "Bits per Joule", interval_bpj)
    plot_interval_metric("Energy per Message vs Message Interval", "Energy per Message (J)", interval_energy_per_msg)
    plot_interval_metric("Throughput vs Message Interval", "Throughput (bps)", interval_throughput)
    plot_interval_metric("Total Energy Consumed vs Message Interval", "Total Energy Consumed (J)", interval_total_energy)
    plot_interval_metric("Average Power vs Message Interval", "Average Power (W)", interval_avg_power)

    def plot_payload_metric(title, ylabel, data_map):
        fig_pl, ax_pl = plt.subplots(1, 1, figsize=(8, 4))
        for idx, (tech, env, _) in enumerate(series):
            color = color_map[(tech, env)]
            payloads = data_map.get(tech, {}).get(env, {})
            if not payloads:
                continue
            xs = sorted(payloads.keys())
            medians = [np.median(payloads[x]) for x in xs]
            ax_pl.plot(xs, medians, color=color, marker="o", linewidth=2.0, label=f"{tech}-{env}")
        ax_pl.set_xlabel("Payload Size (bytes)")
        ax_pl.set_ylabel(ylabel)
        ax_pl.set_title(title)
        ax_pl.grid(True, linestyle="--", alpha=0.5)
        ax_pl.set_xscale("log")
        formatter = ScalarFormatter()
        formatter.set_scientific(False)
        ax_pl.xaxis.set_major_formatter(formatter)
        ax_pl.legend()
        plt.tight_layout()
        plt.show()

    # Payload-based figures
    plot_payload_metric("PDR vs Payload Size", "PDR", payload_pdr)
    plot_payload_metric("Bits-per-Joule vs Payload Size", "Bits per Joule", payload_bpj)

    # Simulation time vs BPJ figure
    fig_st, ax_st = plt.subplots(1, 1, figsize=(8, 4))
    for tech, env, _ in series:
        color = color_map[(tech, env)]
        st_map = simtime_bpj.get(tech, {}).get(env, {})
        if not st_map:
            continue
        xs = sorted(st_map.keys())
        ys = [np.median(st_map[x]) for x in xs]
        ax_st.plot(xs, ys, color=color, marker="o", linewidth=2.0, label=f"{tech}-{env}")
    ax_st.set_xlabel("Simulation Time (s)")
    ax_st.set_ylabel("Bits per Joule")
    ax_st.set_title("Bits-per-Joule vs Simulation Time")
    ax_st.grid(True, linestyle="--", alpha=0.5)
    ax_st.legend()
    plt.tight_layout()
    plt.show()

    # Reliability–energy frontier: scatter PDR vs Energy/Msg, colored by interval
    fig_frontier, ax_frontier = plt.subplots(1, 1, figsize=(8, 4))
    energies = []
    pdrs = []
    intervals_c = []
    markers = ["o", "s", "D", "^", "v", "P", "X"]
    tech_env_marker = {}

    for idx, (tech, env, _) in enumerate(series):
        marker = markers[idx % len(markers)]
        tech_env_marker[(tech, env)] = marker
        pdr_map = interval_pdr.get(tech, {}).get(env, {})
        energy_map = interval_energy_per_msg.get(tech, {}).get(env, {})
        common_intervals = sorted(set(pdr_map.keys()) & set(energy_map.keys()))
        for iv in common_intervals:
            energies.append(np.median(energy_map[iv]))
            pdrs.append(np.median(pdr_map[iv]))
            intervals_c.append(iv)
            ax_frontier.scatter(
                energies[-1],
                pdrs[-1],
                c=[iv],
                cmap="viridis",
                marker=marker,
                edgecolors="k",
                label=f"{tech}-{env}" if iv == common_intervals[0] else None,
            )

    ax_frontier.set_xlabel("Energy per Message (J)")
    ax_frontier.set_ylabel("PDR")
    ax_frontier.set_title("Reliability–Energy Frontier (colored by interval)")
    ax_frontier.grid(True, linestyle="--", alpha=0.5)
    # Legend for tech-env markers
    handles, labels = ax_frontier.get_legend_handles_labels()
    if handles:
        ax_frontier.legend(loc="best")
    # Colorbar for intervals
    if intervals_c:
        sm = plt.cm.ScalarMappable(cmap="viridis")
        sm.set_array(np.array(intervals_c))
        cbar = fig_frontier.colorbar(sm, ax=ax_frontier)
        cbar.set_label("Message Interval (s)")
    plt.tight_layout()
    plt.show()



# ============================================================
# Main CLI
# ============================================================
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", help="Path to a single run folder")
    parser.add_argument("--batch", nargs="+", help="Path(s) to experiment folder(s) (multiple runs)")
    parser.add_argument("--lora", help="Simulation(s) use LoRa", action="store_true")
    args = parser.parse_args()

    if args.input:
        print("Processing single run:", args.input)
        meta_path = os.path.join(args.input, "metadata.json")
        duration = SIMULATION_TIME
        if os.path.exists(meta_path):
            try:
                meta = json.load(open(meta_path))
                duration = meta.get("simTimeSec", SIMULATION_TIME)
            except Exception:
                pass
        if args.lora:
            stats = parse_metrics_json(os.path.join(args.input, "metrics.json"), duration)
        else:
            stats = parse_flowmon_xml(os.path.join(args.input, "flowmon.xml"))
        energy = load_energy_csv(os.path.join(args.input, "energy.csv"))
        plot_single_run(stats, energy, duration)

    elif args.batch:
        all_runs = []
        for batch_path in args.batch:
            print("Processing batch folder:", batch_path)
            runs = load_experiment_runs(batch_path, args.lora)
            if not runs:
                print(f"No runs found in {batch_path}. If your runs use metrics.json (LoRa-style), pass --lora.")
            all_runs.extend(runs)
        if not all_runs:
            print("No runs found across all batch paths.")
        else:
            plot_batch(all_runs)

    else:
        print("Use --input or --batch.")
