import cv2
import requests
import numpy as np
from ultralytics import YOLO
import sys
import threading
import time
import websocket
from flask import Flask, Response

# ==========================================
# 1. CONFIGURATION
# ==========================================
ESP32_IP = "192.168.0.203"  

# Stream from Port 80
STREAM_URL = f"http://{ESP32_IP}:200/stream"
# WebSocket to Port 81
WS_URL = f"ws://{ESP32_IP}:81/"

app = Flask(__name__)

# FIXED PATH: Points to the model inside the 'code' folder
print("Loading Custom YOLO Model (code/best.pt)...")
model = YOLO("code/best.pt") 

fire_active = False
human_active = False
ws_app = None

# ==========================================
# 2. WEBSOCKET CONNECTION
# ==========================================
def on_open(ws):
    print("✅ Connected to ESP32 Command Center WebSocket!")

def run_websocket():
    global ws_app
    ws_app = websocket.WebSocketApp(WS_URL, on_open=on_open)
    ws_app.run_forever()

# Start WebSocket in the background so it doesn't block the video
threading.Thread(target=run_websocket, daemon=True).start()

def send_alert(message):
    if ws_app and ws_app.sock and ws_app.sock.connected:
        ws_app.send(message)

# ==========================================
# 3. ROBUST STREAMING & AI RELAY
# ==========================================
def generate_frames():
    global fire_active, human_active
    
    while True:
        print(f"Opening HTTP Stream at {STREAM_URL}...")
        try:
            response = requests.get(STREAM_URL, stream=True, timeout=5)
            response.raise_for_status()
            byte_buffer = bytes()

            for chunk in response.iter_content(chunk_size=4096):
                byte_buffer += chunk
                
                # Search for JPEG Start and End markers
                a = byte_buffer.find(b'\xff\xd8')
                b = byte_buffer.find(b'\xff\xd9')
                
                if a != -1 and b != -1:
                    if a < b:
                        jpg_data = byte_buffer[a:b+2]
                        byte_buffer = byte_buffer[b+2:] 
                        
                        frame = cv2.imdecode(np.frombuffer(jpg_data, dtype=np.uint8), cv2.IMREAD_COLOR)
                        
                        if frame is not None:
                            # Run Custom YOLO
                            results = model(frame, stream=True, verbose=False)
                            
                            current_fire = False
                            current_human = False
                            
                            # Your custom bounding box drawing logic
                            for r in results:
                                for box in r.boxes:
                                    x1, y1, x2, y2 = map(int, box.xyxy[0])
                                    cls_id = int(box.cls[0])
                                    conf = float(box.conf[0])

                                    if conf > 0.50: # 50% Confidence threshold
                                        if cls_id == 0:  
                                            current_fire = True
                                            label = f"FIRE! {conf:.2f}"
                                            color = (0, 0, 255) 
                                        elif cls_id == 1: 
                                            current_human = True
                                            label = f"Person {conf:.2f}"
                                            color = (0, 255, 0) 
                                        else:
                                            label = f"Unknown {conf:.2f}"
                                            color = (255, 255, 0)

                                        cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
                                        cv2.putText(frame, label, (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

                            # Trigger Alerts over WebSocket to ESP32
                            if current_fire and not fire_active:
                                send_alert("CMD:ML_FIRE")
                                fire_active = True
                            elif not current_fire and fire_active:
                                send_alert("CMD:ML_FIRE_CLEAR")
                                fire_active = False

                            if current_human and not human_active:
                                send_alert("CMD:ML_HUMAN")
                                human_active = True
                            elif not current_human and human_active:
                                send_alert("CMD:ML_HUMAN_CLEAR")
                                human_active = False

                            # Convert annotated frame back to JPEG bytes to send to Dashboard
                            ret, buffer = cv2.imencode('.jpg', frame)
                            frame_bytes = buffer.tobytes()
                            
                            yield (b'--frame\r\n'
                                   b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')
                    else:
                        byte_buffer = byte_buffer[a:]

        except Exception as e:
            print(f"Stream error/disconnected: {e}. Retrying in 2 seconds...")
            time.sleep(2)

# ==========================================
# 4. LOCAL FLASK SERVER
# ==========================================
@app.route('/video_feed')
def video_feed():
    # This serves the annotated video to your HTML Dashboard
    return Response(generate_frames(), mimetype='multipart/x-mixed-replace; boundary=frame')

if __name__ == '__main__':
    print("🚀 Starting AI Video Relay on Port 5000...")
    print("Go to your ESP32 IP in the browser to view the Dashboard!")
    # Disable standard logging to keep terminal clean
    import logging
    log = logging.getLogger('werkzeug')
    log.setLevel(logging.ERROR)
    
    app.run(host='0.0.0.0', port=5000, threaded=True)