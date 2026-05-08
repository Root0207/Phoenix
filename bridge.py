"""
============================================================
  F2D — Flask Bridge Server (Windows compatible)
  - Compiles server.c using gcc with Winsock2 (-lws2_32)
  - Starts C backend on port 8080
  - Proxies all /api/* from frontend to C backend
  - Frontend talks to Flask on port 5000

  Usage:
    pip install flask flask-cors requests
    python bridge.py
============================================================
"""

import subprocess
import time
import sys
import os
import signal
import platform

import requests
from flask import Flask, request, Response
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

C_SOURCE = "server.c"
C_PORT   = 8080
C_URL    = f"http://localhost:{C_PORT}"

IS_WINDOWS = platform.system() == "Windows"
C_BINARY   = "server.exe" if IS_WINDOWS else "./server"

c_process = None

def compile_c():
    if not os.path.exists(C_SOURCE):
        print(f"[ERROR] {C_SOURCE} not found in current folder.")
        sys.exit(1)

    print(f"[Bridge] Compiling {C_SOURCE} ...")

    cmd = ["gcc", C_SOURCE, "-o", C_BINARY]
    if IS_WINDOWS:
        cmd.append("-lws2_32")

    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print("[ERROR] Compilation failed:")
        print(result.stderr)
        sys.exit(1)

    print("[Bridge] Compilation successful ✓")

def start_c_server():
    global c_process
    print(f"[Bridge] Starting C backend on port {C_PORT} ...")
    c_process = subprocess.Popen(
        [C_BINARY],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    time.sleep(1)

    if c_process.poll() is not None:
        out, err = c_process.communicate()
        print("[ERROR] C backend failed to start:")
        print(err.decode())
        sys.exit(1)

    print(f"[Bridge] C backend running (PID {c_process.pid}) ✓")

def wait_for_c(retries=15):
    print("[Bridge] Waiting for C backend to be ready...")
    for i in range(retries):
        try:
            requests.get(f"{C_URL}/api/tables", timeout=1)
            print("[Bridge] C backend is responding ✓")
            return
        except Exception:
            time.sleep(0.5)
    print("[WARN] C backend may not be ready — proceeding anyway.")

@app.route("/api/<path:subpath>", methods=["GET", "POST", "OPTIONS"])
def proxy(subpath):
    target = f"{C_URL}/api/{subpath}"
    headers = {
        k: v for k, v in request.headers
        if k.lower() not in ("host", "content-length")
    }
    try:
        resp = requests.request(
            method=request.method,
            url=target,
            headers=headers,
            data=request.get_data(),
            timeout=10,
            allow_redirects=False,
        )
        return Response(resp.content, status=resp.status_code, headers=dict(resp.headers))
    except requests.exceptions.ConnectionError:
        return {"error": "C backend is not running"}, 502
    except requests.exceptions.Timeout:
        return {"error": "C backend timed out"}, 504

@app.route("/")
def index():
    for name in ["hotel_reservation.html", "hotel_reservation__1_.html"]:
        if os.path.exists(name):
            with open(name, "r", encoding="utf-8") as f:
                return f.read()
    return "<h2>Place hotel_reservation.html in the same folder as bridge.py</h2>"


if __name__ == "__main__":
    compile_c()
    start_c_server()
    wait_for_c()

    print()
    print("=" * 54)
    print("  F2D Smart Dining — Running!")
    print(f"  Open browser →  http://localhost:5000")
    print(f"  C backend    →  http://localhost:{C_PORT}")
    print()
    print("  Demo logins:")
    print("    customer@f2d.com / demo123")
    print("    manager@f2d.com  / demo123")
    print("=" * 54)
    print()

    app.run(port=5000, debug=False)