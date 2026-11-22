import argparse
import csv
import math
import re
import statistics
from pathlib import Path


def read_run(samples):
    log = samples.with_suffix(".log").read_text()
    with samples.open(newline="") as data:
        rows = [{name: int(value) for name, value in row.items()} for row in csv.DictReader(data)]
    times = sorted(row["frame_ns"] for row in rows)
    median = statistics.median(times)
    spikes = [row for row in rows if row["frame_ns"] > max(2 * median, 1e9 / 60)]
    swap_spikes = sum(row["swap_ns"] >= row["cpu_submit_ns"] for row in spikes)

    def field(name, default=""):
        match = re.search(rf"\b{name}=([^\s]+)", log)
        return match[1] if match else default

    def memory(name):
        match = re.search(rf"^{name}:\s+(\d+) kB", log, re.MULTILINE)
        return int(match[1]) if match else None

    return {
        "run": samples.stem,
        "driver": field("driver"),
        "requested": field("requested"),
        "pixels": field("pixels"),
        "display": field("display"),
        "scale": field("scale"),
        "interval_set": field("interval_set"),
        "interval_known": field("interval_known"),
        "swap_interval": field("swap_interval") if field("interval_known") == "1" else "unavailable",
        "frames": len(rows),
        "fps": len(rows) / float(field("wall_seconds")),
        "avg_ms": statistics.mean(times) / 1e6,
        "median_ms": median / 1e6,
        "p95_ms": times[math.ceil(0.95 * len(times)) - 1] / 1e6,
        "p99_ms": times[math.ceil(0.99 * len(times)) - 1] / 1e6,
        "cpu_median_ms": statistics.median(row["cpu_submit_ns"] for row in rows) / 1e6,
        "gpu_median_ms": statistics.median(row["gpu_ns"] for row in rows) / 1e6,
        "cpu_percent_one_core": field("cpu_percent_one_core"),
        "drm_render_engine_percent": field("drm_render_engine_percent"),
        "rss_kib": memory("Rss"),
        "pss_kib": memory("Pss"),
        "spikes": len(spikes),
        "swap_dominated_spikes": swap_spikes,
    }


def main():
    parser = argparse.ArgumentParser(description="Сводка пар кадр/журнал engine_resolution_benchmark")
    parser.add_argument("runs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    rows = [read_run(path) for path in args.runs]
    with args.output.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
