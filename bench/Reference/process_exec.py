# Parallel exec to match Yona's let-parallelism
import os
import subprocess
from concurrent.futures import ThreadPoolExecutor


def _echo_cmd(word):
    if os.name == "nt":
        return ["cmd", "/c", "echo", word]
    return ["sh", "-lc", f"echo {word}"]


def exec_len(word):
    r = subprocess.run(
        _echo_cmd(word), capture_output=True, text=True, check=True
    )
    return len(r.stdout.rstrip("\r\n"))


with ThreadPoolExecutor(3) as ex:
    futures = [ex.submit(exec_len, w) for w in ["hello", "world", "yona"]]
    print(sum(f.result() for f in futures))
