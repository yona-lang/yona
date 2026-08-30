import subprocess

if __name__ == "__main__":
    if __import__("os").name == "nt":
        cmd = ["cmd", "/c", "for /l %i in (1,1,10000) do @echo %i"]
    else:
        cmd = ["seq", "1", "10000"]
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    normalized = result.stdout.replace("\r\n", "\n").replace("\r", "\n")
    print(len(normalized.rstrip("\n")))
