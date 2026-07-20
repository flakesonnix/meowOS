#!/usr/bin/env python3
"""Test HTTP fixture server for meow integration tests.

Serves files from a --root directory over HTTP. Started by
test/integration/common.sh::startHttp with --port 0, prints the chosen port
as `LISTENING_ON=<port>` on stdout, and keeps running until killed.

Special paths (used by section 04.http.sh):
  /flaky/<file>  -> responds 500 for the first 2 requests, then serves the file
                    (exercises client retry / backoff on 5xx).
  /slow/<file>   -> delays the response long enough to trigger a client-side
                    download timeout, then serves the file.
Any other path maps to a file under --root (404 if absent).
"""

import argparse
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, unquote

ROOT = "/tmp/meow-http-root"
SLOW_DELAY = 35.0  # seconds; must exceed the client HTTP timeout
FLAKY_ATTEMPTS = 2  # number of 500s before a flaky path succeeds


def content_type(path: str) -> str:
    if path.endswith(".toml"):
        return "application/toml; charset=utf-8"
    return "application/octet-stream"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):  # silence default logging
        pass

    def _serve_file(self, real_path: str, status: int = 200):
        try:
            with open(real_path, "rb") as f:
                data = f.read()
        except OSError:
            self.send_error(404, "Not Found")
            return
        self.send_response(status)
        self.send_header("Content-Type", content_type(real_path))
        self.send_header("Content-Length", str(len(data)))
        self.send_header("ETag", '"%d"' % os.path.getmtime(real_path))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def do_GET(self):
        self.handle_request()

    def do_HEAD(self):
        self.handle_request()

    def handle_request(self):
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        if path.startswith("/"):
            path = path[1:]

        # /slow/... : force a client timeout before serving.
        if path.startswith("slow/"):
            time.sleep(SLOW_DELAY)
            target = os.path.join(ROOT, path[len("slow/"):])
            self._serve_file(target)
            return

        # /flaky/... : fail the first FLAKY_ATTEMPTS requests with 500.
        if path.startswith("flaky/"):
            key = path
            with state_lock:
                attempts[key] = attempts.get(key, 0) + 1
                n = attempts[key]
            if n <= FLAKY_ATTEMPTS:
                self.send_error(500, "Internal Server Error")
                return
            target = os.path.join(ROOT, path[len("flaky/"):])
            self._serve_file(target)
            return

        # Normal file serving with traversal protection.
        target = os.path.normpath(os.path.join(ROOT, path))
        if not target.startswith(os.path.normpath(ROOT)) or not os.path.isfile(target):
            self.send_error(404, "Not Found")
            return
        self._serve_file(target)


state_lock = threading.Lock()
attempts: dict = {}


def main():
    global ROOT
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="/tmp/meow-http-root")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()
    ROOT = os.path.abspath(args.root)

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    port = httpd.server_address[1]
    print("LISTENING_ON=%d" % port, flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()


if __name__ == "__main__":
    main()
