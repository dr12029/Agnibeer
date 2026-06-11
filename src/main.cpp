#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include <WebSocketsServer.h> 

// ===========================
// 1. WI-FI CREDENTIALS
// ===========================
const char* ssid = "Shadhu babar guha";
const char* password = "abds@EEE_22";

// Set your Static IP address
IPAddress local_IP(192, 168, 0, 203);
// Set your Gateway IP address (Usually your router's IP)
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   // Google DNS (optional)
IPAddress secondaryDNS(8, 8, 4, 4); // Google DNS (optional)

// ===========================
// 2. HARDWARE PINS
// ===========================
// Camera Pins
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// Ultrasonic Sensor Pins
#define TRIG1 14   // Keep these away from the camera pins
#define ECHO1 21  // Remember the 10k/20k Voltage Divider!
#define TRIG2 38
#define ECHO2 47

// ===========================
// Global Servers & Timers
// ===========================
httpd_handle_t camera_httpd = NULL;
WebSocketsServer webSocket = WebSocketsServer(81); 

unsigned long lastPingTime = 0;
const int pingInterval = 100; // Check distance every 100ms
bool obstacleDetected = false;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ===========================
// 3. HTML DASHBOARD 
// ===========================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Rover Command</title>
    <style>
        :root { --bg-color: #121212; --panel-bg: #1e1e1e; --text-main: #ffffff; --text-muted: #aaaaaa; --accent: #007bff; --danger: #ff4444; --success: #00C851; --warning: #ffbb33; }
        body { background-color: var(--bg-color); color: var(--text-main); font-family: 'Segoe UI', sans-serif; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
        .header { width: 100%; max-width: 1200px; display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; border-bottom: 1px solid #333; padding-bottom: 10px; }
        .status-indicator { display: flex; align-items: center; gap: 8px; font-weight: bold; }
        .dot { width: 12px; height: 12px; background-color: var(--danger); border-radius: 50%; box-shadow: 0 0 8px var(--danger); }
        .dot.connected { background-color: var(--success); box-shadow: 0 0 8px var(--success); }
        .dashboard-grid { display: grid; grid-template-columns: 1.2fr 1fr 1fr; gap: 20px; max-width: 1200px; width: 100%; }
        .column { display: flex; flex-direction: column; gap: 20px; }
        .panel { background-color: var(--panel-bg); border-radius: 8px; padding: 15px; border: 1px solid #333; }
        .panel h2 { margin-top: 0; font-size: 1.1rem; color: var(--text-muted); border-bottom: 1px solid #333; padding-bottom: 8px; margin-bottom: 15px; text-transform: uppercase; }
        .video-container { width: 100%; aspect-ratio: 4/3; background-color: #000; border-radius: 6px; overflow: hidden; display: flex; justify-content: center; align-items: center; border: 1px solid #444; }
        #camera-stream { width: 100%; height: 100%; object-fit: cover; }
        .sensor-row { display: flex; justify-content: space-between; background-color: #2a2a2a; padding: 10px; border-radius: 4px; margin-bottom: 10px; font-size: 1rem; }
        .status-badge { font-weight: bold; color: var(--success); }
        .status-badge.alert { color: var(--danger); animation: blink 1s infinite; }
        .status-badge.warning { color: var(--warning); }
        @keyframes blink { 50% { opacity: 0.5; } }
        .log-container { height: 400px; overflow-y: auto; background-color: #111; padding: 10px; border-radius: 4px; font-family: monospace; font-size: 0.85rem; display: flex; flex-direction: column; gap: 5px; }
        .log-entry { border-bottom: 1px solid #222; padding-bottom: 5px; color: var(--text-muted); }
        .log-entry.alert { color: var(--danger); font-weight: bold; }
        .log-time { color: var(--accent); margin-right: 8px; }
    </style>
</head>
<body>
    <div class="header">
        <h1>Rover Command Center</h1>
        <div class="status-indicator"><div class="dot" id="ws-dot"></div><span id="ws-status">Disconnected</span></div>
    </div>
    <div class="dashboard-grid">
        <div class="column">
            <div class="panel">
                <h2>Live Feed (Edge ML)</h2>
                <div class="video-container"><img id="camera-stream" alt="Waiting for stream..." src=""></div>
            </div>
            <div class="panel">
                <h2>AI Telemetry</h2>
                <div class="sensor-row"><span>Fire Detection (YOLO):</span><span id="fire-val" class="status-badge">CLEAR</span></div>
                <div class="sensor-row"><span>Human Detection (YOLO):</span><span id="human-val" class="status-badge">CLEAR</span></div>
                <div class="sensor-row"><span>Gas Level (MQ-2):</span><strong id="gas-val">--</strong></div>
                <div class="sensor-row"><span>Hardware Fire Array:</span><span id="flame-val" class="status-badge">CLEAR</span></div>
                <div class="sensor-row"><span>Sonar Obstacle:</span><span id="sonar-val" class="status-badge">CLEAR</span></div>
            </div>
        </div>
        <div class="column">
            <div class="panel">
                <h2>Navigation Controls</h2>
                <p style="color: #888; font-family: monospace;">[W] [A] [S] [D] to Drive</p>
                <p style="color: #888; font-family: monospace;">[P] Toggle Water Pump</p>
                <p id="collision-warn" style="color: var(--danger); display: none; font-weight: bold;">⚠️ COLLISION OVERRIDE ACTIVE</p>
            </div>
        </div>
        <div class="column">
            <div class="panel" style="height: 100%; display: flex; flex-direction: column;">
                <h2>System Log</h2>
                <div class="log-container" id="log-box"></div>
            </div>
        </div>
    </div>
    <script>
        let ws; 
        const gateway = `ws://${window.location.hostname}:81/`;
        let activeKey = null;
        
        window.onload = () => {
            const streamImg = document.getElementById('camera-stream');
            // Pulling video from the laptop's Python Flask server
            streamImg.src = "http://127.0.0.1:5000/video_feed"; 
            addLog("System initialized. Waiting for connection...");
            initWebSocket();
        };

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
            ws.onopen = () => { document.getElementById('ws-dot').classList.add('connected'); document.getElementById('ws-status').innerText = "Connected"; addLog("WebSocket connected.", false); };
            ws.onclose = () => { document.getElementById('ws-dot').classList.remove('connected'); document.getElementById('ws-status').innerText = "Disconnected"; addLog("Connection lost. Retrying...", true); setTimeout(initWebSocket, 2000); };
            ws.onmessage = (event) => { parseIncomingData(event.data); };
        }

        function sendCmd(cmd) { if (ws && ws.readyState === WebSocket.OPEN) { ws.send(cmd + "\n"); } }

        // --- Keyboard Controls ---
        document.addEventListener('keydown', (e) => {
            const key = e.key.toLowerCase();
            if (key === 'p') { sendCmd("P:ON"); return; } // Simplified Pump test
            
            if (activeKey === key || !['w', 'a', 's', 'd'].includes(key)) return; 
            activeKey = key;
            if (key === 'w') sendCmd('M:F'); else if (key === 's') sendCmd('M:B'); else if (key === 'a') sendCmd('M:L'); else if (key === 'd') sendCmd('M:R');
        });

        document.addEventListener('keyup', (e) => {
            const key = e.key.toLowerCase();
            if (key === 'p') { sendCmd("P:OFF"); return; }
            if (['w', 'a', 's', 'd'].includes(key)) { sendCmd('M:S'); if (activeKey === key) activeKey = null; }
        });

        // --- Parsing Mixed Telemetry (Python + Arduino) ---
        function parseIncomingData(dataString) {
            // Python AI Alerts
            if (dataString === "ALERT:FIRE") {
                document.getElementById('fire-val').innerText = "FIRE DETECTED!"; document.getElementById('fire-val').classList.add('alert');
                addLog("CRITICAL: Python ML detected FIRE!", true);
            } 
            else if (dataString === "CLEAR:FIRE") {
                document.getElementById('fire-val').innerText = "CLEAR"; document.getElementById('fire-val').classList.remove('alert');
            }
            else if (dataString === "ALERT:HUMAN") {
                document.getElementById('human-val').innerText = "PERSON DETECTED"; document.getElementById('human-val').classList.add('warning');
            }
            else if (dataString === "CLEAR:HUMAN") {
                document.getElementById('human-val').innerText = "CLEAR"; document.getElementById('human-val').classList.remove('warning');
            }
            // Ultrasonic Alerts
            else if (dataString === "ALERT:SONAR") {
                document.getElementById('sonar-val').innerText = "OBSTACLE"; document.getElementById('sonar-val').classList.add('warning');
                document.getElementById('collision-warn').style.display = "block";
            }
            else if (dataString === "CLEAR:SONAR") {
                document.getElementById('sonar-val').innerText = "CLEAR"; document.getElementById('sonar-val').classList.remove('warning');
                document.getElementById('collision-warn').style.display = "none";
            }
            // Arduino Telemetry (GAS:145 | FLAME_SENSORS:[1,1,1])
            else if (dataString.includes("GAS:")) {
                try {
                    const parts = dataString.split(" | ");
                    document.getElementById('gas-val').innerText = parts[0].replace("GAS:", "").trim();
                    const flameArrayStr = parts[1].replace("FLAME_SENSORS:", "").trim();
                    if (flameArrayStr.includes("0")) {
                        document.getElementById('flame-val').innerText = "FIRE DETECTED!"; document.getElementById('flame-val').classList.add('alert');
                    } else {
                        document.getElementById('flame-val').innerText = "CLEAR"; document.getElementById('flame-val').classList.remove('alert');
                    }
                } catch (e) {}
            }
        }
    </script>
</body>
</html>
)rawliteral";

// ===========================
// 4. HTTP SERVER HANDLERS
// ===========================
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, strlen(index_html));
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t * _jpg_buf = NULL;
    char * part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){ return res; }

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) { res = ESP_FAIL; } 
        else { _jpg_buf_len = fb->len; _jpg_buf = fb->buf; }
        
        if(res == ESP_OK){
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if(res == ESP_OK){ res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len); }
        if(res == ESP_OK){ res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY)); }
        
        if(fb){ esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; }
        if(res != ESP_OK){ break; }
    }
    return res;
}

// Replace your existing startServers function with this:
void startServers(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 200; // <--- CHANGED TO PORT 200

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
    
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &stream_uri);
    }
}

// ===========================
// 5. ULTRASONIC SENSOR LOGIC
// ===========================
long getDistance(int trigPin, int echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    
    // Timeout set to 30000us (approx 5 meters) to prevent hanging
    long duration = pulseIn(echoPin, HIGH, 30000); 
    if (duration == 0) return 999; // No echo received
    
    return duration * 0.034 / 2; // Convert to cm
}

// ===========================
// 6. WEBSOCKET ROUTER
// ===========================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if(type == WStype_TEXT) {
        String msg = String((char*)payload);
        
        // --- Python ML Commands ---
        if(msg == "CMD:ML_FIRE") {
            webSocket.broadcastTXT("ALERT:FIRE"); 
            Serial1.print("P:ON\n"); 
        }
        else if(msg == "CMD:ML_FIRE_CLEAR") {
            webSocket.broadcastTXT("CLEAR:FIRE");
            Serial1.print("P:OFF\n");
        }
        else if(msg == "CMD:ML_HUMAN") { webSocket.broadcastTXT("ALERT:HUMAN"); }
        else if(msg == "CMD:ML_HUMAN_CLEAR") { webSocket.broadcastTXT("CLEAR:HUMAN"); }
        
        // --- Dashboard Manual Controls ---
        // Forward WASD and manual pump controls directly to the Arduino
        else if (msg.startsWith("M:") || msg.startsWith("P:")) {
            // Prevent driving forward if obstacle detected
            if (obstacleDetected && msg.startsWith("M:F")) {
                Serial1.print("M:S\n"); // Force stop instead
            } else {
                Serial1.print(msg + "\n");
            }
        }
    }
}

// ===========================
// 7. MAIN SETUP & LOOP
// ===========================
void setup() {
    Serial.begin(115200);
    
    // START ARDUINO BRIDGE (TX=1, RX=2)
    Serial1.begin(9600, SERIAL_8N1, 1, 2);

    // Init Sonar Pins
    pinMode(TRIG1, OUTPUT); pinMode(ECHO1, INPUT);
    pinMode(TRIG2, OUTPUT); pinMode(ECHO2, INPUT);

    // Camera Init
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = 10000000;
    config.frame_size = FRAMESIZE_QVGA; 
    config.pixel_format = PIXFORMAT_JPEG; 
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 20; 
    config.fb_count = 2;       

    esp_camera_init(&config);

    // Configure the Static IP BEFORE connecting
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
        Serial.println("STA Failed to configure");
    }

    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");

    startServers();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    // This is what I forgot to add!
    Serial.print("SUCCESS! System Ready. Open Dashboard at: http://");
    Serial.print(WiFi.localIP());
    Serial.println(":200/"); // <--- Added the :200 port to the output
}

void loop() {
    webSocket.loop(); // Process incoming Dashboard/Python messages

    // 1. Process Arduino Telemetry
    if (Serial1.available()) {
        String arduinoData = Serial1.readStringUntil('\n');
        arduinoData.trim();
        if (arduinoData.length() > 0) {
            // Forward directly to the Dashboard UI
            webSocket.broadcastTXT(arduinoData);
        }
    }

    // 2. Process Ultrasonic Sensors (Non-blocking)
    if (millis() - lastPingTime >= pingInterval) {
        lastPingTime = millis();
        
        long dist1 = getDistance(TRIG1, ECHO1);
        long dist2 = getDistance(TRIG2, ECHO2);
        
        // If an object is closer than 20cm
        if (dist1 < 20 || dist2 < 20) {
            if (!obstacleDetected) {
                obstacleDetected = true;
                Serial1.print("M:S\n"); // Immediately command Arduino to stop
                webSocket.broadcastTXT("ALERT:SONAR"); // Warn Dashboard
            }
        } else {
            if (obstacleDetected) {
                obstacleDetected = false;
                webSocket.broadcastTXT("CLEAR:SONAR"); // Clear Dashboard Warning
            }
        }
    }
}