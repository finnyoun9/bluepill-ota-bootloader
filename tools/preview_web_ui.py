#!/usr/bin/env python3
"""Serve the ESP32 embedded Web UI for local browser preview."""

import argparse
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import URLError
from urllib.request import Request, urlopen


def extract_page(header: Path) -> bytes:
    source = header.read_text(encoding="utf-8")
    match = re.search(r'R"WEBOTA\(([\s\S]*?)\)WEBOTA";', source)
    if match is None:
        raise ValueError(f"embedded Web UI not found in {header}")
    return match.group(1).encode("utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument(
        "--device",
        help="ESP32 base URL used to proxy live API data, e.g. http://192.168.0.104",
    )
    parser.add_argument(
        "--header",
        type=Path,
        default=Path(__file__).parents[1]
        / "esp32-comm-bridge"
        / "src"
        / "web_ota_page.h",
    )
    args = parser.parse_args()
    page = extract_page(args.header)
    device = args.device.rstrip("/") if args.device else None

    class Handler(BaseHTTPRequestHandler):
        def proxy_device(self, method: str, body: bytes | None = None) -> None:
            if device is None:
                self.send_error(503, "start preview with --device to proxy live data")
                return
            try:
                headers = {"Cache-Control": "no-store"}
                if body is not None:
                    headers["Content-Type"] = "application/json"
                request = Request(
                    device + self.path,
                    data=body,
                    headers=headers,
                    method=method,
                )
                with urlopen(request, timeout=3) as response:
                    response_body = response.read()
                    self.send_response(response.status)
                    self.send_header(
                        "Content-Type",
                        response.headers.get("Content-Type", "application/json"),
                    )
                    self.send_header("Cache-Control", "no-store")
                    self.send_header("Content-Length", str(len(response_body)))
                    self.end_headers()
                    self.wfile.write(response_body)
            except URLError as error:
                self.send_error(502, f"device API unavailable: {error.reason}")

        def do_GET(self) -> None:
            if self.path.split("?", 1)[0] in {"/api/status", "/api/sensors"}:
                self.proxy_device("GET")
                return

            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(page)))
            self.end_headers()
            self.wfile.write(page)

        def do_POST(self) -> None:
            if self.path.split("?", 1)[0] != "/api/control":
                self.send_error(405, "only /api/control is proxied")
                return
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length >= 64:
                self.send_error(400, "invalid control body")
                return
            self.proxy_device("POST", self.rfile.read(content_length))

        def log_message(self, _format: str, *_args: object) -> None:
            return

    suffix = "" if device else "?demo=1"
    print(f"Preview: http://{args.host}:{args.port}/{suffix}")
    if device:
        print(f"Live API proxy: {device}")
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
