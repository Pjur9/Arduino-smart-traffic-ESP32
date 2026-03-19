#include "WebServerManager.h"
#include <WiFi.h> // Obavezno za IPAddress
#ifdef ENABLE_MDNS
#include <ESPmDNS.h>
#endif
#ifdef ENABLE_OTA
#include <AsyncElegantOTA.h>
#endif

// ========== Wi-Fi (STA) - AUTOMATSKI DHCP ==========
bool WebServerManager::connectSTA(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  
  WiFi.begin(ssid, pass);

  Serial.printf("Connecting to WiFi (STA) SSID='%s'\n", ssid);
  const unsigned long connectStart = millis(); 
  const unsigned long TIMEOUT = 20000; // 20s

  while (WiFi.status() != WL_CONNECTED && millis() - connectStart < TIMEOUT) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("Adresa za pristup: http://" + WiFi.localIP().toString());
    
    // *** KLJUČNO: Šaljemo IP adresu u kontroler za prikaz na LCD-u ***
    if (trafficController) {
      trafficController->currentIP = WiFi.localIP().toString();
    }
    // ***************************************************************

    return true;
  } else {
    Serial.println("STA connect FAILED.");
    return false;
  }
}

void WebServerManager::begin(const char* ssid,
                             const char* pass,
                             TrafficController* controller,
                             const char* hostName) {
  trafficController = controller;
  if (hostName && *hostName) hostname = hostName;

  connectSTA(ssid, pass);
  tryStartMDNS();
  setupRoutes();

#ifdef ENABLE_OTA
  AsyncElegantOTA.begin(&server);
#endif

  server.begin();
  logEvent("SYSTEM BOOT: Web server started");
}

void WebServerManager::tick() {
  const unsigned long now = millis();
  if (now - lastConnCheck < WIFI_CHECK_PERIOD_MS) return;
  lastConnCheck = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.disconnect(false, false);
    WiFi.reconnect();
  }
}

void WebServerManager::tryStartMDNS() {
#ifdef ENABLE_MDNS
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(hostname.c_str())) {
      Serial.printf("mDNS responder started: http://%s.local\n", hostname.c_str());
      MDNS.addService("http", "tcp", 80);
    }
  }
#endif
}

// ========== JSON Helper ==========
String WebServerManager::buildTelemetryJson() const {
  if (!trafficController) return "{}";
  SystemState d = trafficController->getCurrentStateData();
  String json = "{";
  json += "\"mode\":\"" + d.mode + "\",";
  json += "\"light\":\"" + d.light + "\",";
  json += "\"ramp\":\"" + d.ramp + "\",";
  json += "\"vehicles\":" + String(d.vehicles) + ",";
  json += "\"t\":" + String(d.temperature, 1) + ",";
  json += "\"h\":" + String(d.humidity, 1) + ",";
  json += "\"p\":" + String(d.pressure, 1) + ",";
  json += "\"ldr\":" + String(d.ldr) + ",";
  
  // Energija
  json += "\"volts\":" + String(d.busVoltage, 2) + ",";
  json += "\"amps\":" + String(d.current_mA, 1) + ",";
  json += "\"watts\":" + String(d.power_mW, 1);

#if ULTRASONIC_ENABLED
  json += ",\"ultra_mm\":" + String(d.ultra_mm);
  json += ",\"congestion\":" + String(d.congestion);
#endif
  json += "}";
  return json;
}

// ========== LOGGING SISTEM ==========

void WebServerManager::logEvent(const String& msg) {
  unsigned long s = millis() / 1000;
  String timeStr = String(s / 60) + "m " + String(s % 60) + "s";
  String entry = "[" + timeStr + "] " + msg;

  logBuf[logHead] = entry;
  logHead = (logHead + 1) % LOG_SIZE;
  if (logCount < LOG_SIZE) logCount++;

  events.send(entry.c_str(), "log", millis());
  Serial.println(entry);
}

String WebServerManager::getLogHistoryHTML() const {
  String out = "";
  for (size_t i = 0; i < logCount; i++) {
    size_t idx = (logHead + LOG_SIZE - logCount + i) % LOG_SIZE;
    out += "<div class='log-line'>" + logBuf[idx] + "</div>";
  }
  return out;
}

// ========== HTML & UI ==========

String WebServerManager::handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <meta charset="utf-8">
  <title>Smart Semafor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --bg: #0f172a; --card: #1e293b; --text: #e2e8f0; --accent: #3b82f6; --green: #22c55e; --red: #ef4444; }
    body { font-family: 'Segoe UI', system-ui, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; }
    .container { max-width: 900px; margin: 0 auto; }
    h1 { text-align: center; font-weight: 300; letter-spacing: 1px; margin-bottom: 20px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 15px; margin-bottom: 20px; }
    .card { background: var(--card); padding: 15px; border-radius: 12px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1); border: 1px solid #334155; }
    .card h3 { margin: 0 0 10px 0; font-size: 0.9rem; text-transform: uppercase; color: #94a3b8; letter-spacing: 0.05em; }
    .data-row { display: flex; justify-content: space-between; margin-bottom: 5px; align-items: center; }
    .value { font-weight: bold; font-size: 1.1rem; }
    .status-pill { padding: 4px 10px; border-radius: 99px; font-size: 0.8rem; font-weight: 600; background: #334155; }
    .btn-group { display: flex; gap: 10px; flex-wrap: wrap; margin-top: 10px; }
    button { flex: 1; padding: 12px; border: none; border-radius: 8px; font-weight: 600; cursor: pointer; transition: 0.2s; color: white; }
    .btn-norm { background: var(--accent); } .btn-norm:hover { background: #2563eb; }
    .btn-warn { background: var(--red); } .btn-warn:hover { background: #dc2626; }
    .btn-safe { background: var(--green); } .btn-safe:hover { background: #16a34a; }
    .terminal { background: #000; border-radius: 8px; border: 1px solid #333; font-family: 'Consolas', monospace; padding: 10px; height: 250px; overflow-y: auto; font-size: 0.85rem; color: #0f0; display: flex; flex-direction: column; }
    .log-line { margin-bottom: 2px; border-bottom: 1px solid #111; padding-bottom: 2px; }
    .log-line:last-child { color: #fff; font-weight: bold; }
    footer { text-align: center; margin-top: 30px; color: #64748b; font-size: 0.8rem; }
  </style>
</head>
<body>
<div class="container">
  <h1>🚦 Smart Semafor Panel</h1>
  <div class="grid">
    <div class="card">
      <h3>Glavni Status</h3>
      <div class="data-row"><span>Režim:</span> <span id="mode" class="status-pill">---</span></div>
      <div class="data-row"><span>Svetlo:</span> <span id="light" class="value" style="color:var(--accent)">---</span></div>
      <div class="data-row"><span>Rampa:</span> <span id="ramp" class="value">---</span></div>
      <div class="data-row"><span>Vozila:</span> <span id="vehicles" class="value">0</span></div>
    </div>
    <div class="card">
      <h3>Senzori & Okolina</h3>
      <div class="data-row"><span>Temp:</span> <span id="temp" class="value">--</span> °C</div>
      <div class="data-row"><span>Vlaga:</span> <span id="hum" class="value">--</span> %</div>
      <div class="data-row"><span>LDR (Svetlo):</span> <span id="ldr" class="value">--</span></div>
      <div class="data-row"><span>Ultra Dist:</span> <span id="ultra" class="value">--</span> mm</div>
      <div class="data-row"><span>Gužva:</span> <span id="cong" class="value">--</span> %</div>
    </div>
    <div class="card">
      <h3>⚡ Potrošnja</h3>
      <div class="data-row"><span>Napon:</span> <span id="volts" class="value" style="color:#fbbf24">--</span> V</div>
      <div class="data-row"><span>Struja:</span> <span id="amps" class="value">--</span> mA</div>
      <div class="data-row"><span>Snaga:</span> <span id="watts" class="value">--</span> mW</div>
    </div>
    <div class="card">
      <h3>Upravljanje</h3>
      <div class="btn-group">
        <button class="btn-norm" onclick="cmd('ramp','open')">Rampa GORE</button>
        <button class="btn-norm" onclick="cmd('ramp','close')">Rampa DOLE</button>
      </div>
      <div class="btn-group">
        <button class="btn-warn" onclick="cmd('emerg','on')">HITNO STANJE (ON)</button>
        <button class="btn-safe" onclick="cmd('emerg','off')">NORMALNO (OFF)</button>
      </div>
    </div>
  </div>
  <div class="card">
    <h3>📋 Sistemski Log (Real-time)</h3>
    <div id="terminal" class="terminal">
)rawliteral";

  html += getLogHistoryHTML();

  html += R"rawliteral(
    </div>
  </div>
  <footer id="footer">ESP32 System Ready</footer>
</div>
<script>
  const els = {
    mode: document.getElementById('mode'), light: document.getElementById('light'),
    ramp: document.getElementById('ramp'), vehicles: document.getElementById('vehicles'),
    temp: document.getElementById('temp'), hum: document.getElementById('hum'),
    ldr: document.getElementById('ldr'), ultra: document.getElementById('ultra'),
    cong: document.getElementById('cong'), term: document.getElementById('terminal'),
    volts: document.getElementById('volts'), amps: document.getElementById('amps'), watts: document.getElementById('watts')
  };
  function autoScroll() { els.term.scrollTop = els.term.scrollHeight; }
  autoScroll();
  const es = new EventSource('/events');
  es.addEventListener('telemetry', e => {
    try {
      const d = JSON.parse(e.data);
      els.mode.innerText = d.mode;
      els.light.innerText = d.light;
      if(d.light === "RED") els.light.style.color = "#ef4444";
      else if(d.light === "GREEN") els.light.style.color = "#22c55e";
      else els.light.style.color = "#fbbf24";
      els.ramp.innerText = d.ramp;
      els.vehicles.innerText = d.vehicles;
      els.temp.innerText = d.t ? d.t.toFixed(1) : "--";
      els.hum.innerText = d.h ? d.h.toFixed(1) : "--";
      els.ldr.innerText = d.ldr;
      els.ultra.innerText = d.ultra_mm || "--";
      els.cong.innerText = d.congestion || "0";
      els.volts.innerText = d.volts ? d.volts.toFixed(2) : "0.00";
      els.amps.innerText = d.amps ? d.amps.toFixed(0) : "0";
      els.watts.innerText = d.watts ? d.watts.toFixed(0) : "0";
    } catch(err) { console.log('JSON Parse Err', err); }
  });
  es.addEventListener('log', e => {
    const div = document.createElement('div');
    div.className = 'log-line';
    div.innerText = e.data;
    els.term.appendChild(div);
    if(els.term.childElementCount > 100) els.term.firstChild.remove();
    autoScroll();
  });
  es.onerror = () => console.log("SSE Reconnecting...");
  function cmd(type, val) {
    fetch('/override?'+type+'='+val).then(r => r.text()).catch(e => console.error('CMD FAIL', e));
  }
  fetch('/ip').then(r=>r.text()).then(ip => document.getElementById('footer').innerText = "IP: " + ip + " | Connected");
</script>
</body>
</html>
)rawliteral";
  return html;
}

// ========== Routes Setup ==========

void WebServerManager::setupRoutes() {
  server.on("/", HTTP_GET, [this](AsyncWebServerRequest* req){
    req->send(200, "text/html", handleRoot());
  });
  events.onConnect([](AsyncEventSourceClient *client){
    client->send("connected", "log", millis());
  });
  server.addHandler(&events);
  server.on("/override", HTTP_GET, [this](AsyncWebServerRequest* req){
    String msg = "noop";
    if (req->hasParam("ramp")) {
      String v = req->getParam("ramp")->value();
      if (v == "open")  { trafficController->forceRamp(true);  msg = "CMD: Ramp OPEN"; }
      if (v == "close") { trafficController->forceRamp(false); msg = "CMD: Ramp CLOSE"; }
    }
    if (req->hasParam("emerg")) {
      String v = req->getParam("emerg")->value();
      if (v == "on")  { trafficController->setEmergency(true);  msg = "CMD: Emergency ON"; }
      if (v == "off") { trafficController->setEmergency(false); msg = "CMD: Emergency OFF"; }
    }
    logEvent(msg); 
    req->send(200, "text/plain", msg);
  });
  server.on("/ip", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/plain", WiFi.localIP().toString());
  });
}

void WebServerManager::sendSensorData() {
  if (!trafficController) return;
  events.send(buildTelemetryJson().c_str(), "telemetry", millis());
  String ev = trafficController->takeLastEvent();
  if (ev.length() > 0) logEvent(ev);
}