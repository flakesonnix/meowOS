#!/usr/bin/env python3
"""Local HTTP fixture server for meow integration tests.

Serves files from --root with:
  * ETag + 304 Not Modified on If-None-Match (real HTTP caching path)
  * /slow/<path>  -> sleeps (to exercise transfer timeout)
  * /flaky/<path> -> returns 500 for the first N requests, then 200
                     (to exercise retry-on-5xx)

Dev/test only. Not a runtime dependency of meow.
"""
import argparse
import hashlib
import os
import socketserver
import time
from http.server import SimpleHTTPRequestHandler


class FixtureHandler(SimpleHTTPRequestHandler):
    flaky_remaining = 2  # requests that will 500 before succeeding

    def _etag_for(self, path):
        try:
            with open(path, "rb") as f:
                return '"' + hashlib.sha256(f.read()).hexdigest() + '"'
        except OSError:
            return None

    def _send_file(self, path, status=200):
        etag = self._etag_for(path)
        if etag is not None and self.headers.get("If-None-Match") == etag:
            self.send_response(304)
            self.send_header("ETag", etag)
            self.end_headers()
            return
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            self.send_error(404)
            return
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        if etag is not None:
            self.send_header("ETag", etag)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith("/slow/"):
            rel = self.path[len("/slow/"):]
            time.sleep(45)
            self._send_file(os.path.join(self.server.root, rel))
            return
        if self.path.startswith("/flaky/"):
            if FixtureHandler.flaky_remaining > 0:
                FixtureHandler.flaky_remaining -= 1
                self.send_error(500, "transient failure")
                return
            rel = self.path[len("/flaky/"):]
            self._send_file(os.path.join(self.server.root, rel))
            return
        # default: serve from root with ETag/304 support
        rel = self.path.lstrip("/")
        self._send_file(os.path.join(self.server.root, rel))

    def log_message(self, *args):  # silence
        pass


class FixtureServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True

    def __init__(self, root, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.root = root


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--port", type=int, default=0)
    args = ap.parse_args()
    os.chdir(args.root)
    httpd = FixtureServer(args.root, ("127.0.0.1", args.port), FixtureHandler)
    print(f"LISTENING_ON={httpd.server_address[1]}", flush=True)
    httpd.serve_forever()


if __name__ == "__main__":
    main()
