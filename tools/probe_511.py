#!/usr/bin/env python3
"""Probe the 511.org transit API and answer the questions the firmware depends on.

Run this once before writing any client code. It settles, against the live API:

  1. Does api.511.org serve HTTPS?  The published docs show http:// URLs. If TLS
     is unavailable the firmware's whole cert-validation design is moot, and we
     would rather learn that here than on the device.
  2. Does it gzip?  Matters because the ESP32's HTTPClient does not transparently
     inflate, and a compressed body would arrive as garbage.
  3. Is there a UTF-8 BOM?  ArduinoJson rejects a document that starts with one.
  4. How large is a StopMonitoring response?  Decides buffer vs stream parsing.
  5. Which schedule endpoint, if any, can back the offline timetable cache?

BUDGET: the free tier allows 60 requests/hour. A full run costs well under that,
but re-running it in a tight loop will get you throttled.

The token is read from $CT_511_TOKEN or the gitignored .511-token file. It is
never printed, and it is stripped from anything written to disk.

    python3 tools/probe_511.py
"""

import argparse
import gzip
import io
import json
import os
import pathlib
import sys
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
CAPTURE_DIR = ROOT / "tools" / "captures"

# There is deliberately NO default stop. A developer tool that points at one
# platform out of the box expresses somebody's preference rather than an example,
# and this one talks to a live API on the author's behalf. The stop is required
# on the command line, the same way configDefaults() initialises origin and
# destination to -1 rather than to anyone's stations.
#
# Any 5-digit GTFS platform id works. The encoding is base*10 + a direction
# digit: +1 northbound, +2 southbound. See kStations in src/stations.h for the
# bases and stationStopId() in src/route.h, which is the only place that mapping
# lives.
AGENCY = "CT"

REDACTED = "REDACTED_TOKEN"


def load_token() -> str:
    token = os.environ.get("CT_511_TOKEN", "").strip()
    if token:
        return token
    path = ROOT / ".511-token"
    if path.exists():
        return path.read_text().strip()
    sys.exit(
        "No 511 token found.\n"
        "  Put it in a file that git ignores:\n"
        f"    echo 'YOUR-TOKEN' > {path}\n"
        "  or export it:  export CT_511_TOKEN=YOUR-TOKEN"
    )


def fetch(url: str, token: str, want_gzip: bool = True):
    """GET a URL, returning (status, headers, raw_bytes, error_or_None).

    We ask for gzip explicitly so we can observe whether the server honours it.
    urllib would otherwise not advertise support and we'd learn nothing.
    """
    req = urllib.request.Request(url)
    if want_gzip:
        req.add_header("Accept-Encoding", "gzip")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return resp.status, dict(resp.headers), resp.read(), None
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers or {}), e.read(), None
    except Exception as e:  # DNS failure, TLS failure, timeout
        return None, {}, b"", f"{type(e).__name__}: {e}"


def describe(raw: bytes, headers: dict) -> dict:
    """Summarise a response body: encoding, BOM, size, and JSON shape."""
    info = {
        "wire_bytes": len(raw),
        "content_encoding": headers.get("Content-Encoding", "none"),
        "content_type": headers.get("Content-Type", "?"),
    }

    body = raw
    if headers.get("Content-Encoding", "").lower() == "gzip":
        try:
            body = gzip.decompress(raw)
            info["inflated_bytes"] = len(body)
        except Exception as e:
            info["gzip_error"] = str(e)

    info["utf8_bom"] = body[:3] == b"\xef\xbb\xbf"
    text = body.decode("utf-8-sig", errors="replace")
    info["decoded_chars"] = len(text)

    try:
        info["json"] = json.loads(text)
    except json.JSONDecodeError as e:
        info["json_error"] = str(e)
        info["head"] = text[:300]
    return info


def top_keys(obj, depth: int = 3, prefix: str = "") -> list[str]:
    """Flatten the first few levels of key names, so we can see the shape."""
    out = []
    if depth == 0:
        return out
    if isinstance(obj, dict):
        for k, v in obj.items():
            path = f"{prefix}.{k}" if prefix else k
            out.append(path)
            out += top_keys(v, depth - 1, path)
    elif isinstance(obj, list) and obj:
        out += top_keys(obj[0], depth, f"{prefix}[0]")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Measure what the live 511 API actually returns for one stop.",
        epilog="Example: tools/probe_511.py --stop 70012   "
               "(a 5-digit GTFS platform id; see kStations in src/stations.h)",
    )
    ap.add_argument(
        "--stop",
        required=True,
        metavar="STOPCODE",
        help="5-digit GTFS platform stop_id to probe, e.g. 70012. Required: "
             "this tool has no default stop on purpose.",
    )
    stop = ap.parse_args().stop

    token = load_token()
    CAPTURE_DIR.mkdir(parents=True, exist_ok=True)

    def url(scheme: str, path: str, **params) -> str:
        params = {"api_key": token, **params}
        query = "&".join(f"{k}={v}" for k, v in params.items())
        return f"{scheme}://api.511.org/transit/{path}?{query}"

    def safe(u: str) -> str:
        return u.replace(token, REDACTED)

    print("=" * 72)
    print("1. TRANSPORT — does api.511.org serve HTTPS?")
    print("=" * 72)
    for scheme in ("https", "http"):
        u = url(scheme, "StopMonitoring", agency=AGENCY, stopcode=stop, format="json")
        status, headers, raw, err = fetch(u, token)
        if err:
            print(f"  {scheme:5} -> FAILED: {err}")
        else:
            print(f"  {scheme:5} -> HTTP {status}, {len(raw)} bytes on the wire")

    print()
    print("=" * 72)
    print("2. STOPMONITORING — encoding, BOM, size, shape")
    print("=" * 72)
    u = url("https", "StopMonitoring", agency=AGENCY, stopcode=stop, format="json")
    status, headers, raw, err = fetch(u, token)
    if err:
        print(f"  request failed: {err}")
        return 1

    print(f"  HTTP {status}")
    if status != 200:
        print(f"  body head: {raw[:400]!r}")
        return 1

    info = describe(raw, headers)
    print(f"  Content-Type     : {info['content_type']}")
    print(f"  Content-Encoding : {info['content_encoding']}")
    print(f"  wire bytes       : {info['wire_bytes']:,}")
    if "inflated_bytes" in info:
        print(f"  inflated bytes   : {info['inflated_bytes']:,}")
    print(f"  UTF-8 BOM        : {info['utf8_bom']}")

    if "json" in info:
        doc = info["json"]
        (CAPTURE_DIR / "stopmonitoring.json").write_text(
            json.dumps(doc, indent=2)
        )
        visits = (
            doc.get("ServiceDelivery", {})
            .get("StopMonitoringDelivery", {})
            .get("MonitoredStopVisit", [])
        )
        print(f"  MonitoredStopVisit entries: {len(visits)}")
        if visits:
            print("\n  --- first visit, flattened keys ---")
            for k in top_keys(visits[0], depth=4):
                print(f"    {k}")
            print("\n  --- first visit, verbatim ---")
            print(json.dumps(visits[0], indent=4)[:2000])
        else:
            print("  (no trains currently scheduled at this stop)")
            print(json.dumps(doc, indent=2)[:1500])
    else:
        print(f"  JSON parse failed: {info.get('json_error')}")
        print(f"  head: {info.get('head')}")

    print()
    print("=" * 72)
    print("3. SCHEDULE ENDPOINTS — which can back the offline cache?")
    print("=" * 72)
    candidates = [
        ("stoptimetable", {"OperatorRef": AGENCY, "MonitoringRef": stop}),
        ("timetable", {"operator_id": AGENCY}),
        ("scheduledepartures", {"agency": AGENCY, "stopcode": stop}),
        ("patterns", {"operator_id": AGENCY, "line_id": "L1"}),
        ("lines", {"operator_id": AGENCY}),
        ("stops", {"operator_id": AGENCY}),
    ]
    for path, params in candidates:
        u = url("https", path, format="json", **params)
        status, headers, raw, err = fetch(u, token)
        if err:
            print(f"  {path:20} FAILED: {err}")
            continue
        note = ""
        if status == 200:
            body = raw
            if headers.get("Content-Encoding", "").lower() == "gzip":
                try:
                    body = gzip.decompress(raw)
                except Exception:
                    pass
            (CAPTURE_DIR / f"{path}.json").write_bytes(body)
            try:
                doc = json.loads(body.decode("utf-8-sig"))
                keys = top_keys(doc, depth=2)[:6]
                note = "  keys: " + ", ".join(keys)
            except Exception:
                note = f"  (non-JSON) head={body[:80]!r}"
        print(f"  {path:20} HTTP {status}  {len(raw):>8,} bytes{note}")

    print()
    print(f"Raw captures written to {CAPTURE_DIR} (gitignored).")
    print("Token was not written to any file.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
