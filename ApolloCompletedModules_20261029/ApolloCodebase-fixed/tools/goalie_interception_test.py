#!/usr/bin/env python3
"""Local PlayOn experiment, not an official penalty referee or client dependency."""
import argparse
import gzip
import math
from pathlib import Path
import re
import socket
import subprocess
import time


def parse_sexp(text):
    stack = [[]]
    for token in re.findall(r'\(|\)|[^\s()]+', text):
        if token == '(':
            node = []
            stack[-1].append(node)
            stack.append(node)
        elif token == ')':
            if len(stack) < 2:
                raise ValueError('Unbalanced RSMP expression')
            stack.pop()
        else:
            stack[-1].append(token)
    if len(stack) != 1 or len(stack[0]) != 1:
        raise ValueError('Incomplete RSMP expression')
    return stack[0][0]


def children(expr):
    return [x for x in expr if isinstance(x, list) and x and x[0] == 'nd']


def update_node(expr, old=None):
    node = old if old is not None else {'matrix': None, 'label': None, 'children': []}
    for item in expr:
        if not isinstance(item, list) or not item:
            continue
        if item[0] == 'SLT':
            vals = list(map(float, item[1:]))
            if len(vals) != 16 or not all(map(math.isfinite, vals)):
                raise ValueError('Invalid transform')
            node['matrix'] = vals
        elif item[0] == 'ball':
            node['label'] = 'ball'
        elif item[0] == 'agent':
            node['label'] = 'agent:' + ':'.join(item[1:])
    desc = children(expr)
    if old is not None and len(desc) != len(node['children']):
        raise ValueError('Scene changed without full state')
    node['children'] = [update_node(c, node['children'][i] if old is not None else None)
                        for i, c in enumerate(desc)]
    return node


IDENTITY = [float(i % 5 == 0) for i in range(16)]


def matmul(a, b):
    # Column-major transforms, matching server's SLT serialization.
    return [sum(a[k*4+r]*b[c*4+k] for k in range(4))
            for c in range(4) for r in range(4)]


def collect(node, out, parent=IDENTITY, pending=None):
    matrix = node['matrix']
    world = matmul(parent, matrix) if matrix is not None else parent
    label = node['label'] or pending
    if label and matrix is not None:
        out[label] = (world[12:15], world[10])
        label = None
    for child in node['children']:
        collect(child, out, world, label)


class Scene:
    def __init__(self):
        self.roots = []
        self.modes = []
        self.left_team = None
        self.mode = None

    def frame(self, data):
        expr = parse_sexp(data.decode())
        sections = {x[0][0]: x for x in expr
                    if isinstance(x, list) and x and isinstance(x[0], list) and x[0]}
        env = sections.get('ge', [])
        for item in env:
            if isinstance(item, list) and item and item[0] == 'play_modes':
                self.modes = item[1:]
        for item in sections.get('gs', []):
            if isinstance(item, list) and item:
                if item[0] == 'team_left':
                    self.left_team = item[1]
                if item[0] == 'play_mode':
                    index = int(item[1])
                    self.mode = self.modes[index] if index < len(self.modes) else None
        sg = sections['sg']
        desc = children(sg)
        if 'full' in sg:
            self.roots = [update_node(c) for c in desc]
        else:
            if len(desc) != len(self.roots):
                raise ValueError('Missing full scene')
            self.roots = [update_node(c, self.roots[i]) for i, c in enumerate(desc)]
        out = {}
        for root in self.roots:
            collect(root, out)
        return float(sections['gt'][1]), out


class ReadyGate:
    def __init__(self, target_y):
        self.target_y = target_y
        self.previous = None
        self.since = None

    def update(self, t, objects, mode):
        keeper = objects.get('agent:Apollo3Drelease:1')
        ball = objects.get('ball')
        good = (mode or '').replace('_', '').lower() == 'playon' and keeper is not None and ball is not None
        if good:
            p, upright = keeper
            b, _ = ball
            good = (abs(p[0]+27) <= .15 and abs(p[1]) <= 1.58
                    and p[2] > .5 and upright > .95
                    and abs(b[0]+23) <= .03 and abs(b[1]-self.target_y) <= .03)
        if good and self.previous:
            last_t, last_p, last_b = self.previous
            dt = t-last_t
            good = (0 < dt <= .2
                    and math.hypot(p[0]-last_p[0], p[1]-last_p[1])/dt <= .2
                    and math.hypot(b[0]-last_b[0], b[1]-last_b[1])/dt <= .08)
        else:
            good = False
        if not good:
            self.since = None
        elif self.since is None:
            self.since = t
        self.previous = (t, keeper[0][:], ball[0][:]) if keeper and ball else None
        return good and t-self.since >= .5


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--side', choices=['positive', 'negative'], required=True)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--mport', type=int, default=60001)
    ap.add_argument('--output-dir', type=Path, default=Path('/mnt/c/Users/lenovo/Downloads'))
    args = ap.parse_args()
    sign = 1 if args.side == 'positive' else -1
    y = .6*sign
    gate, scene, buf = ReadyGate(y), Scene(), bytearray()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    out = args.output_dir / ('goalie-interception-' + args.side + '-' + time.strftime('%Y%m%d-%H%M%S') + '.rsmp.gz')
    placed = fired = False
    shot_t = None
    print('8秒后摆位；守门员在门前站稳后发球，不要求横向对齐。', flush=True)
    try:
        with socket.create_connection((args.host, args.mport), timeout=5) as sock, gzip.open(out, 'wb') as record:
            sock.settimeout(.2)
            start = time.monotonic()
            def send(text):
                data = text.encode()
                sock.sendall(len(data).to_bytes(4, 'big') + data)
            while True:
                elapsed = time.monotonic()-start
                if not placed and elapsed >= 8:
                    if scene.left_team != 'Apollo3Drelease':
                        raise RuntimeError('本工具只适用于Apollo3Drelease为左队的单人本地测试。')
                    send('(agent (unum 1) (team Apollo3Drelease) (move -27 0 0.7 0))'
                         '(dropBall)' + f'(ball (pos -23 {y} 0.11) (vel 0 0 0))')
                    placed = True
                    print('已摆位；正在等待实际站位稳定。', flush=True)
                if not fired and elapsed > 98:
                    print('准备超时，没有发球。请保留记录用于排查。', flush=True)
                    break
                if fired and time.monotonic()-fired_wall > 120:
                    print('观察达到现实时间上限，结束记录。', flush=True)
                    break
                try:
                    data = sock.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    raise RuntimeError('服务器监控连接中断')
                record.write(data)
                buf.extend(data)
                while len(buf) >= 4:
                    n = int.from_bytes(buf[:4], 'big')
                    if n > 10*1024*1024:
                        raise RuntimeError('异常监控消息长度')
                    if len(buf) < n+4:
                        break
                    frame = bytes(buf[4:n+4]);del buf[:n+4]
                    t, objects = scene.frame(frame)
                    if (scene.mode or '').replace('_', '').lower() == 'gameover':
                        raise RuntimeError('比赛已结束，请重新启动服务器后再测试。')
                    if placed and not fired and gate.update(t, objects, scene.mode):
                        send(f'(ball (pos -23 {y} 0.11) (vel -3.0 0 0))')
                        fired = True;shot_t = t;fired_wall = time.monotonic()
                        print(f'站位通过：仿真时间{t:.2f}，已发球。', flush=True)
                    if fired and t-shot_t >= 16:
                        print('已记录发球后16秒仿真过程。', flush=True)
                        return
    finally:
        print('记录文件：', out, flush=True)
        subprocess.run(['bash', str(Path.home()/'robocup/ApolloCodebase-fixed/kill.sh')], check=False)
    if not fired:
        raise SystemExit(2)


if __name__ == '__main__':
    main()
