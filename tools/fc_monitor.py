#!/usr/bin/env python3
"""Read-only browser dashboard for the F411's existing QD OLED UART frames."""
import argparse
import json
import re
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("Install pyserial: python3 -m pip install pyserial") from exc

FRAME_SIZE = 173
ROW_SIZE = 21
AXES = ("AX", "AY", "AZ", "GX", "GY", "GZ")
AXIS_RE = re.compile(r"\b([AG][XYZ]):\s*(-?\d+\.\d+)")
lock = threading.Lock()
state = {"last": 0.0, "frames": 0, "rows": [], "values": {}, "history": deque(maxlen=300), "error": "等待串口数据"}


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode(frame):
    if len(frame) != FRAME_SIZE or frame[:3] != b"QD\x01" or int.from_bytes(frame[-2:], "little") != crc16(frame[:-2]):
        return None
    text = frame[3:-2].decode("ascii", errors="replace")
    rows = [text[i:i + ROW_SIZE].rstrip() for i in range(0, len(text), ROW_SIZE)]
    values = {key: float(value) for row in rows for key, value in AXIS_RE.findall(row)}
    return (rows, values) if all(key in values for key in AXES) else None


def reader(port, baud):
    buf = bytearray()
    while True:
        try:
            with serial.Serial(port, baudrate=baud, timeout=.2) as ser:
                with lock:
                    state["error"] = ""
                while True:
                    buf.extend(ser.read(512))
                    while True:
                        start = buf.find(b"QD\x01")
                        if start < 0:
                            if len(buf) > 2:
                                del buf[:-2]
                            break
                        if start:
                            del buf[:start]
                        if len(buf) < FRAME_SIZE:
                            break
                        decoded = decode(bytes(buf[:FRAME_SIZE]))
                        if decoded is None:
                            del buf[0]
                            continue
                        del buf[:FRAME_SIZE]
                        rows, values = decoded
                        now = time.time()
                        with lock:
                            state.update(last=now, rows=rows, values=values, error="")
                            state["frames"] += 1
                            state["history"].append({"t": now, **values})
        except Exception as exc:
            with lock:
                state["error"] = str(exc)
            time.sleep(1)


PAGE = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>F411 传感器调试</title><style>
*{box-sizing:border-box}body{margin:0;background:#10151c;color:#e8eef5;font:15px system-ui,sans-serif}header{padding:16px 22px;background:#18222d;display:flex;justify-content:space-between}h1{font-size:20px;margin:0}.status{color:#f2be63}.ok{color:#64d995}main{max-width:1100px;margin:18px auto;padding:0 14px}.cards{display:grid;grid-template-columns:repeat(6,1fr);gap:9px}.card,.panel{background:#18222d;border:1px solid #293846;border-radius:9px;padding:13px}.key{color:#9db0c2;font-size:13px}.val{font:bold 24px ui-monospace,monospace;margin:5px 0}.unit,.meta{color:#9db0c2;font-size:12px}.charts{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:12px 0}.panel h2{font-size:15px;margin:0 0 6px}canvas{width:100%;height:210px}.help{line-height:1.7;color:#c4d0dc}@media(max-width:700px){.cards{grid-template-columns:repeat(3,1fr)}.charts{grid-template-columns:1fr}}
</style><body><header><h1>F411 / MPU6050 实时调试</h1><div id="status" class="status">等待数据</div></header><main>
<div class="cards" id="cards"></div><div class="charts"><section class="panel"><h2>加速度计（G）</h2><canvas id="acc"></canvas></section><section class="panel"><h2>陀螺仪（°/s）</h2><canvas id="gyro"></canvas></section></div>
<section class="panel help"><b>测试步骤</b><br>1. 拆下螺旋桨。四位页面拨码全断开，OLED 显示 MPU 页面。<br>2. 板子水平静止，记录 AX/AY/AZ；缓慢抬机头约 20°，观察加速度变化和动作过程中的陀螺仪符号。<br>3. 回水平后抬高机身右侧约 20°，再保持水平转动机头，分别观察读数。停住后陀螺仪应回到接近 0。<br>4. 安装约定：MPU X 轴负方向朝机头。动作停下后看加速度读数，转动时看陀螺仪读数。<br><br>有效帧数：<span id="frames">0</span>　当前页：<span id="page">--</span><br><span class="meta">USB-TTL 设 115200 8-N-1：适配器 RX 接 F411 PA2，GND 共地；TX 不接。仅监听，不向飞控发送数据。</span></section></main>
<script>
const keys=['AX','AY','AZ','GX','GY','GZ'],colors={AX:'#52a8ff',AY:'#ffbd4a',AZ:'#64d995',GX:'#db85ff',GY:'#ff758f',GZ:'#54d9cf'};const cards=document.querySelector('#cards');for(const k of keys)cards.insertAdjacentHTML('beforeend',`<div class="card"><div class="key">${k}</div><div class="val" id="v${k}">--</div><div class="unit">${k[0]==='A'?'G':'°/s'}</div></div>`);
function chart(id,series,hist){const c=document.getElementById(id),dpr=devicePixelRatio||1,r=c.getBoundingClientRect();c.width=r.width*dpr;c.height=r.height*dpr;const x=c.getContext('2d');x.scale(dpr,dpr);let w=r.width,h=r.height,p=25,lim=Math.max(...hist.flatMap(o=>series.map(k=>Math.abs(o[k]))),series[0][0]==='A'?1:100)*1.12;x.strokeStyle='#344453';x.fillStyle='#8da1b3';x.font='11px system-ui';for(let j=0;j<=4;j++){let y=p+(h-2*p)*j/4;x.beginPath();x.moveTo(p,y);x.lineTo(w-5,y);x.stroke();x.fillText((lim-2*lim*j/4).toFixed(series[0][0]==='A'?1:0),2,y+4)}for(const k of series){x.strokeStyle=colors[k];x.lineWidth=1.8;x.beginPath();hist.forEach((o,i)=>{let xx=p+(w-p-5)*i/Math.max(hist.length-1,1),yy=p+(h-2*p)*(lim-o[k])/(2*lim);i?x.lineTo(xx,yy):x.moveTo(xx,yy)});x.stroke()}}
async function tick(){try{const d=await(await fetch('/api')).json(),online=d.last&&Date.now()/1000-d.last<1.5,s=document.querySelector('#status');s.textContent=online?'串口已连接 · 实时数据':(d.error||'等待有效 MPU 页面帧');s.className='status '+(online?'ok':'');document.querySelector('#frames').textContent=d.frames;document.querySelector('#page').textContent=d.rows[0]||'--';for(const k of keys)document.querySelector('#v'+k).textContent=d.values[k]===undefined?'--':d.values[k].toFixed(k[0]==='A'?3:2);if(d.history.length){chart('acc',['AX','AY','AZ'],d.history);chart('gyro',['GX','GY','GZ'],d.history)}}catch(e){}}setInterval(tick,250);addEventListener('resize',tick);tick();</script></body></html>'''


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api":
            with lock:
                body = json.dumps({**state, "history": list(state["history"])}, ensure_ascii=False).encode()
            content_type = "application/json; charset=utf-8"
        elif self.path == "/":
            body = PAGE.encode()
            content_type = "text/html; charset=utf-8"
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="USB serial device, e.g. /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-port", type=int, default=8765)
    ap.add_argument("--list", action="store_true", help="list serial devices")
    args = ap.parse_args()
    if args.list:
        for item in list_ports.comports():
            print(f"{item.device}\t{item.description}")
        return
    if not args.port:
        ap.error("provide --port or use --list")
    threading.Thread(target=reader, args=(args.port, args.baud), daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", args.http_port), Handler)
    print(f"Open http://127.0.0.1:{args.http_port} | serial {args.port} @ {args.baud}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
