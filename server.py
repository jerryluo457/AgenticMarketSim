import zmq
import zmq.asyncio
import asyncio
from fastapi import FastAPI, Request
from fastapi.templating import Jinja2Templates
from fastapi.responses import HTMLResponse
import socketio
import subprocess
import signal
import os
import sys

BINARIES = {
    "moderate": "./limit_order_book_moderate",
    "volatile": "./limit_order_book_volatile",
    "very_volatile": "./limit_order_book_very_volatile",
    "most_volatile": "./limit_order_book_most_volatile"
}

app = FastAPI()
sio = socketio.AsyncServer(async_mode='asgi', cors_allowed_origins='*')
socket_app = socketio.ASGIApp(sio, app)
templates = Jinja2Templates(directory="templates")

current_process = None
zmq_ctx = zmq.asyncio.Context()
command_pub = None 

async def zmq_data_listener():
    print("🎧 ZMQ Data Listener Active...")
    sub_sock = zmq_ctx.socket(zmq.SUB)
    sub_sock.connect("tcp://127.0.0.1:5555")
    sub_sock.subscribe("") 

    while True:
        try:
            msg = await sub_sock.recv_string()
            parts = msg.split(" ")
            
            if parts[0] == "DATA":
                await sio.emit('market_data', {'price': float(parts[1]), 'volume': int(parts[2])})
            elif parts[0] == "TRADE":
                await sio.emit('trade_log', {'agent': parts[1], 'side': parts[2], 'qty': int(parts[3]), 'price': float(parts[4])})
            elif parts[0] == "SENTIMENT":
                data = [int(x) for x in parts[1:]]
                await sio.emit('server_sentiment', data)
            elif parts[0] == "SCENARIO_METRICS":
                await sio.emit('scenario_metrics', {
                    'hype': float(parts[1]),
                    'bubble': float(parts[2]),
                    'short_interest': int(parts[3]),
                    'panic': float(parts[4])
                })
            # ADDED: Handler for General Metrics
            elif parts[0] == "METRICS":
                await sio.emit('market_metrics', {'spread': float(parts[1]), 'liquidity': int(parts[2])})
                
        except asyncio.CancelledError:
            break
        except Exception as e:
            await asyncio.sleep(0.1)

async def async_send_command(cmd_string):
    global command_pub
    if command_pub:
        await command_pub.send_string(cmd_string)

@app.get("/", response_class=HTMLResponse)
async def index(request: Request):
    return templates.TemplateResponse("index.html", {"request": request})

@app.on_event("startup")
async def startup_event():
    asyncio.create_task(zmq_data_listener())
    global command_pub
    command_pub = zmq_ctx.socket(zmq.PUB)
    command_pub.connect("tcp://127.0.0.1:5556") 

@app.on_event("shutdown")
def shutdown_event():
    kill_current_simulation()

def kill_current_simulation():
    global current_process
    if current_process:
        print("🛑 Killing active simulation...")
        try:
            os.kill(current_process.pid, signal.SIGTERM)
            current_process.wait()
        except Exception:
            pass
        current_process = None

@sio.on('start_simulation')
async def start_simulation(sid, data):
    global current_process
    kill_current_simulation()
    
    mode = data.get('mode', 'moderate')
    binary_path = BINARIES.get(mode)
    
    if not binary_path or not os.path.exists(binary_path):
        await sio.emit('error', {'message': f"Binary {binary_path} not found."}, room=sid)
        return

    print(f"🚀 Launching {mode}...")
    try:
        current_process = subprocess.Popen([binary_path], stdout=sys.stdout, stderr=sys.stderr)
        await asyncio.sleep(0.5)
        config_str = f"START {data['makers']} {data['fundamental']} {data['momentum']} {data['noise']}"
        await async_send_command(config_str)
    except Exception as e:
        await sio.emit('error', {'message': str(e)}, room=sid)

@sio.on('pause_simulation')
async def pause_simulation(sid): await async_send_command("PAUSE")

@sio.on('resume_simulation')
async def resume_simulation(sid): await async_send_command("RESUME")

@sio.on('stop_simulation')
async def stop_simulation(sid):
    await async_send_command("STOP")
    await asyncio.sleep(0.1)
    kill_current_simulation()

@sio.on('place_order')
async def place_order(sid, data):
    side_int = 0 if data['side'] == 'buy' else 1
    cmd = f"ORDER {side_int} {int(data['quantity'])} {float(data['price'])}"
    await async_send_command(cmd)

@sio.on('set_scenario')
async def set_scenario(sid, data):
    scenario_map = {'normal': 0, 'pump': 1, 'squeeze': 2}
    val = scenario_map.get(data.get('type', 'normal'), 0)
    await async_send_command(f"SCENARIO {val}")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(socket_app, host="0.0.0.0", port=8000)