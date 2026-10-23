#!/usr/bin/env python3
"""py_pub.py — Zenoh 原生发布端 (Python 版)

与 C++ 版 native_lab/src/z_pub.cpp 行为一致:
  Config → open → declare_publisher → put

用法:
  python3 py_pub.py [key] [rate_hz] [count] [connect_endpoint]
  python3 py_pub.py demo/cross 2 10 tcp/192.168.10.1:7447
  python3 py_pub.py demo/cross 2 10            # peer 模式 (组播自动发现)
count=0 表示一直发。
"""
import argparse
import json
import socket
import sys
import time

import zenoh


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("key", nargs="?", default="demo/cross")
    ap.add_argument("rate_hz", nargs="?", type=float, default=1.0)
    ap.add_argument("count", nargs="?", type=int, default=0)
    ap.add_argument("endpoint", nargs="?", default="",
                    help="例: tcp/192.168.10.1:7447；留空则 peer 模式")
    a = ap.parse_args()

    cfg = zenoh.Config()
    if a.endpoint:
        cfg.insert_json5("mode", '"client"')
        cfg.insert_json5("connect/endpoints", json.dumps([a.endpoint]))
        cfg.insert_json5("scouting/multicast/enabled", "false")

    session = zenoh.open(cfg)
    publisher = session.declare_publisher(a.key)
    mode = f"client -> {a.endpoint}" if a.endpoint else "peer (组播发现)"
    print(f"[PUB] key={a.key} mode={mode} rate={a.rate_hz}Hz "
          f"count={a.count or '∞'}", flush=True)

    period = 1.0 / (a.rate_hz if a.rate_hz > 0 else 1.0)
    seq = 0
    try:
        while a.count == 0 or seq < a.count:
            msg = {
                "src": "py_pub",
                "seq": seq,
                "host": socket.gethostname(),
                "ts_ms": int(time.time() * 1000),
            }
            payload = json.dumps(msg, ensure_ascii=False)
            publisher.put(payload, encoding=zenoh.Encoding.APPLICATION_JSON)
            print(f"[PUB] #{seq}  {a.key}  <- {payload}", flush=True)
            seq += 1
            time.sleep(period)
    except KeyboardInterrupt:
        pass

    publisher.undeclare()
    session.close()
    print(f"SUMMARY role=pub key={a.key} sent={seq}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
