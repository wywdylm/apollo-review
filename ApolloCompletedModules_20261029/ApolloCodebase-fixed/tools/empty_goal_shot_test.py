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


def main():
    # Local single-player PlayOn empty-goal check, not a formal penalty referee.
    root = Path(__file__).resolve().parents[1]
    downloads = Path('/mnt/c/Users/lenovo/Downloads')
    output = downloads if downloads.is_dir() else root / 'logs'
    output.mkdir(parents=True, exist_ok=True)
    record_path = output / ('empty-goal-feedback-' + time.strftime('%Y%m%d-%H%M%S') + '.rsmp.gz')
    scene, buf = Scene(), bytearray()
    placed = False
    placed_t = None
    goal_t = None
    fall_t = None
    max_x = -float('inf')
    min_z = float('inf')
    print('8秒后摆位并开球，请切到球场。本工具只测左队单人空门。', flush=True)
    try:
        with socket.create_connection(('127.0.0.1', 60001), timeout=5) as sock, gzip.open(record_path, 'wb') as record:
            sock.settimeout(.2)
            start = time.monotonic()
            while time.monotonic()-start < 150:
                if not placed and time.monotonic()-start >= 8:
                    if scene.left_team != 'Apollo3Drelease':
                        raise RuntimeError('需要Apollo3Drelease为左队；请重启服务器后只启动罚球队员。')
                    text = '(agent (unum 7) (team Apollo3Drelease) (move 19.86 0.08 0.7 0))(dropBall)(ball (pos 20.18 0 0.11) (vel 0 0 0))'
                    data = text.encode()
                    sock.sendall(len(data).to_bytes(4,'big')+data)
                    placed = True
                    print('已摆位，正在记录。', flush=True)
                try:
                    data = sock.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    raise RuntimeError('服务器连接中断')
                record.write(data)
                buf.extend(data)
                while len(buf)>=4:
                    n = int.from_bytes(buf[:4],'big')
                    if n>10*1024*1024:
                        raise RuntimeError('监控消息长度异常')
                    if len(buf)<n+4:
                        break
                    frame=bytes(buf[4:n+4]);del buf[:n+4]
                    t, objects=scene.frame(frame)
                    if (scene.mode or '').replace('_','').lower()=='gameover':
                        raise RuntimeError('比赛已结束，请重启球场服务器。')
                    if not placed:
                        continue
                    if placed_t is None:
                        placed_t=t
                    robot=objects.get('agent:Apollo3Drelease:7')
                    ball=objects.get('ball')
                    if ball:max_x=max(max_x,ball[0][0])
                    if robot:
                        min_z=min(min_z,robot[0][2])
                        if robot[0][2]<.30 and fall_t is None:fall_t=t
                    if scene.mode in ('Goal_Left','Goal_Right') and goal_t is None:
                        goal_t=t
                        print('服务器已判定进球，继续记录2秒观察站立。',flush=True)
                    if (goal_t is not None and t-goal_t>=2) or (fall_t is not None and t-fall_t>=2):
                        print(f'本轮记录结束：球最远x={max_x:.3f}，机器人最低高度={min_z:.3f}',flush=True)
                        print('队员仍在运行，如需停止请执行kill.sh。',flush=True)
                        return
                    if t-placed_t>=50:
                        print(f'观察结束：球最远x={max_x:.3f}，机器人最低高度={min_z:.3f}',flush=True)
                        return
            print('达到现实时间上限，结束记录。',flush=True)
    finally:
        print('记录文件：', record_path, flush=True)
    # Deliberately leave the player running; stop it with the normal kill.sh.

if __name__ == '__main__':
    main()
