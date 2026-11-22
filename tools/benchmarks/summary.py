import csv
import math
import statistics
import sys
from collections import defaultdict

def percentiles(samples):
    ordered = sorted(samples)
    return (
        statistics.median(ordered),
        ordered[math.ceil(0.95 * len(ordered)) - 1],
        ordered[math.ceil(0.99 * len(ordered)) - 1],
    )


if sys.argv[1:2] == ["--opengl"]:
    runs = []
    for filename in sys.argv[2:]:
        with open(filename, newline="") as data:
            rows = [tuple(map(int, row)) for row in csv.reader(data)]
        assert len(rows) == 120 and all(len(row) == 3 for row in rows), filename
        runs.append(tuple(percentiles(column) for column in zip(*rows)))
    print("phase             median_us        P95_us        P99_us")
    for label, values in zip(("CPU submit", "GPU elapsed", "CPU + wait"), zip(*runs)):
        aggregate = (statistics.median(column) / 1000 for column in zip(*values))
        print(f"{label:15s} " + " ".join(f"{value:13.2f}" for value in aggregate))
    sys.exit()

by_case = defaultdict(list)
for filename in sys.argv[1:]:
    with open(filename, newline="") as data:
        by_run = defaultdict(list)
        for row in csv.DictReader(data):
            key = row["phase"], int(row["objects"]), int(row["distinct_geometry"])
            by_run[key].append(int(row["nanoseconds"]))
    for key, samples in by_run.items():
        assert len(samples) == 101, (filename, key, len(samples))
        by_case[key].append(percentiles(samples))

print("phase     objects distinct     median_us        P95_us        P99_us")
for key, runs in sorted(by_case.items()):
    aggregate = (statistics.median(column) / 1000 for column in zip(*runs))
    print(f"{key[0]:7s} {key[1]:9d} {key[2]:8d} " + " ".join(f"{value:13.2f}" for value in aggregate))
