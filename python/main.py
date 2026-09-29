import subprocess
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build" / "duckbomb"

print("========================================", flush=True)
print("DuckBomb App Lab launcher", flush=True)
print("========================================", flush=True)

if not BIN.exists():
    print(f"ERRORE: backend non trovato:", flush=True)
    print(BIN, flush=True)
    print("", flush=True)
    print("Compilalo prima via SSH con:", flush=True)
    print(
        "g++ -std=c++20 -O2 -pthread "
        "src/main.cpp src/code_manager.cpp src/hc12_sender.cpp "
        "-o build/duckbomb",
        flush=True
    )
    sys.exit(1)

print(f"Backend trovato: {BIN}", flush=True)
print("Avvio DuckBomb backend...", flush=True)

try:
    subprocess.run(
        [str(BIN)],
        cwd=str(ROOT),
        check=True
    )
except subprocess.CalledProcessError as e:
    print(f"Backend terminato con codice {e.returncode}", flush=True)
    sys.exit(e.returncode)
except KeyboardInterrupt:
    print("DuckBomb arrestato.", flush=True)