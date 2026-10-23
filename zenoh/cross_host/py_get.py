#!/usr/bin/env python3
"""py_get.py — Zenoh 查询客户端 (Python 版)

用法:
  python3 py_get.py [selector] [timeout_ms] [connect_endpoint]
  python3 py_get.py 'demo/robot/**' 2000 tcp/192.168.10.1:7447
"""
import argparse
import json
import sys

import zenoh


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("selector", nargs="?", default="demo/robot/**")
    ap.add_argument("timeout_ms", nargs="?", type=int, default=2000)
    ap.add_argument("endpoint", nargs="?", default="")
    a = ap.parse_args()

    cfg = zenoh.Config()
    if a.endpoint:
        cfg.insert_json5("mode", '"client"')
        cfg.insert_json5("connect/endpoints", json.dumps([a.endpoint]))
        cfg.insert_json5("scouting/multicast/enabled", "false")

    ok = 0
    err = 0

    # get() 返回一个 Handler[Reply]，直接迭代即可逐条处理；
    # 迭代会在查询完成(或超时)时结束 —— 不要拿到返回值就关 session。
    with zenoh.open(cfg) as session:
        for reply in session.get(a.selector, timeout=a.timeout_ms):
            if reply.ok is not None:
                s = reply.ok
                payload = bytes(s.payload).decode("utf-8", "backslashreplace")
                print(f"[GET] reply  key={s.key_expr}  enc={s.encoding}  payload={payload}",
                      flush=True)
                ok += 1
            else:
                print(f"[GET] error  {reply.err}", flush=True)
                err += 1

    print(f"SUMMARY role=get selector={a.selector} replies={ok} errors={err}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
