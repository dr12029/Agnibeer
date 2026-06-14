#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

// ===========================
// 1. ACCESS POINT CONFIG
// ===========================
const char *ap_ssid = "Agnibeer_Rover";
const char *ap_password = "password123";

// ===========================
// 2. HARDWARE PINS
// ===========================
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

// Sonar Pins
#define TRIG1 41
#define ECHO1 42
#define TRIG2 38
#define ECHO2 47

// Servo Pin
#define SERVO_PIN 40

// ===========================
// Global Objects & States
// ===========================
httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;
WebSocketsServer webSocket = WebSocketsServer(82);
Servo waterServo;

unsigned long lastPingTime = 0;
const int pingInterval = 100;

bool leftObstacle = false;
bool rightObstacle = false;

// Dynamic Speed Variables
int manualSpeed = 120;
int autoSpeed = 175;

bool isDrivingForward = false;

// Autonomous State Tracking
bool isAutoMode = false;
bool hardwareFireActive = false;
bool mlFireActive = false;

String arduinoBuffer = "";

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ===========================
// 3. HTML DASHBOARD
// ===========================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Agnibeer Interface</title>
    <link href="https://fonts.googleapis.com/css2?family=Bebas+Neue&family=Roboto:wght@400;700&display=swap" rel="stylesheet">
    <style>
        :root { --bg-color: #0a0a0a; --panel-bg: #141414; --text-main: #ffffff; --text-muted: #888888; --accent: #007bff; --danger: #d32f2f; --success: #388e3c; --warning: #fbc02d; --border: #333333; }
        body { background-color: var(--bg-color); color: var(--text-main); font-family: 'Roboto', sans-serif; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
        
        h1, h2, h3, .bebas-font { font-family: 'Bebas Neue', sans-serif; letter-spacing: 1px; margin-top: 0; }
        
        .header { width: 100%; max-width: 1400px; display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; border-bottom: 2px solid var(--border); padding-bottom: 10px; }
        .header h1 { font-size: 3rem; color: #e0e0e0; margin-bottom: 0; display: flex; align-items: center; gap: 20px; }
        
        .status-indicator { display: flex; align-items: center; gap: 10px; font-size: 1.2rem; font-family: 'Bebas Neue', sans-serif; letter-spacing: 1px;}
        .dot { width: 14px; height: 14px; background-color: var(--danger); border-radius: 50%; box-shadow: 0 0 10px var(--danger); }
        .dot.connected { background-color: var(--success); box-shadow: 0 0 10px var(--success); }
        
        .audio-badge { font-size: 1rem; padding: 5px 10px; border-radius: 4px; background: #333; color: #aaa; border: 1px solid #555; }
        .audio-badge.active { background: rgba(56, 142, 60, 0.2); color: var(--success); border-color: var(--success); }
        .audio-badge.alarming { background: var(--danger); color: white; animation: blink 0.3s infinite; }

        .dashboard-grid { display: grid; grid-template-columns: 1fr 1.5fr 1fr; gap: 20px; max-width: 1400px; width: 100%; }
        .column { display: flex; flex-direction: column; gap: 20px; }
        .panel { background-color: var(--panel-bg); border-radius: 4px; padding: 15px; border: 1px solid var(--border); box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        .panel h2 { font-size: 1.8rem; color: var(--text-muted); border-bottom: 1px solid var(--border); padding-bottom: 5px; margin-bottom: 15px; }
        
        .video-container { width: 100%; aspect-ratio: 4/3; background-color: #000; border: 1px solid #444; border-radius: 4px; overflow: hidden; display: flex; justify-content: center; align-items: center; }
        #camera-stream { width: 100%; height: 100%; object-fit: cover; }
        .key-map { display: grid; grid-template-columns: auto 1fr; gap: 10px; font-family: monospace; color: #ccc; font-size: 0.95rem; }
        .key-map span { font-weight: bold; color: var(--accent); }
        
        .top-status-row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 15px; }
        .status-box { padding: 15px; border-radius: 4px; border: 2px solid var(--border); font-family: 'Bebas Neue'; font-size: 1.5rem; text-align: center; letter-spacing: 2px; transition: 0.3s; cursor: pointer; user-select: none; }
        .status-box .label { font-size: 0.9rem; color: #888; font-family: 'Roboto', sans-serif; display: block; letter-spacing: normal; margin-bottom: 5px; }
        
        .manual-mode { background-color: rgba(56, 142, 60, 0.15); border-color: var(--success); color: var(--success); }
        .auto-mode { background-color: rgba(0, 123, 255, 0.15); border-color: var(--accent); color: var(--accent); }
        .pump-off { background-color: #1a1a1a; color: #555; }
        .pump-on { background-color: var(--danger); color: white; border-color: #ff5252; box-shadow: 0 0 15px rgba(211, 47, 47, 0.5); }

        .telemetry-core { display: flex; justify-content: center; margin-bottom: 15px; }
        
        .car-container { display: flex; align-items: center; justify-content: center; gap: 20px; width: 100%; background: #0f0f0f; border: 1px solid var(--border); padding: 15px; border-radius: 4px; }
        .car-body { width: 70px; height: 140px; background: #222; border: 2px solid #555; border-radius: 20px; display: flex; align-items: center; justify-content: center; font-family: 'Bebas Neue'; color: #666; font-size: 1.8rem; letter-spacing: 2px; writing-mode: vertical-rl; text-orientation: upright;}
        .sonar-block { font-family: 'Bebas Neue'; font-size: 1.4rem; color: var(--success); padding: 10px 5px; border-radius: 4px; background: rgba(56, 142, 60, 0.1); border: 1px solid var(--success); text-align: center; width: 60px;}
        .sonar-block.alert { color: var(--danger); background: rgba(211, 47, 47, 0.1); border-color: var(--danger); box-shadow: 0 0 10px rgba(211, 47, 47, 0.5); }
        
        .flame-array { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; margin-bottom: 15px; }
        .flame-box { background: #1a1a1a; border: 1px solid var(--border); padding: 10px 0; text-align: center; border-radius: 4px; font-family: 'Bebas Neue', sans-serif; font-size: 1.2rem; color: var(--success); transition: 0.2s;}
        .flame-box.alert-bg { background-color: var(--danger); color: white; border-color: #ff5252; font-weight: bold; animation: blink 0.5s infinite; }
        .flame-label { font-size: 0.8rem; color: #888; display: block; margin-bottom: 2px; font-family: 'Roboto', sans-serif; }

        .gas-bar { background-color: #1a1a1a; padding: 15px; border-radius: 4px; border: 1px solid var(--border); font-family: 'Bebas Neue', sans-serif; font-size: 1.5rem; display: flex; justify-content: space-between; align-items: center;}
        
        input[type=range] { -webkit-appearance: none; width: 100%; background: transparent; margin-top: 10px;}
        input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; height: 18px; width: 18px; border-radius: 50%; background: var(--accent); cursor: pointer; margin-top: -7px; }
        input[type=range]::-webkit-slider-runnable-track { width: 100%; height: 4px; cursor: pointer; background: #444; border-radius: 2px; }

        .log-container { height: 500px; overflow-y: auto; background-color: #0c0c0c; padding: 10px; border-radius: 4px; font-family: monospace; font-size: 0.85rem; display: flex; flex-direction: column; gap: 5px; border: 1px solid var(--border); }
        .log-entry { border-bottom: 1px solid #1a1a1a; padding-bottom: 5px; color: var(--text-muted); }
        .log-entry.alert { color: #ff5252; font-weight: bold; }
        .log-time { color: var(--accent); margin-right: 8px; }
        
        @keyframes blink { 50% { opacity: 0.5; } }
    </style>
</head>
<body>
    <div class="header">
        <h1>AGNIBEER <span id="audio-badge" class="audio-badge">AUDIO: MUTE (PRESS KEY)</span></h1>
        <div class="status-indicator">CONNECTION STATUS <div class="dot" id="ws-dot"></div></div>
    </div>
    
    <div class="dashboard-grid">
        <div class="column">
            <div class="panel">
                <h2>LIVE FEED</h2>
                <div class="video-container"><img id="camera-stream" alt="Waiting for stream..." src="http://127.0.0.1:5000/video_feed"></div>
            </div>
            <div class="panel">
                <h2>NAVIGATION & CONTROLS</h2>
                <div class="key-map">
                    <span>[M]</span> <div>Toggle Auto/Manual Mode</div>
                    <span>[W, A, S, D]</span> <div>Drive Rover (Manual Only)</div>
                    <span>[P]</span> <div>Toggle Submersible Pump</div>
                    <span>[K] / [L]</span> <div>Pan Cannon Left / Right</div>
                </div>
                
                <div style="margin-top: 20px;">
                    <span style="font-family: 'Bebas Neue'; font-size: 1.2rem; color: var(--accent); letter-spacing: 1px;">CANNON AIM:</span>
                    <input type="range" id="servo-slider" min="0" max="180" value="90" oninput="updateServo(this.value)">
                </div>
                
                <div style="margin-top: 20px;">
                    <span style="font-family: 'Bebas Neue'; font-size: 1.2rem; color: var(--accent); letter-spacing: 1px;">SPEED CONTROLS (0-255):</span>
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 5px;">
                        <div style="background: #1a1a1a; padding: 10px; border: 1px solid var(--border); border-radius: 4px; text-align: center;">
                            <span style="font-size: 0.9rem; color: #888;">MANUAL SPEED</span>
                            <input type="number" id="spd-m" min="0" max="255" value="120" onchange="updateSpeedM()" style="width: 80%; background: #333; color: white; border: none; padding: 5px; margin-top: 5px; text-align: center; font-family: monospace; border-radius: 3px;">
                        </div>
                        <div style="background: #1a1a1a; padding: 10px; border: 1px solid var(--border); border-radius: 4px; text-align: center;">
                            <span style="font-size: 0.9rem; color: #888;">AUTO SPEED</span>
                            <input type="number" id="spd-a" min="0" max="255" value="175" onchange="updateSpeedA()" style="width: 80%; background: #333; color: white; border: none; padding: 5px; margin-top: 5px; text-align: center; font-family: monospace; border-radius: 3px;">
                        </div>
                    </div>
                </div>
                
            </div>
        </div>

        <div class="column">
            <div class="panel">
                <h2>SAFETY & TELEMETRY</h2>
                
                <div class="top-status-row">
                    <div id="mode-ui" class="status-box manual-mode" onclick="toggleMode()">
                        <span class="label">DRIVE MODE</span>
                        <span id="mode-text">MANUAL</span>
                    </div>
                    <div id="pump-ui" class="status-box pump-off">
                        <span class="label">WATER PUMP</span>
                        <span id="pump-text">OFF</span>
                    </div>
                </div>

                <div class="telemetry-core">
                    <div class="car-container">
                        <div id="sonar-l-val" class="sonar-block">WAIT</div>
                        <div class="car-body">CAR</div>
                        <div id="sonar-r-val" class="sonar-block">WAIT</div>
                    </div>
                </div>

                <div class="flame-array">
                    <div id="flame-l" class="flame-box"><span class="flame-label">LEFT FLAME</span>CLEAR</div>
                    <div id="flame-f" class="flame-box"><span class="flame-label">FRONT FLAME</span>CLEAR</div>
                    <div id="flame-r" class="flame-box"><span class="flame-label">RIGHT FLAME</span>CLEAR</div>
                </div>
                
                <div class="gas-bar">
                    <span style="color: #888;">GAS SENSOR (MQ-2):</span> 
                    <span id="gas-val" style="color: var(--success);">--</span>
                </div>
            </div>
        </div>

        <div class="column">
            <div class="panel" style="height: 100%; display: flex; flex-direction: column;">
                <h2>SYSTEM LOG</h2>
                <div class="log-container" id="log-box"></div>
            </div>
        </div>
    </div>

    <script>
        let ws; 
        const gateway = `ws://${window.location.hostname}:82/`;
        let activeKey = null;
        let pumpActive = false; 
        let servoInterval = null;
        let servoDirection = 0; 
        let currentAngle = 90;
        
        let currentMode = 'MANUAL'; 
        
        const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        let alarmInterval = null;
        let isAlarming = false;
        let audioUnlocked = false;
        
        let alarms = { fire: false, gas: false, human: false };

        function unlockAudio() {
            if (!audioUnlocked) {
                audioCtx.resume().then(() => {
                    audioUnlocked = true;
                    const badge = document.getElementById('audio-badge');
                    badge.innerText = "AUDIO: ACTIVE";
                    badge.classList.add('active');
                });
            }
        }

        function triggerAlarmSound() {
            if (isAlarming || !audioUnlocked) return;
            isAlarming = true;
            document.getElementById('audio-badge').classList.add('alarming');
            document.getElementById('audio-badge').innerText = "MASTER ALARM";
            
            let highPitch = true;
            alarmInterval = setInterval(() => {
                const osc = audioCtx.createOscillator();
                const gain = audioCtx.createGain();
                osc.connect(gain);
                gain.connect(audioCtx.destination);
                
                osc.frequency.value = highPitch ? 600 : 450;
                osc.type = 'square';
                gain.gain.value = 0.1; 
                
                osc.start();
                gain.gain.exponentialRampToValueAtTime(0.00001, audioCtx.currentTime + 0.15);
                osc.stop(audioCtx.currentTime + 0.15);
                
                highPitch = !highPitch;
            }, 180);
        }

        function stopAlarmSound() {
            if (!isAlarming) return;
            clearInterval(alarmInterval);
            isAlarming = false;
            document.getElementById('audio-badge').classList.remove('alarming');
            document.getElementById('audio-badge').innerText = "AUDIO: ACTIVE";
        }

        function triggerErrorBuzzer() {
            if (!audioUnlocked) return;
            const osc = audioCtx.createOscillator();
            const gain = audioCtx.createGain();
            osc.connect(gain);
            gain.connect(audioCtx.destination);
            
            osc.frequency.value = 120; 
            osc.type = 'sawtooth';
            gain.gain.setValueAtTime(0.1, audioCtx.currentTime);
            
            osc.start();
            gain.gain.exponentialRampToValueAtTime(0.00001, audioCtx.currentTime + 0.3);
            osc.stop(audioCtx.currentTime + 0.3);
        }

        function evaluateAlarms() {
            if (alarms.fire || alarms.gas || alarms.human) { triggerAlarmSound(); } 
            else { stopAlarmSound(); }
        }

        window.onload = () => {
            addLog("System initialized. Waiting for WebSocket connection...");
            initWebSocket();
        };

        document.body.addEventListener('click', unlockAudio);

        function addLog(message, isAlert = false) {
            const logBox = document.getElementById('log-box');
            const time = new Date().toLocaleTimeString();
            const entry = document.createElement('div');
            entry.className = 'log-entry' + (isAlert ? ' alert' : '');
            entry.innerHTML = `<span class="log-time">[${time}]</span> ${message}`;
            logBox.prepend(entry);
        }

        function initWebSocket() {
            ws = new WebSocket(gateway);
            ws.onopen = () => { document.getElementById('ws-dot').classList.add('connected'); addLog("WebSocket connected.", false); };
            ws.onclose = () => { document.getElementById('ws-dot').classList.remove('connected'); addLog("Connection lost. Retrying...", true); setTimeout(initWebSocket, 2000); };
            ws.onmessage = (event) => { parseIncomingData(event.data); };
        }

        function sendCmd(cmd) { if (ws && ws.readyState === WebSocket.OPEN) { ws.send(cmd + "\n"); } }

        // THE NEW DYNAMIC SPEED FUNCTIONS
        function updateSpeedM() {
            let val = parseInt(document.getElementById('spd-m').value);
            if(val < 0) val = 0; if(val > 255) val = 255;
            document.getElementById('spd-m').value = val;
            sendCmd("SPD_M:" + val);
            addLog("SYSTEM: Manual Speed set to " + val);
        }

        function updateSpeedA() {
            let val = parseInt(document.getElementById('spd-a').value);
            if(val < 0) val = 0; if(val > 255) val = 255;
            document.getElementById('spd-a').value = val;
            sendCmd("SPD_A:" + val);
            addLog("SYSTEM: Auto Speed set to " + val);
        }

        function toggleMode() {
            const ui = document.getElementById('mode-ui');
            const text = document.getElementById('mode-text');
            if(currentMode === 'MANUAL') {
                currentMode = 'AUTO';
                text.innerText = "AUTO";
                ui.className = "status-box auto-mode";
                sendCmd("MODE:AUTO"); 
                addLog("SYSTEM: Switched to AUTO Mode. Autonomous navigation engaged.");
            } else {
                currentMode = 'MANUAL';
                text.innerText = "MANUAL";
                ui.className = "status-box manual-mode";
                sendCmd("MODE:MANUAL"); 
                addLog("SYSTEM: Switched to MANUAL Mode. Controls unlocked.");
            }
        }

        document.addEventListener('keydown', (e) => {
            unlockAudio(); 
            const key = e.key.toLowerCase();
            
            if (key === 'm') { 
                if(activeKey !== key) { toggleMode(); activeKey = key; } 
                return; 
            }
            
            if (key === 'p') { 
                if (!pumpActive && currentMode === 'MANUAL') { 
                    pumpActive = true; 
                    sendCmd("P:ON"); 
                    document.getElementById('pump-ui').className = "status-box pump-on";
                    document.getElementById('pump-text').innerText = "ON";
                } 
                return; 
            } 
            
            if (key === 'k' && servoDirection !== -1) { servoDirection = -1; startServoMove(); return; }
            if (key === 'l' && servoDirection !== 1) { servoDirection = 1; startServoMove(); return; }
            
            // Ignore WASD if user is typing in the speed boxes
            if (document.activeElement.tagName === "INPUT") return;

            if (['w', 'a', 's', 'd'].includes(key)) {
                if (currentMode === 'AUTO') {
                    if (activeKey !== key) { 
                        triggerErrorBuzzer();
                        addLog("ACCESS DENIED: Switch to MANUAL mode to drive.", true);
                    }
                    activeKey = key;
                    return; 
                }
                if (activeKey === key) return; 
                activeKey = key;
                if (key === 'w') sendCmd('M:F'); 
                else if (key === 's') sendCmd('M:B'); 
                else if (key === 'a') sendCmd('M:L'); 
                else if (key === 'd') sendCmd('M:R');
            }
        });

        document.addEventListener('keyup', (e) => {
            const key = e.key.toLowerCase();
            
            if (key === 'm') { if(activeKey === key) activeKey = null; return; }
            
            if (key === 'p') { 
                pumpActive = false; 
                if (currentMode === 'MANUAL') {
                    sendCmd("P:OFF"); 
                    document.getElementById('pump-ui').className = "status-box pump-off";
                    document.getElementById('pump-text').innerText = "OFF";
                }
                return; 
            }
            if (key === 'k' && servoDirection === -1) { servoDirection = 0; stopServoMove(); return; }
            if (key === 'l' && servoDirection === 1) { servoDirection = 0; stopServoMove(); return; }
            
            if (['w', 'a', 's', 'd'].includes(key)) { 
                if (currentMode === 'MANUAL') { sendCmd('M:S'); }
                if (activeKey === key) activeKey = null; 
            }
        });

        function startServoMove() {
            if (servoInterval) clearInterval(servoInterval);
            stepServo(); 
            servoInterval = setInterval(stepServo, 200); 
        }

        function stopServoMove() {
            if (servoInterval) clearInterval(servoInterval);
            servoInterval = null;
        }

        function stepServo() {
            currentAngle += (servoDirection * 4); 
            if (currentAngle < 0) currentAngle = 0;
            if (currentAngle > 180) currentAngle = 180;
            document.getElementById('servo-slider').value = currentAngle;
            sendCmd("S:" + currentAngle);
        }

        function updateServo(val) {
            currentAngle = parseInt(val);
            sendCmd("S:" + currentAngle);
        }

        function parseIncomingData(dataString) {
            dataString = dataString.trim();

            if (dataString === "UI:PUMP_ON") {
                document.getElementById('pump-ui').className = "status-box pump-on";
                document.getElementById('pump-text').innerText = "ON (AUTO)";
            }
            else if (dataString === "UI:PUMP_OFF") {
                if(!pumpActive) {
                    document.getElementById('pump-ui').className = "status-box pump-off";
                    document.getElementById('pump-text').innerText = "OFF";
                }
            }

            else if (dataString === "ALERT:FIRE") { 
                alarms.fire = true; evaluateAlarms(); addLog("ML ALERT: Fire Detected!", true); 
            }
            else if (dataString === "CLEAR:FIRE") { 
                alarms.fire = false; evaluateAlarms(); 
            }
            else if (dataString === "ALERT:HUMAN") { 
                alarms.human = true; evaluateAlarms(); addLog("ML ALERT: Human Detected!", true);
            }
            else if (dataString === "CLEAR:HUMAN") { 
                alarms.human = false; evaluateAlarms(); 
            }

            else if (dataString.startsWith("SONAR_L:")) {
                let dist = parseInt(dataString.split(":")[1]);
                const el = document.getElementById('sonar-l-val');
                if (dist < 50) { el.innerHTML = dist + "cm<br>BLOCK"; el.classList.add('alert'); }
                else if (dist == 999) { el.innerHTML = "999cm<br>ERR"; el.classList.remove('alert'); }
                else { el.innerHTML = dist + "cm"; el.classList.remove('alert'); }
            }
            else if (dataString.startsWith("SONAR_R:")) {
                let dist = parseInt(dataString.split(":")[1]);
                const el = document.getElementById('sonar-r-val');
                if (dist < 50) { el.innerHTML = dist + "cm<br>BLOCK"; el.classList.add('alert'); }
                else if (dist == 999) { el.innerHTML = "999cm<br>ERR"; el.classList.remove('alert'); }
                else { el.innerHTML = dist + "cm"; el.classList.remove('alert'); }
            }
            
            else if (dataString.startsWith("GAS:")) {
                const gasLevel = parseInt(dataString.replace("GAS:", "").trim());
                const gasEl = document.getElementById('gas-val');
                gasEl.innerText = gasLevel;
                
                if (gasLevel > 300) {
                    gasEl.style.color = "var(--danger)";
                    if(!alarms.gas) { alarms.gas = true; evaluateAlarms(); addLog("GAS WARNING: High levels detected!", true); }
                } else {
                    gasEl.style.color = "var(--success)";
                    if(alarms.gas) { alarms.gas = false; evaluateAlarms(); }
                }
            }
            else if (dataString.startsWith("FLAME:")) {
                const flameVals = dataString.replace("FLAME:", "").trim().split(",");
                if (flameVals.length === 3) {
                    let isHardwareFire = false;
                    
                    const fL = document.getElementById('flame-l');
                    if (flameVals[0] == "0") { fL.classList.add('alert-bg'); fL.innerHTML = "<span class='flame-label' style='color:#fff;'>LEFT FLAME</span>FIRE"; isHardwareFire = true; } 
                    else { fL.classList.remove('alert-bg'); fL.innerHTML = "<span class='flame-label'>LEFT FLAME</span>CLEAR"; }

                    const fF = document.getElementById('flame-f');
                    if (flameVals[1] == "0") { fF.classList.add('alert-bg'); fF.innerHTML = "<span class='flame-label' style='color:#fff;'>FRONT FLAME</span>FIRE"; isHardwareFire = true; } 
                    else { fF.classList.remove('alert-bg'); fF.innerHTML = "<span class='flame-label'>FRONT FLAME</span>CLEAR"; }

                    const fR = document.getElementById('flame-r');
                    if (flameVals[2] == "0") { fR.classList.add('alert-bg'); fR.innerHTML = "<span class='flame-label' style='color:#fff;'>RIGHT FLAME</span>FIRE"; isHardwareFire = true; } 
                    else { fR.classList.remove('alert-bg'); fR.innerHTML = "<span class='flame-label'>RIGHT FLAME</span>CLEAR"; }
                    
                    if (isHardwareFire && !alarms.fire) {
                        alarms.fire = true; evaluateAlarms(); addLog("HARDWARE ALERT: Fire Detected!", true);
                    } else if (!isHardwareFire && alarms.fire) {
                        alarms.fire = false; evaluateAlarms();
                    }
                }
            }
        }
    </script>
</body>
</html>
)rawliteral";

// ===========================
// 4. HTTP SERVER HANDLERS
// ===========================
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, strlen(index_html));
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
        return res;

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            res = ESP_FAIL;
        }
        else
        {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if (res == ESP_OK)
        {
            char part_buf[128];
            size_t hlen = snprintf(part_buf, 128, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            break;
        }
    }
    return res;
}

// Split Servers so Chrome (Port 80) and Python (Port 81) never block each other
void startServers()
{
    // Server 1: Dashboard (Port 80)
    httpd_config_t config_ui = HTTPD_DEFAULT_CONFIG();
    config_ui.server_port = 80;
    config_ui.ctrl_port = 80;
    httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL};

    if (httpd_start(&camera_httpd, &config_ui) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
    }

    // Server 2: Python Video Stream (Port 81)
    httpd_config_t config_stream = HTTPD_DEFAULT_CONFIG();
    config_stream.server_port = 81;
    config_stream.ctrl_port = 81;
    httpd_uri_t stream_uri = {.uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL};

    if (httpd_start(&stream_httpd, &config_stream) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

// ===========================
// 5. ULTRASONIC SENSOR LOGIC
// ===========================
long getDistance(int trigPin, int echoPin)
{
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 30000);
    if (duration == 0)
        return 999;

    return duration * 0.034 / 2;
}

// ===========================
// 6. WEBSOCKET ROUTER (Port 82)
// ===========================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    if (type == WStype_TEXT)
    {
        String msg = "";
        for (size_t i = 0; i < length; i++)
        {
            msg += (char)payload[i];
        }
        msg.trim();

        if (msg == "MODE:AUTO")
        {
            isAutoMode = true;
        }
        else if (msg == "MODE:MANUAL")
        {
            isAutoMode = false;
            Serial1.print("M:S\n");
            Serial1.print("P:OFF\n");
            webSocket.broadcastTXT("UI:PUMP_OFF");
        }
        // THE NEW FEATURE: Catching Speed Inputs from the Web Dashboard
        else if (msg.startsWith("SPD_M:")) 
        {
            manualSpeed = msg.substring(6).toInt();
        }
        else if (msg.startsWith("SPD_A:")) 
        {
            autoSpeed = msg.substring(6).toInt();
        }
        else if (msg.startsWith("S:"))
        {
            int angle = msg.substring(2).toInt();
            if (angle >= 0 && angle <= 180)
            {
                waterServo.write(angle);
            }
        }
        else if (msg.startsWith("M:") || msg.startsWith("P:"))
        {
            // Inject Manual Speed before moving
            if (msg.startsWith("M:") && msg != "M:S") {
                Serial1.print("SPD:" + String(manualSpeed) + "\n");
            }
            
            // Unrestricted manual driving
            Serial1.print(msg + "\n");
        }
        else if (msg.startsWith("ALERT:") || msg.startsWith("CLEAR:"))
        {
            if (msg == "ALERT:FIRE") mlFireActive = true;
            if (msg == "CLEAR:FIRE") mlFireActive = false;
            webSocket.broadcastTXT(msg);
        }
    }
}

// ===========================
// 7. MAIN SETUP & LOOP
// ===========================
void setup()
{
    Serial.begin(115200);

    Serial1.setRxBufferSize(256);
    Serial1.begin(9600, SERIAL_8N1, 21, 14);

    pinMode(TRIG1, OUTPUT);
    pinMode(ECHO1, INPUT);
    pinMode(TRIG2, OUTPUT);
    pinMode(ECHO2, INPUT);

    waterServo.setPeriodHertz(50);
    waterServo.attach(SERVO_PIN, 500, 2400);
    waterServo.write(90);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = 10000000;
    config.frame_size = FRAMESIZE_QVGA;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 20;
    config.fb_count = 2;

    esp_camera_init(&config);

    Serial.println("Configuring Access Point...");
    WiFi.mode(WIFI_AP);

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ap_ssid, ap_password);

    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());

    startServers();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

void loop()
{
    webSocket.loop();

    while (Serial1.available())
    {
        char c = Serial1.read();
        if (c == '\n')
        {
            arduinoBuffer.trim();
            if (arduinoBuffer.length() > 0)
            {
                if (arduinoBuffer.startsWith("FLAME:"))
                {
                    if (arduinoBuffer.indexOf("0") != -1)
                    {
                        hardwareFireActive = true;
                    }
                    else
                    {
                        hardwareFireActive = false;
                    }
                }
                webSocket.broadcastTXT(arduinoBuffer);
            }
            arduinoBuffer = "";
        }
        else
        {
            if (arduinoBuffer.length() < 250)
            {
                arduinoBuffer += c;
            }
            else
            {
                arduinoBuffer = "";
            }
        }
    }

    if (millis() - lastPingTime >= pingInterval)
    {
        lastPingTime = millis();
        long dist1 = getDistance(TRIG1, ECHO1);
        long dist2 = getDistance(TRIG2, ECHO2);

        String s1 = "SONAR_L:" + String(dist1);
        String s2 = "SONAR_R:" + String(dist2);
        webSocket.broadcastTXT(s1);
        webSocket.broadcastTXT(s2);

        leftObstacle = (dist1 < 50);
        rightObstacle = (dist2 < 50);

        if (isAutoMode)
        {
            // THE NEW FEATURE: Inject Auto Speed before making decisions
            Serial1.print("SPD:" + String(autoSpeed) + "\n");

            if (hardwareFireActive)
            {
                Serial1.print("M:S\n");  
                Serial1.print("P:ON\n"); 
                webSocket.broadcastTXT("UI:PUMP_ON");
            }
            else if (mlFireActive)
            {
                Serial1.print("P:OFF\n");
                webSocket.broadcastTXT("UI:PUMP_OFF");

                if (leftObstacle && rightObstacle)
                {
                    Serial1.print("M:R\n"); 
                }
                else if (leftObstacle)
                {
                    Serial1.print("M:R\n"); 
                }
                else if (rightObstacle)
                {
                    Serial1.print("M:L\n"); 
                }
                else
                {
                    Serial1.print("M:F\n");
                }
            }
            else
            {
                Serial1.print("P:OFF\n");
                webSocket.broadcastTXT("UI:PUMP_OFF");

                if (leftObstacle && rightObstacle)
                {
                    Serial1.print("M:R\n");
                }
                else if (leftObstacle)
                {
                    Serial1.print("M:R\n");
                }
                else if (rightObstacle)
                {
                    Serial1.print("M:L\n");
                }
                else
                {
                    Serial1.print("M:F\n");
                }
            }
        } 
    }
}