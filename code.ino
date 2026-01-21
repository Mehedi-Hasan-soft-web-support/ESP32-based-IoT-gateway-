#include <WiFi.h>
#include <esp_now.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// ============ SYSTEM INFO ============
#define OS_NAME "EarthQuakeOS"
#define OS_VERSION "1.0.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// ============ WiFi CONFIGURATION ============
const char* ssid = "Me";
const char* password = "mehedi113";

// ============ MQTT CONFIGURATION ============
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_Gateway_Earthquake";

const char* topic_node1 = "earthquake/node1";
const char* topic_node2 = "earthquake/node2";
const char* topic_status = "earthquake/status";
const char* topic_alert = "earthquake/alert";

#define LED_PIN 2

// ============ WEB SERVER ============
WebServer server(80);

// ============ NODE 3 CONFIGURATION ============
uint8_t node3Address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool node3Registered = false;

// ============ DATA STRUCTURES ============
typedef struct struct_sensor_message {
  char nodeID[32];
  float accelX;
  float accelY;
  float accelZ;
  float magnitude;
  unsigned long timestamp;
} struct_sensor_message;

typedef struct struct_alert_message {
  char command[32];
  float magnitude;
  unsigned long timestamp;
} struct_alert_message;

struct_sensor_message node1Data = {"NONE", 0, 0, 0, 0, 0};
struct_sensor_message node2Data = {"NONE", 0, 0, 0, 0, 0};
struct_alert_message alertToNode3;

// ============ SYSTEM STATISTICS ============
struct SystemStats {
  unsigned long uptime;
  uint32_t freeHeap;
  uint32_t totalHeap;
  uint8_t heapPercent;
  uint8_t cpuUsage;
  float cpuTemp;
  int wifiSignal;
  unsigned long taskSwitches;
  unsigned long packetsSent;
  unsigned long packetsReceived;
} sysStats;

bool node1Connected = false;
bool node2Connected = false;
unsigned long lastNode1Update = 0;
unsigned long lastNode2Update = 0;
const unsigned long NODE_TIMEOUT = 5000;

float earthquakeThreshold = 0.3;
float warningThreshold = 0.15;
bool earthquakeDetected = false;
bool warningDetected = false;
unsigned long earthquakeStartTime = 0;
const unsigned long MIN_DETECTION_TIME = 500;

int node1Count = 0;
int node2Count = 0;
int node3AlertsSent = 0;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long bootTime;
unsigned long lastCpuCalc = 0;
unsigned long lastStatsUpdate = 0;

// ============ FUNCTION PROTOTYPES ============
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len);
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status);
void detectEarthquake();
void sendAlertToNode3(const char* command, float magnitude);
void blinkLED();
void checkNodeStatus();
void reconnectMQTT();
void publishNodeData(int nodeNum);
void publishStatus();
void publishAlert(const char* message, float magnitude);
void registerNode3Peer();
void setupWebServer();
void handleRoot();
void handleAPI();
void handleSystemStats();
void updateSystemStats();
float getCPUTemp();
void printBootScreen();

// ============ HTML PAGE ============
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>EarthQuakeOS Terminal</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
  font-family: 'Courier New', monospace;
  background: #0a0e27;
  color: #00ff41;
  overflow: hidden;
}
.scanlines {
  position: fixed;
  top: 0; left: 0;
  width: 100%; height: 100%;
  background: linear-gradient(transparent 50%, rgba(0, 255, 65, 0.02) 50%);
  background-size: 100% 4px;
  pointer-events: none;
  z-index: 999;
  animation: scan 8s linear infinite;
}
@keyframes scan {
  0% { transform: translateY(0); }
  100% { transform: translateY(4px); }
}
.container {
  padding: 20px;
  max-width: 1400px;
  margin: 0 auto;
  animation: fadeIn 1s;
}
@keyframes fadeIn {
  from { opacity: 0; }
  to { opacity: 1; }
}
.header {
  text-align: center;
  border: 2px solid #00ff41;
  padding: 15px;
  margin-bottom: 20px;
  background: rgba(0, 255, 65, 0.05);
  box-shadow: 0 0 20px rgba(0, 255, 65, 0.3);
  animation: glow 2s ease-in-out infinite;
}
@keyframes glow {
  0%, 100% { box-shadow: 0 0 20px rgba(0, 255, 65, 0.3); }
  50% { box-shadow: 0 0 30px rgba(0, 255, 65, 0.6); }
}
h1 {
  color: #00ff41;
  text-shadow: 0 0 10px #00ff41;
  font-size: 2em;
  letter-spacing: 3px;
}
.version {
  color: #0f0;
  font-size: 0.9em;
  opacity: 0.8;
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  gap: 15px;
  margin-bottom: 15px;
}
.panel {
  border: 1px solid #00ff41;
  padding: 15px;
  background: rgba(0, 20, 40, 0.8);
  box-shadow: inset 0 0 15px rgba(0, 255, 65, 0.1);
  transition: all 0.3s;
}
.panel:hover {
  border-color: #0f0;
  box-shadow: 0 0 25px rgba(0, 255, 65, 0.4);
  transform: translateY(-2px);
}
.panel-title {
  color: #0f0;
  font-size: 1.1em;
  margin-bottom: 10px;
  border-bottom: 1px solid #00ff41;
  padding-bottom: 5px;
  text-shadow: 0 0 5px #0f0;
}
.stat-row {
  display: flex;
  justify-content: space-between;
  padding: 8px 0;
  border-bottom: 1px dotted rgba(0, 255, 65, 0.2);
}
.stat-label { color: #0f0; opacity: 0.8; }
.stat-value {
  color: #00ff41;
  font-weight: bold;
  text-shadow: 0 0 5px #00ff41;
}
.progress-bar {
  width: 100%;
  height: 20px;
  background: rgba(0, 50, 0, 0.5);
  border: 1px solid #00ff41;
  position: relative;
  overflow: hidden;
  margin-top: 5px;
}
.progress-fill {
  height: 100%;
  background: linear-gradient(90deg, #00ff41, #0f0);
  transition: width 0.5s ease;
  box-shadow: 0 0 10px #0f0;
}
.progress-text {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  color: #000;
  font-weight: bold;
  text-shadow: 0 0 3px #0f0;
}
.sensor-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 15px;
}
.sensor-card {
  border: 1px solid #00ff41;
  padding: 12px;
  background: rgba(0, 30, 0, 0.6);
  position: relative;
}
.sensor-card.active {
  border-color: #0f0;
  animation: pulse 2s infinite;
}
@keyframes pulse {
  0%, 100% { box-shadow: 0 0 10px rgba(0, 255, 65, 0.5); }
  50% { box-shadow: 0 0 25px rgba(0, 255, 65, 0.8); }
}
.sensor-card.inactive {
  border-color: #ff0000;
  opacity: 0.5;
}
.magnitude {
  font-size: 2em;
  text-align: center;
  margin: 10px 0;
  text-shadow: 0 0 10px #00ff41;
}
.alert-box {
  padding: 15px;
  margin: 15px 0;
  border: 2px solid;
  text-align: center;
  font-size: 1.2em;
  animation: blink 1s infinite;
}
@keyframes blink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.7; }
}
.alert-normal {
  border-color: #00ff41;
  background: rgba(0, 255, 65, 0.1);
  color: #0f0;
}
.alert-warning {
  border-color: #ffff00;
  background: rgba(255, 255, 0, 0.1);
  color: #ff0;
}
.alert-critical {
  border-color: #ff0000;
  background: rgba(255, 0, 0, 0.1);
  color: #f00;
  animation: shake 0.5s infinite;
}
@keyframes shake {
  0%, 100% { transform: translateX(0); }
  25% { transform: translateX(-5px); }
  75% { transform: translateX(5px); }
}
.terminal {
  background: #000;
  border: 1px solid #00ff41;
  padding: 15px;
  height: 250px;
  overflow-y: auto;
  font-size: 0.9em;
  box-shadow: inset 0 0 20px rgba(0, 255, 65, 0.1);
}
.terminal::-webkit-scrollbar { width: 8px; }
.terminal::-webkit-scrollbar-track { background: #0a0e27; }
.terminal::-webkit-scrollbar-thumb {
  background: #00ff41;
  box-shadow: 0 0 5px #0f0;
}
.log-entry {
  margin: 5px 0;
  animation: slideIn 0.3s;
}
@keyframes slideIn {
  from { transform: translateX(-20px); opacity: 0; }
  to { transform: translateX(0); opacity: 1; }
}
.timestamp { color: #0a0; }
.log-info { color: #0f0; }
.log-warn { color: #ff0; }
.log-error { color: #f00; }
.footer {
  text-align: center;
  padding: 15px;
  border-top: 1px solid #00ff41;
  margin-top: 20px;
  opacity: 0.7;
}
.node-status {
  display: inline-block;
  width: 12px;
  height: 12px;
  border-radius: 50%;
  margin-right: 5px;
  box-shadow: 0 0 8px;
  animation: statusBlink 2s infinite;
}
@keyframes statusBlink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}
.status-online { background: #0f0; box-shadow: 0 0 10px #0f0; }
.status-offline { background: #f00; box-shadow: 0 0 10px #f00; }
.blink-text {
  animation: textBlink 1.5s infinite;
}
@keyframes textBlink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.3; }
}
</style>
</head>
<body>
<div class="scanlines"></div>
<div class="container">
  <div class="header">
    <h1>⚡ EARTHQUAKE OS ⚡</h1>
    <div class="version">v1.0.0 | BUILD: STABLE | CORE: ESP32</div>
    <div class="version" id="uptime">UPTIME: 00:00:00</div>
  </div>

  <div class="grid">
    <div class="panel">
      <div class="panel-title">⚙️ SYSTEM RESOURCES</div>
      <div class="stat-row">
        <span class="stat-label">CPU USAGE:</span>
        <span class="stat-value" id="cpu">0%</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill" id="cpu-bar" style="width: 0%"></div>
        <div class="progress-text" id="cpu-text">0%</div>
      </div>
      
      <div class="stat-row" style="margin-top: 10px;">
        <span class="stat-label">RAM USAGE:</span>
        <span class="stat-value" id="ram">0%</span>
      </div>
      <div class="progress-bar">
        <div class="progress-fill" id="ram-bar" style="width: 0%"></div>
        <div class="progress-text" id="ram-text">0%</div>
      </div>

      <div class="stat-row" style="margin-top: 10px;">
        <span class="stat-label">CPU TEMP:</span>
        <span class="stat-value" id="temp">0°C</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">FREE HEAP:</span>
        <span class="stat-value" id="heap">0 KB</span>
      </div>
    </div>

    <div class="panel">
      <div class="panel-title">📡 NETWORK STATUS</div>
      <div class="stat-row">
        <span class="stat-label">WiFi SIGNAL:</span>
        <span class="stat-value" id="wifi">0 dBm</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">IP ADDRESS:</span>
        <span class="stat-value" id="ip">0.0.0.0</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">MQTT:</span>
        <span class="stat-value" id="mqtt">DISCONNECTED</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">PACKETS RX:</span>
        <span class="stat-value" id="pkt-rx">0</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">PACKETS TX:</span>
        <span class="stat-value" id="pkt-tx">0</span>
      </div>
    </div>

    <div class="panel">
      <div class="panel-title">📊 PROCESS MONITOR</div>
      <div class="stat-row">
        <span class="stat-label">TASK SWITCHES:</span>
        <span class="stat-value" id="tasks">0</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">ESP-NOW:</span>
        <span class="stat-value">ACTIVE</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">WEB SERVER:</span>
        <span class="stat-value">RUNNING</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">DETECTION:</span>
        <span class="stat-value" id="detection">ONLINE</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">ALERTS SENT:</span>
        <span class="stat-value" id="alerts">0</span>
      </div>
    </div>

    <div class="panel">
      <div class="panel-title">⚡ THRESHOLDS</div>
      <div class="stat-row">
        <span class="stat-label">WARNING:</span>
        <span class="stat-value" id="warn-thresh">0.15g</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">CRITICAL:</span>
        <span class="stat-value" id="crit-thresh">0.30g</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">MIN DETECTION:</span>
        <span class="stat-value">500ms</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">NODE TIMEOUT:</span>
        <span class="stat-value">5000ms</span>
      </div>
    </div>
  </div>

  <div id="alert-status" class="alert-box alert-normal">
    🟢 SYSTEM NORMAL - NO THREATS DETECTED
  </div>

  <div class="sensor-grid">
    <div id="node1-card" class="sensor-card inactive">
      <div class="panel-title">
        <span class="node-status status-offline"></span>
        NODE 1 - SENSOR A
      </div>
      <div class="magnitude" id="node1-mag">0.000g</div>
      <div class="stat-row">
        <span class="stat-label">X:</span>
        <span class="stat-value" id="node1-x">0.00</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Y:</span>
        <span class="stat-value" id="node1-y">0.00</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Z:</span>
        <span class="stat-value" id="node1-z">0.00</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">PACKETS:</span>
        <span class="stat-value" id="node1-count">0</span>
      </div>
    </div>

    <div id="node2-card" class="sensor-card inactive">
      <div class="panel-title">
        <span class="node-status status-offline"></span>
        NODE 2 - SENSOR B
      </div>
      <div class="magnitude" id="node2-mag">0.000g</div>
      <div class="stat-row">
        <span class="stat-label">X:</span>
        <span class="stat-value" id="node2-x">0.00</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Y:</span>
        <span class="stat-value" id="node2-y">0.00</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">Z:</span>
        <span class="stat-value" id="node2-z">0.00</span>
      </div>
      <div class="stat-row">
        <span class="stat-label">PACKETS:</span>
        <span class="stat-value" id="node2-count">0</span>
      </div>
    </div>
  </div>

  <div class="panel">
    <div class="panel-title">💻 SYSTEM LOG MONITOR</div>
    <div class="terminal" id="terminal"></div>
  </div>

  <div class="footer">
    <span class="blink-text">● SYSTEM OPERATIONAL ●</span>
    | DESIGNED BY: SEISMIC DEFENSE SYSTEMS | POWERED BY: ESP32
  </div>
</div>

<script>
let logBuffer = [];
const maxLogs = 100;

function addLog(msg, type = 'info') {
  const now = new Date();
  const time = now.toTimeString().split(' ')[0];
  const entry = `<div class="log-entry"><span class="timestamp">[${time}]</span> <span class="log-${type}">${msg}</span></div>`;
  logBuffer.push(entry);
  if (logBuffer.length > maxLogs) logBuffer.shift();
  
  const terminal = document.getElementById('terminal');
  terminal.innerHTML = logBuffer.join('');
  terminal.scrollTop = terminal.scrollHeight;
}

function updateData() {
  fetch('/api')
    .then(r => r.json())
    .then(data => {
      // System Stats
      document.getElementById('cpu').textContent = data.cpu + '%';
      document.getElementById('cpu-bar').style.width = data.cpu + '%';
      document.getElementById('cpu-text').textContent = data.cpu + '%';
      
      document.getElementById('ram').textContent = data.ramPercent + '%';
      document.getElementById('ram-bar').style.width = data.ramPercent + '%';
      document.getElementById('ram-text').textContent = data.ramPercent + '%';
      
      document.getElementById('temp').textContent = data.temp.toFixed(1) + '°C';
      document.getElementById('heap').textContent = (data.freeHeap / 1024).toFixed(1) + ' KB';
      document.getElementById('wifi').textContent = data.wifi + ' dBm';
      document.getElementById('tasks').textContent = data.tasks;
      document.getElementById('pkt-rx').textContent = data.pktRx;
      document.getElementById('pkt-tx').textContent = data.pktTx;
      document.getElementById('alerts').textContent = data.alertsSent;
      
      // Uptime
      const upSec = Math.floor(data.uptime / 1000);
      const h = Math.floor(upSec / 3600);
      const m = Math.floor((upSec % 3600) / 60);
      const s = upSec % 60;
      document.getElementById('uptime').textContent = 
        `UPTIME: ${h.toString().padStart(2,'0')}:${m.toString().padStart(2,'0')}:${s.toString().padStart(2,'0')}`;
      
      // Node 1
      const n1Card = document.getElementById('node1-card');
      if (data.node1.connected) {
        n1Card.className = 'sensor-card active';
        n1Card.querySelector('.node-status').className = 'node-status status-online';
        document.getElementById('node1-mag').textContent = data.node1.mag.toFixed(3) + 'g';
        document.getElementById('node1-x').textContent = data.node1.x.toFixed(2);
        document.getElementById('node1-y').textContent = data.node1.y.toFixed(2);
        document.getElementById('node1-z').textContent = data.node1.z.toFixed(2);
        document.getElementById('node1-count').textContent = data.node1.count;
      } else {
        n1Card.className = 'sensor-card inactive';
        n1Card.querySelector('.node-status').className = 'node-status status-offline';
      }
      
      // Node 2
      const n2Card = document.getElementById('node2-card');
      if (data.node2.connected) {
        n2Card.className = 'sensor-card active';
        n2Card.querySelector('.node-status').className = 'node-status status-online';
        document.getElementById('node2-mag').textContent = data.node2.mag.toFixed(3) + 'g';
        document.getElementById('node2-x').textContent = data.node2.x.toFixed(2);
        document.getElementById('node2-y').textContent = data.node2.y.toFixed(2);
        document.getElementById('node2-z').textContent = data.node2.z.toFixed(2);
        document.getElementById('node2-count').textContent = data.node2.count;
      } else {
        n2Card.className = 'sensor-card inactive';
        n2Card.querySelector('.node-status').className = 'node-status status-offline';
      }
      
      // Alert Status
      const alertBox = document.getElementById('alert-status');
      if (data.earthquake) {
        alertBox.className = 'alert-box alert-critical';
        alertBox.innerHTML = '🚨 CRITICAL: EARTHQUAKE DETECTED! MAGNITUDE: ' + data.avgMag.toFixed(3) + 'g';
        if (!window.lastEarthquake) {
          addLog('⚠️ EARTHQUAKE DETECTED! Activating emergency protocols...', 'error');
        }
        window.lastEarthquake = true;
      } else if (data.warning) {
        alertBox.className = 'alert-box alert-warning';
        alertBox.innerHTML = '⚠️ WARNING: VIBRATION DETECTED - MAGNITUDE: ' + data.avgMag.toFixed(3) + 'g';
        window.lastEarthquake = false;
      } else {
        alertBox.className = 'alert-box alert-normal';
        alertBox.innerHTML = '🟢 SYSTEM NORMAL - NO THREATS DETECTED';
        if (window.lastEarthquake === true) {
          addLog('✓ System returned to normal state', 'info');
        }
        window.lastEarthquake = false;
      }
      
      // MQTT Status
      document.getElementById('mqtt').textContent = data.mqtt ? 'CONNECTED' : 'DISCONNECTED';
      document.getElementById('mqtt').style.color = data.mqtt ? '#0f0' : '#f00';
      
      // IP Address
      document.getElementById('ip').textContent = data.ip;
    })
    .catch(e => addLog('API Error: ' + e.message, 'error'));
}

addLog('✓ EarthQuakeOS v1.0.0 initialized', 'info');
addLog('✓ Web interface loaded successfully', 'info');
addLog('✓ Connecting to gateway...', 'info');

setInterval(updateData, 500);
updateData();
</script>
</body>
</html>
)rawliteral";

// ============ ESP-NOW CALLBACKS ============
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  struct_sensor_message tempData;
  memcpy(&tempData, incomingData, sizeof(tempData));
  
  sysStats.packetsReceived++;
  blinkLED();
  
  if (strcmp(tempData.nodeID, "NODE_1") == 0) {
    memcpy(&node1Data, &tempData, sizeof(tempData));
    node1Connected = true;
    lastNode1Update = millis();
    node1Count++;
    publishNodeData(1);
  } else if (strcmp(tempData.nodeID, "NODE_2") == 0) {
    memcpy(&node2Data, &tempData, sizeof(tempData));
    node2Connected = true;
    lastNode2Update = millis();
    node2Count++;
    publishNodeData(2);
  }
}

void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    sysStats.packetsSent++;
  }
}

// ============ WEB SERVER HANDLERS ============
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleAPI() {
  StaticJsonDocument<1024> doc;
  
  // System stats
  doc["cpu"] = sysStats.cpuUsage;
  doc["temp"] = sysStats.cpuTemp;
  doc["freeHeap"] = sysStats.freeHeap;
  doc["ramPercent"] = sysStats.heapPercent;
  doc["wifi"] = sysStats.wifiSignal;
  doc["uptime"] = millis() - bootTime;
  doc["tasks"] = sysStats.taskSwitches;
  doc["pktRx"] = sysStats.packetsReceived;
  doc["pktTx"] = sysStats.packetsSent;
  doc["alertsSent"] = node3AlertsSent;
  doc["mqtt"] = mqttClient.connected();
  doc["ip"] = WiFi.localIP().toString();
  
  // Node 1
  JsonObject n1 = doc.createNestedObject("node1");
  n1["connected"] = node1Connected;
  n1["mag"] = node1Data.magnitude;
  n1["x"] = node1Data.accelX;
  n1["y"] = node1Data.accelY;
  n1["z"] = node1Data.accelZ;
  n1["count"] = node1Count;
  
  // Node 2
  JsonObject n2 = doc.createNestedObject("node2");
  n2["connected"] = node2Connected;
  n2["mag"] = node2Data.magnitude;
  n2["x"] = node2Data.accelX;
  n2["y"] = node2Data.accelY;
  n2["z"] = node2Data.accelZ;
  n2["count"] = node2Count;
  
  // Alert status
  float avgMag = 0;
  if (node1Connected && node2Connected) {
    avgMag = (node1Data.magnitude + node2Data.magnitude) / 2.0;
  } else if (node1Connected) {
    avgMag = node1Data.magnitude;
  } else if (node2Connected) {
    avgMag = node2Data.magnitude;
  }
  
  doc["avgMag"] = avgMag;
  doc["earthquake"] = earthquakeDetected;
  doc["warning"] = warningDetected;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ============ SYSTEM STATS UPDATE ============
void updateSystemStats() {
  sysStats.uptime = millis() - bootTime;
  sysStats.freeHeap = ESP.getFreeHeap();
  sysStats.totalHeap = ESP.getHeapSize();
  sysStats.heapPercent = 100 - ((sysStats.freeHeap * 100) / sysStats.totalHeap);
  
  // Simple CPU usage estimation
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  if (now - lastCheck > 0) {
    sysStats.cpuUsage = random(15, 45); // Simulated for demo
    lastCheck = now;
  }
  
  sysStats.cpuTemp = getCPUTemp();
  sysStats.wifiSignal = WiFi.RSSI();
  sysStats.taskSwitches++;
}

float getCPUTemp() {
  return 45.0 + (sysStats.cpuUsage / 10.0); // Simulated temp based on usage
}

// ============ SETUP WEB SERVER ============
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api", handleAPI);
  server.begin();
  Serial.println("✓ Web Server Started");
  Serial.print("📱 Dashboard URL: http://");
  Serial.println(WiFi.localIP());
}

// ============ PRINT BOOT SCREEN ============
void printBootScreen() {
  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════════════════════════╗");
  Serial.println("║                                                       ║");
  Serial.println("║     ███████╗ █████╗ ██████╗ ████████╗██╗  ██╗       ║");
  Serial.println("║     ██╔════╝██╔══██╗██╔══██╗╚══██╔══╝██║  ██║       ║");
  Serial.println("║     █████╗  ███████║██████╔╝   ██║   ███████║       ║");
  Serial.println("║     ██╔══╝  ██╔══██║██╔══██╗   ██║   ██╔══██║       ║");
  Serial.println("║     ███████╗██║  ██║██║  ██║   ██║   ██║  ██║       ║");
  Serial.println("║     ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝       ║");
  Serial.println("║                                                       ║");
  Serial.println("║          ██████╗ ██╗   ██╗ █████╗ ██╗  ██╗███████╗  ║");
  Serial.println("║         ██╔═══██╗██║   ██║██╔══██╗██║ ██╔╝██╔════╝  ║");
  Serial.println("║         ██║   ██║██║   ██║███████║█████╔╝ █████╗    ║");
  Serial.println("║         ██║▄▄ ██║██║   ██║██╔══██║██╔═██╗ ██╔══╝    ║");
  Serial.println("║         ╚██████╔╝╚██████╔╝██║  ██║██║  ██╗███████╗  ║");
  Serial.println("║          ╚══▀▀═╝  ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝  ║");
  Serial.println("║                                                       ║");
  Serial.println("║                      ┏━━━━━━━━━━━━━┓                 ║");
  Serial.println("║                      ┃  OS v1.0.0  ┃                 ║");
  Serial.println("║                      ┗━━━━━━━━━━━━━┛                 ║");
  Serial.println("║                                                       ║");
  Serial.println("╠═══════════════════════════════════════════════════════╣");
  Serial.println("║  SEISMIC DEFENSE OPERATING SYSTEM                    ║");
  Serial.println("║  Real-Time Earthquake Detection & Response           ║");
  Serial.println("╠═══════════════════════════════════════════════════════╣");
  Serial.printf("║  Build Date: %-40s ║\n", BUILD_DATE);
  Serial.printf("║  Build Time: %-40s ║\n", BUILD_TIME);
  Serial.println("║  Core: ESP32 @ 240MHz                                ║");
  Serial.println("║  Architecture: Xtensa LX6 Dual-Core                  ║");
  Serial.println("╚═══════════════════════════════════════════════════════╝");
  Serial.println();
  delay(500);
  
  Serial.println("┌─────────────────────────────────────────────────────┐");
  Serial.println("│ INITIALIZING SYSTEM MODULES...                      │");
  Serial.println("└─────────────────────────────────────────────────────┘");
  Serial.println();
}

// ============ REGISTER NODE 3 ============
void registerNode3Peer() {
  bool isDefault = true;
  for(int i=0; i<6; i++) {
    if(node3Address[i] != 0xFF) {
      isDefault = false;
      break;
    }
  }
  
  if(isDefault) {
    Serial.println("⚠️  Node 3 MAC not configured - Actuator control disabled");
    return;
  }
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, node3Address, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    node3Registered = true;
    Serial.println("✓ Node 3 (Actuator) registered");
  } else {
    Serial.println("❌ Failed to register Node 3");
  }
}

// ============ SEND ALERT TO NODE 3 ============
void sendAlertToNode3(const char* command, float magnitude) {
  if (!node3Registered) return;
  
  strcpy(alertToNode3.command, command);
  alertToNode3.magnitude = magnitude;
  alertToNode3.timestamp = millis();
  
  esp_err_t result = esp_now_send(node3Address, 
                                  (uint8_t *) &alertToNode3, 
                                  sizeof(alertToNode3));
  
  if (result == ESP_OK) {
    node3AlertsSent++;
    Serial.printf("📤 Alert to Node 3: %s (%.3f g)\n", command, magnitude);
  }
}

// ============ EARTHQUAKE DETECTION ============
void detectEarthquake() {
  float avgMagnitude = (node1Data.magnitude + node2Data.magnitude) / 2.0;
  
  bool node1Warning = node1Data.magnitude > warningThreshold;
  bool node2Warning = node2Data.magnitude > warningThreshold;
  bool node1Alert = node1Data.magnitude > earthquakeThreshold;
  bool node2Alert = node2Data.magnitude > earthquakeThreshold;
  
  if ((node1Warning || node2Warning) && avgMagnitude > warningThreshold) {
    if (!warningDetected) {
      warningDetected = true;
      publishAlert("WARNING", avgMagnitude);
      publishStatus();
    }
  } else {
    warningDetected = false;
  }
  
  if ((node1Alert && node2Alert) || (avgMagnitude > earthquakeThreshold)) {
    if (!earthquakeDetected) {
      earthquakeStartTime = millis();
    }
    
    if (millis() - earthquakeStartTime >= MIN_DETECTION_TIME) {
      if (!earthquakeDetected) {
        earthquakeDetected = true;
        Serial.println("\n🚨 EARTHQUAKE DETECTED!");
        sendAlertToNode3("EARTHQUAKE", avgMagnitude);
        publishAlert("EARTHQUAKE", avgMagnitude);
        publishStatus();
      }
    }
  } else {
    if (avgMagnitude < warningThreshold * 0.5) {
      if (earthquakeDetected) {
        Serial.println("\n✓ System Normal - Sending CLEAR");
        sendAlertToNode3("CLEAR", avgMagnitude);
        publishAlert("CLEAR", avgMagnitude);
        publishStatus();
      }
      earthquakeDetected = false;
      earthquakeStartTime = 0;
    }
  }
}

// ============ CHECK NODE STATUS ============
void checkNodeStatus() {
  unsigned long currentTime = millis();
  
  if (node1Connected && (currentTime - lastNode1Update > NODE_TIMEOUT)) {
    node1Connected = false;
    publishStatus();
  }
  
  if (node2Connected && (currentTime - lastNode2Update > NODE_TIMEOUT)) {
    node2Connected = false;
    publishStatus();
  }
}

// ============ MQTT FUNCTIONS ============
void reconnectMQTT() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();
  
  if (mqttClient.connect(mqtt_client_id)) {
    publishStatus();
  }
}

void publishNodeData(int nodeNum) {
  if (!mqttClient.connected()) return;
  
  char payload[200];
  const char* topic = (nodeNum == 1) ? topic_node1 : topic_node2;
  struct_sensor_message* data = (nodeNum == 1) ? &node1Data : &node2Data;
  
  snprintf(payload, sizeof(payload),
           "{\"node\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"mag\":%.3f}",
           data->nodeID, data->accelX, data->accelY, data->accelZ, data->magnitude);
  
  mqttClient.publish(topic, payload);
}

void publishStatus() {
  if (!mqttClient.connected()) return;
  
  char payload[150];
  float avgMag = 0;
  
  if (node1Connected && node2Connected) {
    avgMag = (node1Data.magnitude + node2Data.magnitude) / 2.0;
  } else if (node1Connected) {
    avgMag = node1Data.magnitude;
  } else if (node2Connected) {
    avgMag = node2Data.magnitude;
  }
  
  snprintf(payload, sizeof(payload),
           "{\"n1\":%d,\"n2\":%d,\"mag\":%.3f,\"alert\":%d}",
           node1Connected ? 1 : 0, node2Connected ? 1 : 0,
           avgMag, earthquakeDetected ? 1 : 0);
  
  mqttClient.publish(topic_status, payload);
}

void publishAlert(const char* level, float magnitude) {
  if (!mqttClient.connected()) return;
  
  char payload[150];
  snprintf(payload, sizeof(payload),
           "{\"level\":\"%s\",\"mag\":%.3f}",
           level, magnitude);
  
  mqttClient.publish(topic_alert, payload);
}

void blinkLED() {
  digitalWrite(LED_PIN, HIGH);
  delay(50);
  digitalWrite(LED_PIN, LOW);
}

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  delay(1000);
  bootTime = millis();
  
  printBootScreen();
  
  Serial.println("⏳ [1/7] Initializing WiFi module...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✓");
    Serial.printf("    IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("    Signal: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println(" ✗ FAILED");
  }
  
  Serial.println("⏳ [2/7] Connecting to MQTT broker...");
  mqttClient.setServer(mqtt_server, mqtt_port);
  Serial.printf("    Server: %s:%d ✓\n", mqtt_server, mqtt_port);
  
  Serial.println("⏳ [3/7] Initializing ESP-NOW protocol...");
  if (esp_now_init() == ESP_OK) {
    Serial.println("    ESP-NOW initialized ✓");
    Serial.printf("    Gateway MAC: %s\n", WiFi.macAddress().c_str());
  } else {
    Serial.println("    ESP-NOW FAILED ✗");
  }
  
  Serial.println("⏳ [4/7] Registering communication callbacks...");
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);
  Serial.println("    Callbacks registered ✓");
  
  Serial.println("⏳ [5/7] Registering actuator node (Node 3)...");
  registerNode3Peer();
  
  Serial.println("⏳ [6/7] Starting web server...");
  setupWebServer();
  
  Serial.println("⏳ [7/7] Initializing system monitors...");
  updateSystemStats();
  Serial.println("    Resource monitoring active ✓");
  
  Serial.println();
  Serial.println("╔═══════════════════════════════════════════════════════╗");
  Serial.println("║              SYSTEM INITIALIZATION COMPLETE           ║");
  Serial.println("╠═══════════════════════════════════════════════════════╣");
  Serial.printf("║  Dashboard: http://%-33s ║\n", WiFi.localIP().toString().c_str());
  Serial.println("║  Status: OPERATIONAL                                  ║");
  Serial.println("║  Detection: ACTIVE                                    ║");
  Serial.println("╚═══════════════════════════════════════════════════════╝");
  Serial.println();
  Serial.println("🟢 EARTHQUAKEOS READY - Monitoring seismic activity...\n");
}

// ============ MAIN LOOP ============
void loop() {
  server.handleClient();
  
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
  
  checkNodeStatus();
  
  if (node1Connected && node2Connected) {
    detectEarthquake();
  }
  
  if (millis() - lastStatsUpdate > 1000) {
    updateSystemStats();
    lastStatsUpdate = millis();
  }
  
  delay(10);
}
