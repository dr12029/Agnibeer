import cv2
import urllib.request
import numpy as np
from ultralytics import YOLO
import sys
import threading
import time
import websocket
from flask import Flask, Response

# ==========================================
# 1. CONFIGURATION (AP MODE)
# ==========================================
ESP32_IP = "192.168.4.1"  

STREAM_URL = f"http://{ESP32_IP}:81/stream"
WS_URL = f"ws://{ESP32_IP}:82/"

app = Flask(__name__)

print("Loading Custom YOLO Model (code/best.pt)...")
model = YOLO("code/best.pt") 

# Read class mapping directly from your model to prevent ID mismatches
class_names = model.names
print(f"✅ Model loaded successfully. Detected Classes: {class_names}")

fire_active = False
human_active = False
ws_app = None

# Threading buffers to isolate network ingestion from AI processing
latest_raw_frame = None
frame_lock = threading.Lock()

# ==========================================
# 2. WEBSOCKET CONNECTION
# ==========================================
def on_open(ws):
    print("✅ Connected to ESP32 Command Center WebSocket!")

def run_websocket():
    global ws_app
    ws_app = websocket.WebSocketApp(WS_URL, on_open=on_open)
    ws_app.run_forever()

threading.Thread(target=run_websocket, daemon=True).start()

def send_alert(message):
    global ws_app
    if ws_app and ws_app.sock and ws_app.sock.connected:
        print(f"📡 Transmitting to Dashboard: {message}")
        ws_app.send(message)
    else:
        print(f"⚠️ WS Disconnected! Cannot send: {message}")

# ==========================================
# 3. HIGH-SPEED BG NETWORK THREAD
# Consumes frames at maximum Wi-Fi speed to prevent hardware lag
# ==========================================
def network_stream_fetcher():
    global latest_raw_frame
    bytes_buffer = b''
    while True:
        print(f"Opening HTTP Stream at {STREAM_URL}...")
        try:
            stream = urllib.request.urlopen(STREAM_URL, timeout=5)
            while True:
                chunk = stream.read(4096)
                if not chunk:
                    break
                bytes_buffer += chunk
                
                a = bytes_buffer.find(b'\xff\xd8')
                b = bytes_buffer.find(b'\xff\xd9')
                
                if a != -1 and b != -1:
                    if a < b:
                        jpg_data = bytes_buffer[a:b+2]
                        bytes_buffer = bytes_buffer[b+2:] 
                        
                        frame = cv2.imdecode(np.frombuffer(jpg_data, dtype=np.uint8), cv2.IMREAD_COLOR)
                        if frame is not None:
                            # Safely save to shared buffer without blocking
                            with frame_lock:
                                latest_raw_frame = frame
                    else:
                        bytes_buffer = bytes_buffer[a:]
        except Exception as e:
            print(f"Stream error/disconnected: {e}. Retrying in 2 seconds...")
            time.sleep(2)

# Launch the stream fetcher immediately as a background service
threading.Thread(target=network_stream_fetcher, daemon=True).start()

# ==========================================
# 4. ROBUST AI GENERATOR LOOP
# Runs at the pace of your CPU/GPU, completely detached from network speed
# ==========================================
def generate_frames():
    global fire_active, human_active, latest_raw_frame
    
    while True:
        # Check if a frame is available from the background network thread
        with frame_lock:
            if latest_raw_frame is None:
                frame = None
            else:
                frame = latest_raw_frame.copy()
        
        if frame is None:
            time.sleep(0.01) # Sleep briefly to prevent high CPU utilization while waiting
            continue

        # Run Custom YOLO
        results = model(frame, stream=True, verbose=False)
        
        current_fire = False
        current_human = False
        
        for r in results:
            for box in r.boxes:
                x1, y1, x2, y2 = map(int, box.xyxy[0])
                cls_id = int(box.cls[0])
                conf = float(box.conf[0])

                if conf > 0.45: # 45% Confidence threshold
                    # Robust Name-String Matching instead of relying on Class IDs
                    class_label = class_names[cls_id].lower()

                    if "fire" in class_label or "flame" in class_label:  
                        current_fire = True
                        label = f"FIRE! {conf:.2f}"
                        color = (0, 0, 255) 
                    elif "person" in class_label or "human" in class_label: 
                        current_human = True
                        label = f"Person {conf:.2f}"
                        color = (0, 255, 0) 
                    else:
                        label = f"{class_names[cls_id]} {conf:.2f}"
                        color = (255, 255, 0)

                    cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
                    cv2.putText(frame, label, (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

        # Trigger Alerts over WebSocket to ESP32
        if current_fire and not fire_active:
            send_alert("ALERT:FIRE")
            fire_active = True
        elif not current_fire and fire_active:
            send_alert("CLEAR:FIRE")
            fire_active = False

        if current_human and not human_active:
            send_alert("ALERT:HUMAN")
            human_active = True
        elif not current_human and human_active:
            send_alert("CLEAR:HUMAN")
            human_active = False

        # Convert annotated frame back to JPEG bytes to send to Dashboard
        ret, buffer = cv2.imencode('.jpg', frame)
        frame_bytes = buffer.tobytes()
        
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')
               
        # Give the CPU breathing room (~30 FPS max tracking speed)
        time.sleep(0.03)

# ==========================================
# 5. LOCAL FLASK SERVER
# ==========================================
@app.route('/video_feed')
def video_feed():
    # This serves the annotated video to your HTML Dashboard
    return Response(generate_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    print("🚀 Starting AI Video Relay on Port 5000...")
    import logging
    log = logging.getLogger('werkzeug')
    log.setLevel(logging.ERROR)
    
    app.run(host='0.0.0.0', port=5000, threaded=True)