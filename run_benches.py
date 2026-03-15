#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

def copy_workdir_to(dst: str) -> None:
    '''
    Copy the entire current working directory into dst.
    Requires Python 3.8+ for dirs_exist_ok.
    '''
    src = os.getcwd()
    shutil.copytree(src, dst, dirs_exist_ok=True)

def start_run(run_id: int) -> dict:
    '''
    Create a temp dir, copy the current working dir into it,
    then start `make bench` in that temp dir.
    Returns a dict tracking the process and paths.
    '''
    tmpdir = tempfile.mkdtemp(prefix=f"bench_{run_id}_")
    try:
        copy_workdir_to(tmpdir)
    except Exception as e:
        print(f"[run {run_id}] Failed to copy working directory: {e}", file=sys.stderr)
        shutil.rmtree(tmpdir, ignore_errors=True)
        raise

    log_path = os.path.join(tmpdir, "make_bench.log")
    try:
        # Open a log file for stdout+stderr
        log_file = open(log_path, "w")
        proc = subprocess.Popen(
            ["make", "bench"],
            cwd=tmpdir,
            stdout=log_file,
            stderr=subprocess.STDOUT,
        )
        # Parent no longer needs the handle; child keeps its fd
        log_file.close()
    except Exception as e:
        print(f"[run {run_id}] Failed to start 'make bench': {e}", file=sys.stderr)
        shutil.rmtree(tmpdir, ignore_errors=True)
        raise

    return {"id": run_id, "proc": proc, "tmpdir": tmpdir, "log_path": log_path}

def print_log(log_path: str, run_id: int) -> None:
    '''Print the captured output for a failed run, if available.'''
    try:
        with open(log_path, "r") as f:
            content = f.read()
        if content.strip():
            print(f"\n=== Output for failed run #{run_id} ===")
            print(content)
            print("=== End of output ===\n")
        else:
            print(f"(run #{run_id} produced no output)", file=sys.stderr)
    except Exception as e:
        print(f"(could not read log for run #{run_id}: {e})", file=sys.stderr)

def main():
    parser = argparse.ArgumentParser(
        description="Run `make bench` many times in parallel, each in its own temp copy of the cwd."
    )
    parser.add_argument(
        "-n", "--num-runs",
        type=int,
        default=1000,
        help="Total number of runs to execute (default: 1000).",
    )
    parser.add_argument(
        "-j", "--jobs",
        type=int,
        default=72,
        help="Maximum number of concurrent runs (default: 72).",
    )
    args = parser.parse_args()

    total_runs = args.num_runs
    max_parallel = max(1, args.jobs)

    next_run_id = 1
    completed = 0
    running = []

    # Start initial batch
    while next_run_id <= total_runs and len(running) < max_parallel:
        try:
            info = start_run(next_run_id)
        except Exception:
            # If we can't even start a run, bail out.
            sys.exit(1)
        running.append(info)
        next_run_id += 1

    # Main loop: monitor running processes
    while running:
        # Iterate over a snapshot since we may remove items
        for info in list(running):
            proc = info["proc"]
            run_id = info["id"]
            tmpdir = info["tmpdir"]
            log_path = info["log_path"]

            ret = proc.poll()
            if ret is None:
                # Still running
                continue

            # Process finished
            running.remove(info)

            if ret == 0:
                # Success: don't show output, just say it finished
                completed += 1
                print(f"run #{run_id} completed", flush=True)

                # Clean up temp directory for this run
                shutil.rmtree(tmpdir, ignore_errors=True)

                # Start next run if we still have more to do
                if next_run_id <= total_runs:
                    try:
                        new_info = start_run(next_run_id)
                    except Exception:
                        # Could not start another run; terminate everything
                        # and exit with error.
                        print(f"Failed to start run #{next_run_id}, aborting.", file=sys.stderr)
                        # Terminate any remaining running processes
                        for other in running:
                            other["proc"].terminate()
                        for other in running:
                            try:
                                other["proc"].wait(timeout=10)
                            except subprocess.TimeoutExpired:
                                other["proc"].kill()
                                other["proc"].wait()
                            shutil.rmtree(other["tmpdir"], ignore_errors=True)
                        sys.exit(1)

                    running.append(new_info)
                    next_run_id += 1
            else:
                # Failure: show output for this run, then stop everything
                print(f"run #{run_id} failed with exit code {ret}, aborting.", file=sys.stderr)
                print_log(log_path, run_id)

                # Terminate all other running processes
                for other in running:
                    other["proc"].terminate()

                # Wait for them to die, then clean up their temp dirs
                for other in running:
                    try:
                        other["proc"].wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        other["proc"].kill()
                        other["proc"].wait()
                    shutil.rmtree(other["tmpdir"], ignore_errors=True)

                # Clean up this run's temp dir last
                shutil.rmtree(tmpdir, ignore_errors=True)

                sys.exit(ret if ret != 0 else 1)

        # Avoid busy-waiting
        time.sleep(0.1)

    # All done successfully
    print(f"All {completed} runs completed successfully.")
    sys.exit(0)


if __name__ == "__main__":
    main()
