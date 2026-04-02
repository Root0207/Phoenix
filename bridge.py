"""
============================================================
  F2D — Flask Bridge Server
  - Compiles server.c automatically
  - Starts the C backend on port 8080
  - Proxies all /api/* requests from frontend to C backend
  - Frontend only needs to talk to Flask on port 5000
============================================================
  Usage:
    pip install flask flask-cors requests
    python bridge.py
============================================================
"""

import subprocess
import threading
import time
import sys
import os
import signal

import requests
from flask import Flask, request, Response
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

C_SOURCE  = "server.c"       # path to your C file
C_BINARY  = "./server"       # compiled output
C_PORT    = 8080             # port the C server listens on
C_URL     = f"http://localhost:{C_PORT}"

c_process = None             # holds the running C subprocess

# ─── Step 1: Compile the C backend ───────────────────────
def compile_c():
    if not os.path.exists(C_SOURCE):
        print(f"[ERROR] {C_SOURCE} not found. Place server.c in the same folder.")
        sys.exit(1)

    print(f"[Bridge] Compiling {C_SOURCE} ...")
    result = subprocess.run(
        ["gcc", C_SOURCE, "-o", C_BINARY],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("[ERROR] Compilation failed:\n", result.stderr)
        sys.exit(1)

    print("[Bridge] Compilation successful ✓")

# ─── Step 2: Start the C backend process ─────────────────
def start_c_server():
    global c_process
    print(f"[Bridge] Starting C backend on port {C_PORT} ...")
    c_process = subprocess.Popen(
        [C_BINARY],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    # Give it a moment to boot
    time.sleep(1)

    if c_process.poll() is not None:
        print("[ERROR] C server failed to start.")
        out, err = c_process.communicate()
        print(err.decode())
        sys.exit(1)

    print(f"[Bridge] C backend running (PID {c_process.pid}) ✓")

# ─── Step 3: Wait until C server is ready ────────────────
def wait_for_c(retries=10):
    for i in range(retries):
        try:
            requests.get(f"{C_URL}/api/tables", timeout=1)
            print("[Bridge] C backend is responding ✓")
            return
        except Exception:
            time.sleep(0.5)
    print("[WARN] C backend may not be ready yet, proceeding anyway.")

# ─── Proxy: forward every /api/* request to C server ─────
@app.route("/api/<path:subpath>", methods=["GET", "POST", "OPTIONS"])
def proxy(subpath):
    target_url = f"{C_URL}/api/{subpath}"

    # Forward headers & body
    headers = {
        key: value for key, value in request.headers
        if key.lower() not in ("host", "content-length")
    }

    try:
        resp = requests.request(
            method  = request.method,
            url     = target_url,
            headers = headers,
            data    = request.get_data(),
            timeout = 10,
            allow_redirects = False,
        )
        # Return C server's response back to the browser
        return Response(
            resp.content,
            status  = resp.status_code,
            headers = dict(resp.headers),
        )
    except requests.exceptions.ConnectionError:
        return {"error": "C backend is not running"}, 502
    except requests.exceptions.Timeout:
        return {"error": "C backend timed out"}, 504

# ─── Serve the HTML frontend directly (optional) ─────────
@app.route("/")
def index():
    html_file = "hotel_reservation.html"
    if os.path.exists(html_file):
        with open(html_file, "r", encoding="utf-8") as f:
            return f.read()
    return "<h2>Put hotel_reservation.html in the same folder</h2>"

# ─── Clean up C process on exit ──────────────────────────
def cleanup(sig=None, frame=None):
    global c_process
    if c_process and c_process.poll() is None:
        print("\n[Bridge] Stopping C backend...")
        c_process.terminate()
        c_process.wait()
        print("[Bridge] C backend stopped.")
    sys.exit(0)

signal.signal(signal.SIGINT,  cleanup)
signal.signal(signal.SIGTERM, cleanup)

# ─── Main ─────────────────────────────────────────────────
if __name__ == "__main__":
    compile_c()
    start_c_server()
    wait_for_c()

    print()
    print("=" * 52)
    print("  F2D Smart Dining — Bridge Server")
    print("  Open your browser:  http://localhost:5000")
    print("  C backend running:  http://localhost:8080")
    print("  Demo logins:")
    print("    customer@f2d.com / demo123")
    print("    manager@f2d.com  / demo123")
    print("=" * 52)
    print()

    app.run(port=5000, debug=False)
