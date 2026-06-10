#include "web_task.h"
#include "uart_task.h"   // needed for xSensorMutex / gSensorData (/data endpoint)
#include <ESPAsyncWebServer.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char* TAG = "WEB";
static AsyncWebServer webServer(80);

static const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Control Panel</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700;900&display=swap');
  :root{
    --bg:#060b14;--sur:#0d1521;--sur2:#111d2e;--bor:#1a3350;
    --acc:#00d4ff;--acc2:#0099bb;--grn:#00ff88;--red:#ff3366;
    --warn:#ffaa00;--tx:#c8d8e8;--tx2:#4a6a8a;
  }
  *{margin:0;padding:0;box-sizing:border-box}
  body{
    background:var(--bg);color:var(--tx);
    font-family:'Share Tech Mono',monospace;
    min-height:100vh;
    background-image:
      radial-gradient(ellipse at 10% 20%,rgba(0,212,255,.07) 0%,transparent 50%),
      radial-gradient(ellipse at 90% 80%,rgba(0,255,136,.04) 0%,transparent 50%);
  }

  /* ── TOP BAR ── */
  .topbar{
    display:flex;align-items:center;justify-content:space-between;
    padding:16px 32px;border-bottom:1px solid var(--bor);
    background:rgba(13,21,33,.9);backdrop-filter:blur(10px);
    position:sticky;top:0;z-index:100;
  }
  .logo{font-family:'Orbitron',monospace;font-size:1.1rem;
    color:var(--acc);letter-spacing:4px}
  .logo span{color:var(--tx2);font-size:.7rem;display:block;letter-spacing:3px;margin-top:2px}
  .topbar-right{display:flex;align-items:center;gap:20px}
  .conn-dot{width:8px;height:8px;border-radius:50%;background:var(--grn);
    box-shadow:0 0 8px var(--grn);animation:pulse 2s infinite}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
  .conn-label{font-size:.7rem;color:var(--tx2);letter-spacing:1px}
  .clock{font-family:'Orbitron',monospace;font-size:.85rem;color:var(--acc);letter-spacing:2px}

  /* ── LAYOUT ── */
  .main{display:grid;grid-template-columns:280px 1fr;gap:0;min-height:calc(100vh - 57px)}

  /* ── SIDEBAR ── */
  .sidebar{
    border-right:1px solid var(--bor);padding:24px 20px;
    background:rgba(13,21,33,.5);
  }
  .sidebar-section{margin-bottom:28px}
  .sidebar-title{font-size:.65rem;color:var(--tx2);letter-spacing:3px;
    text-transform:uppercase;margin-bottom:14px;
    padding-bottom:8px;border-bottom:1px solid var(--bor)}

  /* Sensor cards */
  .sensor-card{
    background:var(--sur2);border:1px solid var(--bor);border-radius:10px;
    padding:16px;margin-bottom:10px;display:flex;align-items:center;gap:14px;
  }
  .sensor-icon{font-size:1.6rem;width:40px;text-align:center}
  .sensor-val{font-family:'Orbitron',monospace;font-size:1.5rem;color:var(--acc)}
  .sensor-lbl{font-size:.6rem;color:var(--tx2);letter-spacing:2px;margin-top:2px}

  /* Input status */
  .input-row{
    display:flex;align-items:center;justify-content:space-between;
    padding:10px 14px;background:var(--sur2);border:1px solid var(--bor);
    border-radius:8px;margin-bottom:8px;
  }
  .input-name{font-size:.75rem;letter-spacing:1px;color:var(--tx)}
  .input-badge{
    padding:3px 10px;border-radius:4px;font-size:.65rem;letter-spacing:2px;
    font-weight:bold;transition:all .3s;
  }
  .input-badge.high{background:rgba(0,255,136,.15);color:var(--grn);
    border:1px solid rgba(0,255,136,.3)}
  .input-badge.low{background:rgba(255,51,102,.1);color:var(--red);
    border:1px solid rgba(255,51,102,.2)}

  /* System info */
  .info-row{display:flex;justify-content:space-between;align-items:center;
    padding:8px 0;border-bottom:1px solid rgba(26,51,80,.5);font-size:.7rem}
  .info-row:last-child{border-bottom:none}
  .info-key{color:var(--tx2);letter-spacing:1px}
  .info-val{color:var(--acc);font-size:.75rem}

  /* ── CONTENT ── */
  .content{padding:28px 32px}
  .content-title{
    font-family:'Orbitron',monospace;font-size:.85rem;color:var(--tx2);
    letter-spacing:3px;margin-bottom:20px;display:flex;align-items:center;gap:10px
  }
  .content-title::after{content:'';flex:1;height:1px;background:var(--bor)}

  /* Relay grid */
  .relay-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:16px;margin-bottom:32px}

  .relay-card{
    background:var(--sur);border:1px solid var(--bor);border-radius:12px;
    padding:24px;position:relative;overflow:hidden;transition:border-color .3s;
  }
  .relay-card::before{
    content:'';position:absolute;top:0;left:0;right:0;height:2px;
    background:var(--red);transition:background .3s;
  }
  .relay-card.on{border-color:rgba(0,255,136,.3)}
  .relay-card.on::before{background:var(--grn)}

  .relay-header{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:20px}
  .relay-name{font-family:'Orbitron',monospace;font-size:.9rem;color:var(--tx);letter-spacing:2px}
  .relay-num{font-size:.65rem;color:var(--tx2);margin-top:3px;letter-spacing:1px}

  /* Big toggle */
  .toggle-wrap{display:flex;justify-content:center;margin:16px 0 20px}
  .toggle{position:relative;width:72px;height:36px;cursor:pointer}
  .toggle input{opacity:0;width:0;height:0}
  .toggle-slider{
    position:absolute;inset:0;background:rgba(255,51,102,.2);
    border:1px solid var(--red);border-radius:36px;transition:.3s;
  }
  .toggle-slider::before{
    content:'';position:absolute;width:28px;height:28px;
    left:3px;top:3px;background:var(--red);border-radius:50%;
    transition:.3s;box-shadow:0 0 8px rgba(255,51,102,.5);
  }
  .toggle input:checked + .toggle-slider{
    background:rgba(0,255,136,.15);border-color:var(--grn);
  }
  .toggle input:checked + .toggle-slider::before{
    transform:translateX(36px);background:var(--grn);
    box-shadow:0 0 12px rgba(0,255,136,.7);
  }

  .relay-status{text-align:center;font-size:.7rem;letter-spacing:2px;
    color:var(--red);transition:color .3s}
  .relay-status.on{color:var(--grn)}

  .relay-btn-row{display:flex;gap:8px;margin-top:16px}
  .rbtn{
    flex:1;padding:9px;border:1px solid;border-radius:7px;
    font-family:'Share Tech Mono',monospace;font-size:.72rem;
    cursor:pointer;background:transparent;transition:all .2s;letter-spacing:1px;
  }
  .rbtn-on{border-color:var(--grn);color:var(--grn)}
  .rbtn-on:hover{background:rgba(0,255,136,.1);box-shadow:0 0 12px rgba(0,255,136,.2)}
  .rbtn-off{border-color:var(--red);color:var(--red)}
  .rbtn-off:hover{background:rgba(255,51,102,.1);box-shadow:0 0 12px rgba(255,51,102,.2)}

  /* Log */
  .log-box{
    background:var(--sur);border:1px solid var(--bor);border-radius:10px;
    padding:16px;height:180px;overflow-y:auto;font-size:.72rem;line-height:1.8;
  }
  .log-box::-webkit-scrollbar{width:4px}
  .log-box::-webkit-scrollbar-track{background:transparent}
  .log-box::-webkit-scrollbar-thumb{background:var(--bor);border-radius:2px}
  .log-entry{display:flex;gap:10px}
  .log-time{color:var(--tx2);min-width:80px}
  .log-msg{color:var(--tx)}
  .log-msg.ok{color:var(--grn)}
  .log-msg.err{color:var(--red)}
  .log-msg.warn{color:var(--warn)}
</style>
</head>
<body>

<!-- TOP BAR -->
<div class="topbar">
  <div class="logo">ESP32 CONTROL
    <span>FREERTOS · RS485 · STM32</span>
  </div>
  <div class="topbar-right">
    <div class="conn-dot"></div>
    <span class="conn-label">CONNECTED</span>
    <div class="clock" id="clock">--:--:--</div>
  </div>
</div>

<!-- MAIN LAYOUT -->
<div class="main">

  <!-- SIDEBAR -->
  <div class="sidebar">

    <div class="sidebar-section">
      <div class="sidebar-title">Sensor Data</div>
      <div class="sensor-card">
        <div class="sensor-icon">🌡️</div>
        <div>
          <div class="sensor-val" id="temp">--.-</div>
          <div class="sensor-lbl">TEMPERATURE °C</div>
        </div>
      </div>
      <div class="sensor-card">
        <div class="sensor-icon">💧</div>
        <div>
          <div class="sensor-val" id="hum">--.-</div>
          <div class="sensor-lbl">HUMIDITY %</div>
        </div>
      </div>
    </div>

    <div class="sidebar-section">
      <div class="sidebar-title">Digital Input</div>
      <div class="input-row">
        <span class="input-name">IN 1</span>
        <span class="input-badge low" id="in1badge">LOW</span>
      </div>
      <div class="input-row">
        <span class="input-name">IN 2</span>
        <span class="input-badge low" id="in2badge">LOW</span>
      </div>
      <div class="input-row">
        <span class="input-name">IN 3</span>
        <span class="input-badge low" id="in3badge">LOW</span>
      </div>
      <div class="input-row">
        <span class="input-name">IN 4</span>
        <span class="input-badge low" id="in4badge">LOW</span>
      </div>
    </div>

    <div class="sidebar-section">
      <div class="sidebar-title">System Info</div>
      <div class="info-row">
        <span class="info-key">DEVICE</span>
        <span class="info-val">ESP32</span>
      </div>
      <div class="info-row">
        <span class="info-key">PROTOCOL</span>
        <span class="info-val">RS485</span>
      </div>
      <div class="info-row">
        <span class="info-key">BAUD</span>
        <span class="info-val">115200</span>
      </div>
      <div class="info-row">
        <span class="info-key">UPDATE</span>
        <span class="info-val">2s</span>
      </div>
      <div class="info-row">
        <span class="info-key">STATUS</span>
        <span class="info-val" id="statusVal">ONLINE</span>
      </div>
    </div>

  </div>

  <!-- CONTENT -->
  <div class="content">
    <!-- Relay Control section disabled — relay TX removed -->
    <!--
    <div class="content-title">Relay Control</div>
    <div class="relay-grid" id="relayGrid"></div>
    -->

    <div class="content-title">Activity Log</div>
    <div class="log-box" id="logBox"></div>
  </div>
</div>

<script>
const relayState = [false,false,false,false];

// ===== CLOCK =====
function updateClock(){
  const now = new Date();
  document.getElementById('clock').textContent =
    now.toTimeString().slice(0,8);
}
setInterval(updateClock, 1000);
updateClock();

// ===== LOG =====
function addLog(msg, type=''){
  const box = document.getElementById('logBox');
  const now  = new Date().toTimeString().slice(0,8);
  const div  = document.createElement('div');
  div.className = 'log-entry';
  div.innerHTML = `<span class="log-time">[${now}]</span>
                   <span class="log-msg ${type}">${msg}</span>`;
  box.appendChild(div);
  box.scrollTop = box.scrollHeight;
  // Giữ tối đa 50 dòng
  while(box.children.length > 50) box.removeChild(box.firstChild);
}

// ===== BUILD RELAY CARDS =====
function buildRelayGrid(){
  const g = document.getElementById('relayGrid');
  for(let i=1;i<=4;i++){
    const card = document.createElement('div');
    card.className = 'relay-card';
    card.id = 'rcard'+i;
    card.innerHTML = `
      <div class="relay-header">
        <div>
          <div class="relay-name">RELAY ${i}</div>
          <div class="relay-num">OUTPUT CHANNEL ${i}</div>
        </div>
      </div>
      <div class="toggle-wrap">
        <label class="toggle">
          <input type="checkbox" id="rtoggle${i}"
            onchange="relayToggle(${i},this.checked)">
          <span class="toggle-slider"></span>
        </label>
      </div>
      <div class="relay-status" id="rstatus${i}">OFF</div>
      <div class="relay-btn-row">
        <button class="rbtn rbtn-on"  onclick="relay(${i},1)">▶ ON</button>
        <button class="rbtn rbtn-off" onclick="relay(${i},0)">■ OFF</button>
      </div>`;
    g.appendChild(card);
  }
}

// ===== RELAY ACTIONS =====
function updateRelayUI(n, on){
  relayState[n-1] = on;
  const card   = document.getElementById('rcard'+n);
  const status = document.getElementById('rstatus'+n);
  const toggle = document.getElementById('rtoggle'+n);
  card.className   = 'relay-card' + (on?' on':'');
  status.className = 'relay-status' + (on?' on':'');
  status.textContent = on ? 'ON — ACTIVE' : 'OFF — IDLE';
  toggle.checked   = on;
}

function relay(n, state){
  fetch('/r'+n+(state?'on':'off'))
    .then(r=>{ if(r.ok){ updateRelayUI(n,!!state);
      addLog('Relay '+n+' → '+(state?'ON':'OFF'), state?'ok':'err'); }
    }).catch(()=>addLog('Relay '+n+' command failed','err'));
}

function relayToggle(n, checked){ relay(n, checked?1:0); }

// ===== FETCH DATA =====
function fetchData(){
  fetch('/data').then(r=>r.json()).then(d=>{
    document.getElementById('temp').textContent =
      d.valid ? d.temp.toFixed(1) : '--.-';
    document.getElementById('hum').textContent =
      d.valid ? d.hum.toFixed(1)  : '--.-';
    document.getElementById('statusVal').textContent = 'ONLINE';

    if(d.in){
      d.in.forEach((val,i)=>{
        const badge = document.getElementById('in'+(i+1)+'badge');
        badge.textContent = val ? 'HIGH' : 'LOW';
        badge.className   = 'input-badge '+(val?'high':'low');
      });
    }
  }).catch(()=>{
    document.getElementById('statusVal').textContent = 'ERROR';
  });
}

buildRelayGrid();
addLog('System initialized','ok');
addLog('Connecting to ESP32...','warn');
fetchData();
addLog('Data fetch started','ok');
setInterval(fetchData, 2000);
</script>
</body>
</html>
)rawhtml";

// ===== BUILD RELAY CMD — disabled (relay TX removed) =====
// static void buildRelayCmd(char* out, int relay, bool on) {
//     snprintf(out, CMD_MAX_LEN, "$CMD,R%d,%s#", relay, on ? "ON" : "OFF");
// }

// ===== SEND RELAY TO QUEUE — disabled =====
// static void sendRelay(int relay, bool on, AsyncWebServerRequest* request) {
//     RelayCmd_t cmd;
//     buildRelayCmd(cmd.cmd, relay, on);
//     if (xQueueSend(xRelayQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
//         request->send(200, "text/plain", "OK");
//     } else {
//         request->send(503, "text/plain", "Queue full");
//     }
// }

// ===== INIT WEB SERVER =====
void initWebServer() {
    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", HTML_PAGE);
    });

    // Relay control routes — disabled (relay TX removed)
    // webServer.on("/r1on",  HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(1,true, req); });
    // webServer.on("/r1off", HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(1,false,req); });
    // webServer.on("/r2on",  HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(2,true, req); });
    // webServer.on("/r2off", HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(2,false,req); });
    // webServer.on("/r3on",  HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(3,true, req); });
    // webServer.on("/r3off", HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(3,false,req); });
    // webServer.on("/r4on",  HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(4,true, req); });
    // webServer.on("/r4off", HTTP_GET, [](AsyncWebServerRequest* req){ sendRelay(4,false,req); });

    webServer.on("/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        static char jsonBuf[192];
        float t = 0, h = 0;
        bool  v = false;
        bool  inp[4] = {false};

        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(50))) {
            t = gSensorData.temp;
            h = gSensorData.hum;
            v = gSensorData.valid;
            for (int i = 0; i < 4; i++)
                inp[i] = gSensorData.input[i];
            xSemaphoreGive(xSensorMutex);
        }
        snprintf(jsonBuf, sizeof(jsonBuf),
                 "{\"temp\":%.1f,\"hum\":%.1f,\"valid\":%s,"
                 "\"in\":[%d,%d,%d,%d]}",
                 t, h, v ? "true" : "false",
                 inp[0], inp[1], inp[2], inp[3]);
        req->send(200, "application/json", jsonBuf);
    });

    webServer.onNotFound([](AsyncWebServerRequest* req){
        req->send(404, "text/plain", "Not found");
    });

    webServer.begin();
    ESP_LOGI(TAG, "Web server started");
}

void webServerTask(void* pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}