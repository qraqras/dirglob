import os
import subprocess
import shutil
import time

BENCH_EXEC = "./examples/bench_glob"
DATA_DIR = "bench_data"

def setup_data():
    if os.path.exists(DATA_DIR):
        shutil.rmtree(DATA_DIR)
    os.makedirs(DATA_DIR)

    # Create 1000 files in root
    for i in range(1000):
        with open(os.path.join(DATA_DIR, f"file{i}.txt"), "w") as f:
            f.write("test")

    # Create subdirs
    for d in ["sub1", "sub2", "sub3", "noise1", "noise2"]:
        p = os.path.join(DATA_DIR, d)
        os.makedirs(p)
        for i in range(100):
            with open(os.path.join(p, f"deep{i}.dat"), "w") as f:
                f.write("test")

def run_bench(pattern, iter_count=1000):
    cmd = [BENCH_EXEC, pattern, str(iter_count)]
    print(f"--- Running: {' '.join(cmd)} ---")
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running benchmark: {e}")

if __name__ == "__main__":
    if not os.path.exists(BENCH_EXEC):
        print(f"Error: {BENCH_EXEC} not found. Build the project first.")
        exit(1)

    print("Setting up test data...")
    setup_data()

    print("\nBenchmark 1: Literal Branch (Optimization Target)")
    # Should use stat() instead of readdir()
    # Pattern matches 3 files out of 1000.
    run_bench(f"{DATA_DIR}/file{{1,2,3}}.txt", 2000)

    print("\nBenchmark 2: Wildcard Branch (LCP optimization)")
    # Should scan but use prefix matching
    run_bench(f"{DATA_DIR}/{{sub1,sub2}}/deep*.dat", 500)

    print("\nBenchmark 3: Baseline Wildcard")
    run_bench(f"{DATA_DIR}/*.txt", 500)

    print("\nBenchmark 4: Complex Brace")
    run_bench(f"{DATA_DIR}/{{sub1,sub2,noise1}}/deep{{1,2}}*.dat", 500)

    print("\nBenchmark 5: Cross-Segment Brace")
    # {sub1/deep,sub2/deep}1*.dat -> Checks if we can handle slashes inside braces optimally
    run_bench(f"{DATA_DIR}/{{sub1/deep,sub2/deep}}1*.dat", 500)

    print("\nBenchmark 6: Braces with Wildcards")
    # {*.txt,*.dat} in a directory with mix of txt and dat
    # This specifically tests if our LCP logic combines these into one NFA efficiently
    # or if we are forced to re-scan.
    # Note: root has 1000 .txt files. recursive directories have .dat.
    # Let's create a specific mixed directory for this.
    mixed_dir = os.path.join(DATA_DIR, "mixed")
    if not os.path.exists(mixed_dir):
        os.makedirs(mixed_dir)
        for i in range(500):
            with open(os.path.join(mixed_dir, f"f{i}.txt"), "w") as f: f.write("x")
            with open(os.path.join(mixed_dir, f"f{i}.md"), "w") as f: f.write("x")

    run_bench(f"{mixed_dir}/{{*.txt,*.md}}", 500)

    print("\nBenchmark 7: Pure Literal (No Brace)")
    run_bench(f"{DATA_DIR}/file100.txt", 2000)

    print("\nBenchmark 8: Question Mark Wildcard")
    run_bench(f"{DATA_DIR}/file?.txt", 500)

    print("\nBenchmark 9: Character Class")
    run_bench(f"{DATA_DIR}/file[1-3].txt", 2000)

    # Clean up?
    # shutil.rmtree(DATA_DIR)
