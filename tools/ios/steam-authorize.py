#!/usr/bin/env python3
"""Show DepotDownloader's rotating Steam Guard QR on a loopback-only page.

No login keys, passwords, QR images or tokens are written to disk. The child
uses fetch-engine.command's fixed DLL allowlist and does not remember login.
The page expires after ten minutes; a timeout terminates the download helper.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
STATE = {"svg": "", "status": "Connecting to Steam…", "done": False}
LOCK = threading.Lock()


def qr_svg(rows):
    # DepotDownloader prints each square as two identical terminal characters.
    if len(rows) < 21 or any(len(row) % 2 for row in rows):
        return None
    width = max(map(len, rows)) // 2
    if width != len(rows):
        return None
    squares = []
    for y, row in enumerate(rows):
        for x in range(len(row) // 2):
            pair = row[2*x:2*x+2]
            if pair not in ("██", "  "):
                return None
            if pair == "██":
                squares.append(f'<rect x="{x}" y="{y}" width="1" height="1"/>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {width}" '
            'shape-rendering="crispEdges"><rect width="100%" height="100%" fill="white"/>'
            '<g fill="black">' + ''.join(squares) + '</g></svg>')


PAGE = b'''<!doctype html><html><head><meta charset="utf-8"><title>Steam authorization</title>
<style>body{font:18px system-ui;margin:48px auto;max-width:560px;padding:0 24px;background:#171d25;color:#eee}
h1{font-size:28px}img{width:min(100%,400px);background:white;border-radius:8px}p{line-height:1.5}.note{color:#b9c5d1}</style></head>
<body><h1>Authorize Titanfall 2's download</h1><p>Open the Steam mobile app, choose Steam Guard, and scan this code.</p>
<img id="qr" alt="Waiting for Steam's QR code"><p id="status">Connecting to Steam...</p>
<button id="retry" hidden onclick="this.disabled=true;fetch('/retry',{method:'POST'}).finally(()=>this.disabled=false)">Show a new Steam QR code</button>
<p class="note">Only engine files will download. Your Steam login won't be saved. This page expires after ten minutes.</p>
<script>let last='';async function refresh(){try{let s=await(await fetch('/status',{cache:'no-store'})).json();
document.getElementById('status').textContent=s.status;if(s.svg&&s.svg!==last){last=s.svg;
document.getElementById('qr').src='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(s.svg)}
document.getElementById('qr').hidden=!s.svg;document.getElementById('retry').hidden=!s.done;
}catch(e){document.getElementById('status').textContent='Authorization session ended. Run the helper again to retry.';return}
setTimeout(refresh,1500)}refresh();</script></body></html>'''


class Handler(BaseHTTPRequestHandler):
    def local_request(self):
        host = f'127.0.0.1:{self.server.server_port}'
        return self.headers.get('Host') == host and self.headers.get('Origin', f'http://{host}') == f'http://{host}'

    def do_GET(self):
        if not self.local_request():
            self.send_error(403)
            return
        if self.path == '/':
            body, mime = PAGE, 'text/html; charset=utf-8'
        elif self.path == '/status':
            with LOCK:
                body = json.dumps(STATE).encode()
            mime = 'application/json'
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', mime)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != '/retry' or not self.local_request():
            self.send_error(403)
            return
        self.server.start_download()
        self.send_response(204)
        self.end_headers()

    def log_message(self, *_):
        pass


def main():
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    proc = None
    start_lock = threading.Lock()

    def read_output(child):
        rows = []
        for raw in child.stdout:
            line = raw.rstrip('\r\n')
            if len(line) > 20 and set(line) <= {' ', '█'}:
                rows.append(line)
                continue
            if rows:
                svg = qr_svg(rows)
                if svg:
                    with LOCK:
                        STATE.update(svg=svg, status="Scan with Steam Guard, then approve the sign-in on your phone.")
                    print('QR_READY', flush=True)
                rows = []
            # No raw authentication output is copied into logs or the page.
            lower = line.lower()
            if 'logged on' in lower or 'logging in' in lower and 'qr' not in lower:
                with LOCK:
                    STATE.update(svg='', status='Steam authorized. Fetching engine files…')
            if 'error' in lower or 'failed' in lower or 'no subscription' in lower:
                with LOCK:
                    STATE['status'] = 'Steam reported a download problem. Check the account owns Titanfall 2 and retry.'
        result = child.wait()
        with LOCK:
            STATE.update(svg='', done=True, status=('Engine download finished.' if result == 0
                         else 'Download did not complete. Run the helper again to retry.'))
        print(f'DOWNLOADER_EXIT={result}', flush=True)

    def start_download():
        nonlocal proc
        with start_lock:
            # Retry only after the reader has finished publishing the previous result.
            with LOCK:
                if proc is not None and not STATE['done']:
                    return
                STATE.update(svg='', done=False, status='Connecting to Steam…')
            proc = subprocess.Popen([str(ROOT/'tools/ios/fetch-engine.command')], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, text=True, bufsize=1)
            threading.Thread(target=read_output, args=(proc,), daemon=True).start()

    server.start_download = start_download
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f'AUTH_PAGE=http://127.0.0.1:{server.server_port}/', flush=True)
    start_download()
    try:
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
