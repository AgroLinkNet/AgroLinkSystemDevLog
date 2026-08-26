/*
  =============================================================
   RECEIVER — ESP32-C3 SuperMini + DX-LR02 + Wi-Fi AP + SSE
  =============================================================

  BEHAVIOUR
  ---------
  - Listens for LoRa packets from the Sender over UART.
  - Creates its own Wi-Fi access point ("ESP32-LoRa-Receiver").
  - Hosts a web page at http://192.168.4.1 that updates INSTANTLY
    the moment a packet arrives — no polling, no page refresh.
    Updates are pushed from the ESP32 to the browser using
    Server-Sent Events (SSE), so they appear in real time.
  - The page shows the latest packet number prominently plus a
    scrollable history log with timestamps from the browser's
    own clock (real wall-clock time, no NTP needed).

  WIRING
  ------
  DX-LR02               ESP32-C3 SuperMini
  --------              ------------------
  VCC            -----> 3V3
  GND            -----> GND
  UART_TX        -----> GPIO4    (ESP receives here)
  UART_RX        -----> GPIO5    (ESP transmits here)
  M0, M1, AUX         leave unconnected

  HOW TO USE
  ----------
  1. Upload this sketch.
  2. On your phone or laptop, connect to Wi-Fi network:
       Network : ESP32-LoRa-Receiver
       Password: lora1234
  3. Open your browser and go to:  http://192.168.4.1
  4. Press the button on the Sender — the page updates instantly.

  ARDUINO IDE SETTINGS
  --------------------
  Board            : ESP32C3 Dev Module  (or "Nologo ESP32C3 Super Mini")
  USB CDC On Boot  : Enabled

  LIBRARIES NEEDED  (Tools → Manage Libraries)
  --------------------------------------------
  - ESPAsyncWebServer  (by lacamera)
  - AsyncTCP           (by dvarrel)
    Both are available directly in the Arduino Library Manager.
    Install AsyncTCP first, then ESPAsyncWebServer.
*/

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// ── Access point credentials ──────────────────────────────────────
const char* AP_SSID     = "ESP32-LoRa-Receiver";
const char* AP_PASSWORD = "";   // min 8 chars; use "" for open network

// ── Pin assignments ────────────────────────────────────────────────
#define LORA_RX_PIN  4    // ESP RX ← DX-LR02 UART_TX
#define LORA_TX_PIN  5    // ESP TX → DX-LR02 UART_RX
#define LORA_BAUD    9600

// ── Globals ───────────────────────────────────────────────────────
HardwareSerial   LoRaSerial(1);
AsyncWebServer   server(80);
AsyncEventSource events("/events");

// ── HTML page (stored in flash) ───────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LoRa Receiver</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: system-ui, -apple-system, sans-serif;
  background: #0d0d0d;
  color: #e5e5e5;
  min-height: 100vh;
}

/* ── Header ── */
header {
  background: #111;
  border-bottom: 1px solid #1c1c1c;
  padding: 14px 20px;
  display: flex;
  align-items: center;
  gap: 10px;
  position: sticky;
  top: 0;
  z-index: 10;
}
.dot {
  width: 9px; height: 9px;
  border-radius: 50%;
  background: #22c55e;
  box-shadow: 0 0 10px #22c55e99;
  flex-shrink: 0;
  transition: background 0.4s, box-shadow 0.4s;
}
.dot.off {
  background: #ef4444;
  box-shadow: 0 0 10px #ef444499;
}
header h1 {
  font-size: 0.95em;
  font-weight: 500;
  color: #999;
  letter-spacing: 0.03em;
}

/* ── Main ── */
.main {
  padding: 20px;
  max-width: 580px;
  margin: 0 auto;
}

/* ── Latest card ── */
.card {
  background: #141414;
  border: 1px solid #1e1e1e;
  border-radius: 16px;
  padding: 32px 24px 28px;
  text-align: center;
  margin-bottom: 20px;
}
.card-label {
  font-size: 0.68em;
  color: #555;
  text-transform: uppercase;
  letter-spacing: 0.14em;
  margin-bottom: 16px;
}
.big-num {
  font-size: 5.5em;
  font-weight: 800;
  color: #7dd3fc;
  line-height: 1;
  font-variant-numeric: tabular-nums;
  transition: color 0.15s;
}
.big-num.flash { color: #fff; }
.recv-time {
  font-size: 0.85em;
  color: #555;
  margin-top: 12px;
  font-variant-numeric: tabular-nums;
  min-height: 1.2em;
}

/* ── History log ── */
.log-label {
  font-size: 0.68em;
  color: #555;
  text-transform: uppercase;
  letter-spacing: 0.14em;
  margin-bottom: 10px;
}
.log {
  background: #141414;
  border: 1px solid #1e1e1e;
  border-radius: 16px;
  overflow: hidden;
  max-height: 340px;
  overflow-y: auto;
}
.log-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 18px;
  border-bottom: 1px solid #1a1a1a;
  animation: slideIn 0.22s ease;
}
.log-row:last-child { border-bottom: none; }
@keyframes slideIn {
  from { opacity: 0; transform: translateY(-7px); }
  to   { opacity: 1; transform: translateY(0); }
}
.log-pkt  { font-weight: 600; color: #7dd3fc; font-size: 0.93em; }
.log-time { font-size: 0.80em; color: #555; font-variant-numeric: tabular-nums; }
.empty    { padding: 36px; text-align: center; color: #383838; font-size: 0.88em; }

/* ── Status badge ── */
.status {
  position: fixed;
  bottom: 14px; right: 16px;
  font-size: 0.70em;
  transition: color 0.4s;
  color: #333;
}
.status.live { color: #22c55e; }
.status.dead { color: #ef4444; }
</style>
</head>
<body>

<header>
  <div class="dot" id="dot"></div>
  <h1>LoRa Receiver &mdash; Live</h1>
</header>

<div class="main">
  <div class="card">
    <div class="card-label">Last packet received</div>
    <div class="big-num" id="big-num">&#8212;</div>
    <div class="recv-time" id="recv-time">Waiting for first packet&hellip;</div>
  </div>

  <div class="log-label">History</div>
  <div class="log" id="log">
    <div class="empty" id="empty-msg">No packets yet.</div>
  </div>
</div>

<div class="status" id="status-el">Connecting&hellip;</div>

<script>
const MAX_LOG = 60;

const bigEl    = document.getElementById('big-num');
const timeEl   = document.getElementById('recv-time');
const logEl    = document.getElementById('log');
const statusEl = document.getElementById('status-el');
const dotEl    = document.getElementById('dot');

function fmt(d) {
  // Use the browser's local clock for real wall-clock timestamps
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function onPacket(num) {
  const now = new Date();
  const t = fmt(now);

  // Update the big display with a brief flash
  bigEl.textContent = '#' + num;
  bigEl.classList.add('flash');
  setTimeout(() => bigEl.classList.remove('flash'), 180);
  timeEl.textContent = 'Received at ' + t;

  // Prepend to history log
  const emptyMsg = document.getElementById('empty-msg');
  if (emptyMsg) emptyMsg.remove();

  // Cap log at MAX_LOG entries
  while (logEl.children.length >= MAX_LOG) {
    logEl.removeChild(logEl.lastChild);
  }

  const row = document.createElement('div');
  row.className = 'log-row';
  row.innerHTML =
    '<span class="log-pkt">Packet #' + num + '</span>' +
    '<span class="log-time">' + t + '</span>';
  logEl.insertBefore(row, logEl.firstChild);
}

// ── Server-Sent Events connection ─────────────────────────────────
// The browser keeps one persistent connection open. The ESP32
// pushes an event the instant a packet arrives — no polling needed.
const es = new EventSource('/events');

es.addEventListener('packet', function(e) {
  const num = parseInt(e.data, 10);
  if (!isNaN(num)) onPacket(num);
});

es.onopen = function() {
  statusEl.textContent = '● Live';
  statusEl.className   = 'status live';
  dotEl.classList.remove('off');
};

es.onerror = function() {
  statusEl.textContent = '● Disconnected — retrying…';
  statusEl.className   = 'status dead';
  dotEl.classList.add('off');
  // EventSource retries automatically; no extra code needed
};
</script>
</body>
</html>
)rawliteral";

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  LoRaSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  // Start access point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println("\n=== LoRa Receiver ===");
  Serial.print  ("Network  : "); Serial.println(AP_SSID);
  Serial.print  ("Password : "); Serial.println(AP_PASSWORD);
  Serial.print  ("Open in browser : http://");
  Serial.println(WiFi.softAPIP());

  // Serve the main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  // SSE endpoint — each browser tab connects here and receives
  // pushed events whenever a LoRa packet arrives
  events.onConnect([](AsyncEventSourceClient* client) {
    Serial.println("Browser connected to SSE stream.");
  });
  server.addHandler(&events);
  server.begin();

  Serial.println("Web server started. Listening for LoRa packets...");
}

// ── Loop ──────────────────────────────────────────────────────────
void loop() {
  if (LoRaSerial.available()) {
    String line = LoRaSerial.readStringUntil('\n');
    line.trim();

    // Expect packets in the format "PKT:N" sent by the Sender sketch
    if (line.startsWith("PKT:")) {
      String numStr = line.substring(4);   // everything after "PKT:"
      uint32_t num  = (uint32_t)numStr.toInt();
      if (num > 0) {
        // Push the packet number to ALL connected browsers instantly
        events.send(numStr.c_str(), "packet", millis());
        Serial.println("Received + pushed: PKT:" + numStr);
      }
    }
  }
}
