#!/usr/bin/env python3
"""py_sub.py — Zenoh 原生订阅端 (Python 版)

用法:
  python3 py_sub.py [key] [duration_s] [connect_endpoint]
  python3 py_sub.py demo/cross 20 tcp/192.168.10.1:7447
  python3 py_sub.py '**' 10 tcp/192.168.10.1:7447   # 看所有 key
duration=0 表示一直订阅 (Ctrl-C 退出)。
"""
import argparse
import json
import sys
import time

import zenoh


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("key", nargs="?", default="demo/cross")
    ap.add_argument("duration", nargs="?", type=float, default=0.0)
    ap.add_argument("endpoint", nargs="?", default="",
                    help="例: tcp/192.168.10.1:7447；留空则 peer 模式")
    a = ap.parse_args()

    cfg = zenoh.Config()
    if a.endpoint:
        cfg.insert_json5("mode", '"client"')
        cfg.insert_json5("connect/endpoints", json.dumps([a.endpoint]))
        cfg.insert_json5("scouting/multicast/enabled", "false")

    session = zenoh.open(cfg)
    count = 0

    def on_sample(sample: zenoh.Sample) -> None:
        nonlocal count
        count += 1
        raw = bytes(sample.payload)
        try:
            payload = raw.decode("utf-8")
        except UnicodeDecodeError:
            payload = raw.hex()
        print(f"[SUB] #{count}  key={sample.key_expr}  "
              f"enc={sample.encoding}  payload={payload}", flush=True)

    subscriber = session.declare_subscriber(a.key, on_sample)
    mode = f"client -> {a.endpoint}" if a.endpoint else "peer (组播发现)"
    print(f"[SUB] subscribed {a.key}  mode={mode}", flush=True)

    deadline = time.time() + a.duration if a.duration > 0 else None
    try:
        while deadline is None or time.time() < deadline:
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass

    subscriber.undeclare()
    session.close()
    print(f"SUMMARY role=sub key={a.key} received={count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
