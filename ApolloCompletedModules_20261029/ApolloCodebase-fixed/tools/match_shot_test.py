#!/usr/bin/env python3
"""Local seven-player PlayOn empty-goal check, not a full opponent match."""
from pathlib import Path
import socket,time,gzip
from empty_goal_shot_test import Scene

def main():
    downloads=Path('/mnt/c/Users/lenovo/Downloads')
    output=downloads if downloads.is_dir() else Path(__file__).resolve().parents[1]/'logs'
    output.mkdir(parents=True,exist_ok=True)
    path=output/('match-shot-'+time.strftime('%Y%m%d-%H%M%S')+'.rsmp.gz')
    scene=Scene();buf=bytearray();placed=False;placed_t=None;goal_t=None;fall_t=None;min_z=float('inf')
    print('8秒后布置己方7人门前射门测试，请切到球场；不需要启动对手。',flush=True)
    try:
        with socket.create_connection(('127.0.0.1',60001),timeout=5) as sock,gzip.open(path,'wb') as record:
            sock.settimeout(.2);start=time.monotonic()
            while time.monotonic()-start<150:
                try:data=sock.recv(65536)
                except socket.timeout:continue
                if not data:raise RuntimeError('服务器监控连接中断')
                record.write(data);buf.extend(data)
                while len(buf)>=4:
                    n=int.from_bytes(buf[:4],'big')
                    if n>10*1024*1024:raise RuntimeError('监控消息长度异常')
                    if len(buf)<n+4:break
                    frame=bytes(buf[4:n+4]);del buf[:n+4]
                    t,objects=scene.frame(frame)
                    if (scene.mode or '').replace('_','').lower()=='gameover':raise RuntimeError('比赛已结束，请重启球场服务器。')
                    if not placed and time.monotonic()-start>=8:
                        if scene.left_team!='Apollo3Drelease':raise RuntimeError('需要Apollo3Drelease为左队，请用新服务器只启动己方。')
                        if any('agent:Apollo3Drelease:'+str(i) not in objects for i in range(1,8)):raise RuntimeError('己方7人尚未全部出现，请检查启动日志。')
                        if any(k.startswith('agent:') and not k.startswith('agent:Apollo3Drelease:') for k in objects):raise RuntimeError('本轮为空门集成测试，请先停止对手。')
                        poses=[(-27,0),(-15,-10),(-15,10),(-8,-12),(-8,12),(0,-10),(19.86,.08)]
                        text=''.join(f'(agent (unum {i}) (team Apollo3Drelease) (move {x} {y} .7 0))' for i,(x,y) in enumerate(poses,1))
                        text+='(dropBall)(ball (pos 20.18 0 .11) (vel 0 0 0))'
                        msg=text.encode();sock.sendall(len(msg).to_bytes(4,'big')+msg)
                        placed=True;placed_t=t;print('已摆位，观察7号是否对位并射门。',flush=True)
                    if not placed:continue
                    robot=objects.get('agent:Apollo3Drelease:7')
                    if robot:
                        min_z=min(min_z,robot[0][2])
                        if robot[0][2]<.30 and fall_t is None:fall_t=t
                    if scene.mode=='Goal_Left' and goal_t is None:
                        goal_t=t;print('服务器判定己方进球，再观察2秒。',flush=True)
                    if (goal_t is not None and t-goal_t>=2) or (fall_t is not None and t-fall_t>=2) or t-placed_t>=50:
                        print(f'记录结束：进球={goal_t is not None}，7号最低高度={min_z:.3f}米。',flush=True)
                        print('队伍仍在运行；需要停止时执行kill.sh。',flush=True);return
            print('达到现实时间上限，停止记录。',flush=True)
    finally:print('记录文件：',path,flush=True)

if __name__=='__main__':
    main()
