/*
 * ESP32-S3  —  WiFi Keyboard Passthrough
 * ───────────────────────────────────────
 * Connect your laptop to the ESP32's WiFi AP, open 192.168.4.1,
 * and type normally. Every keystroke is captured in the browser
 * and forwarded to the PC that the ESP32 is plugged into via USB-HID.
 *
 * Supports: all printable chars, F1-F12, arrows, modifiers (Ctrl/Alt/Shift/Win),
 *           combinations like Ctrl+C, Ctrl+Z, Alt+Tab, etc.
 *
 * Board settings (Arduino IDE):
 *   Board   → ESP32S3 Dev Module
 *   USB Mode → USB-OTG (TinyUSB)
 */

#include <WiFi.h>
#include <WebServer.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

const char* SSID     = "ESP32-KB";
const char* PASSWORD = "12345678";

WebServer      server(80);
USBHIDKeyboard Keyboard;

// ─────────────────────────────────────────────────────────────────
//  HTML page — stored in flash
// ─────────────────────────────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Passthrough</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&display=swap');
  :root{--bg:#080a0f;--panel:#0d1117;--border:#1e2736;--acc:#39ff7e;--acc2:#00b8ff;--dim:#334;--text:#c8d8e8;--warn:#ff6b35}
  *{box-sizing:border-box;margin:0;padding:0}
  html,body{height:100%;background:var(--bg);font-family:'IBM Plex Mono',monospace;color:var(--text)}
  body{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:16px;padding:20px}

  /* ── Header ── */
  header{text-align:center}
  .title{font-size:11px;letter-spacing:8px;color:var(--acc);text-transform:uppercase;font-weight:600}
  .sub{font-size:10px;letter-spacing:3px;color:#334;margin-top:5px}

  /* ── Capture zone ── */
  #zone{
    position:relative;
    width:min(620px,100%);
    background:var(--panel);
    border:1px solid var(--border);
    border-radius:10px;
    overflow:hidden;
    outline:none;
  }
  #zone:focus{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc),0 0 30px rgba(57,255,126,.08)}
  #zone-inner{padding:18px 20px 14px}

  /* ── Status strip ── */
  .strip{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}
  .pill{display:flex;align-items:center;gap:6px;font-size:10px;letter-spacing:2px;color:#556}
  .dot{width:7px;height:7px;border-radius:50%;background:#223;transition:background .2s}
  .dot.live{background:var(--acc);box-shadow:0 0 8px var(--acc);animation:pulse 1.6s infinite}
  @keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
  .lat{font-size:10px;color:#445;letter-spacing:1px}
  .lat span{color:var(--acc2)}

  /* ── Log feed ── */
  #log{
    height:200px;overflow-y:auto;display:flex;flex-direction:column-reverse;
    gap:3px;padding:4px 0;
    scrollbar-width:thin;scrollbar-color:#1e2736 transparent
  }
  .entry{display:flex;align-items:center;gap:10px;padding:4px 8px;border-radius:4px;animation:slide .15s ease}
  @keyframes slide{from{opacity:0;transform:translateY(-4px)}to{opacity:1;transform:none}}
  .entry.ok{background:rgba(57,255,126,.04)}
  .entry.err{background:rgba(255,107,53,.06)}
  .ts{font-size:9px;color:#334;min-width:48px}
  .badge{font-size:10px;font-weight:600;padding:1px 6px;border-radius:3px;letter-spacing:.5px}
  .badge.ok{background:rgba(57,255,126,.15);color:var(--acc)}
  .badge.err{background:rgba(255,107,53,.15);color:var(--warn)}
  .kname{font-size:11px;flex:1;color:var(--text)}
  .kname .combo{color:#556;font-size:10px;margin-left:4px}
  .rtt{font-size:10px;color:#334}

  /* ── Hint bar ── */
  .hint{border-top:1px solid var(--border);padding:10px 20px;font-size:10px;color:#334;letter-spacing:1px;display:flex;justify-content:space-between}
  .hint b{color:#556}

  /* ── Mod indicators ── */
  .mods{display:flex;gap:6px}
  .mod{font-size:9px;padding:2px 6px;border-radius:3px;border:1px solid #1e2736;color:#334;letter-spacing:1px;transition:all .1s}
  .mod.on{border-color:var(--acc2);color:var(--acc2);background:rgba(0,184,255,.1)}

  /* ── Click-to-focus overlay ── */
  #overlay{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;background:rgba(8,10,15,.92);cursor:pointer;z-index:10;transition:opacity .2s}
  #overlay.hidden{opacity:0;pointer-events:none}
  .overlay-icon{font-size:32px}
  .overlay-text{font-size:11px;letter-spacing:4px;color:var(--acc);text-transform:uppercase}
  .overlay-sub{font-size:10px;color:#334;letter-spacing:2px}
</style>
</head>
<body>

<header>
  <div class="title">&#9658; ESP32-S3 Passthrough</div>
  <div class="sub">TYPE ON THIS LAPTOP &nbsp;·&nbsp; KEYSTROKES GO TO THE PC</div>
</header>

<div id="zone" tabindex="0">
  <!-- click-to-focus overlay -->
  <div id="overlay">
    <div class="overlay-icon">&#9000;</div>
    <div class="overlay-text">Click to Activate</div>
    <div class="overlay-sub">Then type normally</div>
  </div>

  <div id="zone-inner">
    <div class="strip">
      <div class="pill">
        <div class="dot" id="dot"></div>
        <span id="status">IDLE — CLICK TO ACTIVATE</span>
      </div>
      <div class="mods">
        <div class="mod" id="m-ctrl">CTRL</div>
        <div class="mod" id="m-alt">ALT</div>
        <div class="mod" id="m-shift">SHIFT</div>
        <div class="mod" id="m-win">WIN</div>
      </div>
    </div>

    <div id="log"></div>

    <div class="hint">
      <span><b>TAB / ARROWS / F-KEYS</b> all forwarded</span>
      <span class="lat">latency: <span id="lat">—</span></span>
    </div>
  </div>
</div>

<script>
const zone    = document.getElementById('zone');
const overlay = document.getElementById('overlay');
const dot     = document.getElementById('dot');
const status  = document.getElementById('status');
const log     = document.getElementById('log');
const latEl   = document.getElementById('lat');
const mods    = {ctrl:'m-ctrl',alt:'m-alt',shift:'m-shift',win:'m-win'};

// ── Keys to skip (don't forward) ─────────────────────────────────
const SKIP = new Set(['Unidentified']);
// Modifier keys alone are tracked for the UI but not forwarded as isolated presses.
const MODIFIER_KEYS = new Set(['Control','Alt','Shift','Meta','AltGraph']);

// ── Map browser key → ESP32 key code string ──────────────────────
function mapKey(e) {
  const k = e.key;
  // Printable single char — send as-is (HID lib handles case/symbols)
  if (k.length === 1) return k;
  // Special keys
  const MAP = {
    'Enter':'ENTER','Backspace':'BKSP','Tab':'TAB','Escape':'ESC',
    'Delete':'DEL','CapsLock':'CAPS','Insert':'INS',
    'Home':'HOME','End':'END','PageUp':'PGUP','PageDown':'PGDN',
    'ArrowUp':'UP','ArrowDown':'DOWN','ArrowLeft':'LEFT','ArrowRight':'RIGHT',
    'F1':'F1','F2':'F2','F3':'F3','F4':'F4',
    'F5':'F5','F6':'F6','F7':'F7','F8':'F8',
    'F9':'F9','F10':'F10','F11':'F11','F12':'F12',
    ' ':'SPACE',
  };
  return MAP[k] || null;
}

// ── Build display label for the log ──────────────────────────────
function comboLabel(e, keyCode) {
  const parts = [];
  if (e.ctrlKey)  parts.push('Ctrl');
  if (e.altKey)   parts.push('Alt');
  if (e.shiftKey && keyCode.length > 1) parts.push('Shift'); // skip shift label for plain chars
  if (e.metaKey)  parts.push('Win');
  return parts.length ? parts.join('+') + '+' : '';
}

// ── Log entry ─────────────────────────────────────────────────────
function addLog(label, combo, rtt, ok) {
  const now = new Date();
  const ts  = now.toTimeString().slice(0,8);
  const el  = document.createElement('div');
  el.className = 'entry ' + (ok ? 'ok' : 'err');
  el.innerHTML =
    `<span class="ts">${ts}</span>` +
    `<span class="badge ${ok?'ok':'err'}">${ok?'OK':'ERR'}</span>` +
    `<span class="kname">${label}<span class="combo">${combo}</span></span>` +
    `<span class="rtt">${rtt != null ? rtt+'ms' : ''}</span>`;
  // Prepend (log shows newest at top via column-reverse)
  log.prepend(el);
  // Keep at most 60 entries
  while (log.children.length > 60) log.removeChild(log.lastChild);
}

// ── Send key to ESP32 ─────────────────────────────────────────────
const queue   = [];
let   sending = false;

function enqueue(params, label, combo) {
  queue.push({params, label, combo});
  drain();
}

function drain() {
  if (sending || queue.length === 0) return;
  sending = true;
  const {params, label, combo} = queue.shift();
  const t0 = performance.now();
  fetch('/key?' + params)
    .then(r => r.text())
    .then(() => {
      const rtt = Math.round(performance.now() - t0);
      addLog(label, combo, rtt, true);
      latEl.textContent = rtt + 'ms';
    })
    .catch(() => addLog(label, combo, null, false))
    .finally(() => { sending = false; drain(); });
}

// ── Active / focus state ──────────────────────────────────────────
let active = false;

function activate() {
  active = true;
  overlay.classList.add('hidden');
  dot.classList.add('live');
  status.textContent = 'CAPTURING — TYPE NOW';
  zone.focus();
}

function deactivate() {
  active = false;
  overlay.classList.remove('hidden');
  dot.classList.remove('live');
  status.textContent = 'IDLE — CLICK TO ACTIVATE';
}

overlay.addEventListener('click', activate);
zone.addEventListener('blur', deactivate);
zone.addEventListener('focus', () => { if (!active) activate(); });

// ── Mod-key highlight ─────────────────────────────────────────────
function setMod(key, on) {
  const id = {Control:'m-ctrl',Alt:'m-alt',Shift:'m-shift',Meta:'m-win'}[key];
  if (id) document.getElementById(id).classList.toggle('on', on);
}
window.addEventListener('keydown', e => setMod(e.key, true));
window.addEventListener('keyup',   e => setMod(e.key, false));

// ── Main keydown handler ──────────────────────────────────────────
zone.addEventListener('keydown', e => {
  // Always prevent tab from leaving the zone
  if (e.key === 'Tab') e.preventDefault();
  // Prevent Ctrl+W, Ctrl+T browser shortcuts while active
  if (e.ctrlKey && 'wtnr'.includes(e.key)) e.preventDefault();

  if (!active) return;
  if (SKIP.has(e.key)) return;
  if (MODIFIER_KEYS.has(e.key)) return; // modifier alone — just highlight

  const keyCode = mapKey(e);
  if (!keyCode) return;

  const params = new URLSearchParams({
    k:     keyCode,
    ctrl:  e.ctrlKey  ? '1' : '0',
    alt:   e.altKey   ? '1' : '0',
    shift: e.shiftKey ? '1' : '0',
    gui:   e.metaKey  ? '1' : '0',
  }).toString();

  const combo = comboLabel(e, keyCode);
  const label = keyCode.length === 1 ? keyCode : keyCode;

  enqueue(params, label, combo);
});
</script>
</body></html>
)rawliteral";


// ─────────────────────────────────────────────────────────────────
//  Key handler
// ─────────────────────────────────────────────────────────────────
void handleKey() {
  if (!server.hasArg("k")) { server.send(400, "text/plain", "ERR:no key"); return; }

  String k     = server.arg("k");
  bool ctrl    = server.arg("ctrl")  == "1";
  bool alt     = server.arg("alt")   == "1";
  bool shift   = server.arg("shift") == "1";
  bool gui     = server.arg("gui")   == "1";

  // Press modifiers first
  if (ctrl)  Keyboard.press(KEY_LEFT_CTRL);
  if (alt)   Keyboard.press(KEY_LEFT_ALT);
  if (shift) Keyboard.press(KEY_LEFT_SHIFT);
  if (gui)   Keyboard.press(KEY_LEFT_GUI);

  // Press the key itself
  if (k.length() == 1) {
    // Printable char — let HID handle case/symbols
    Keyboard.press((uint8_t)k[0]);
  } else {
    // Look up special key
    uint8_t code = 0;
    if      (k == "ENTER")  code = KEY_RETURN;
    else if (k == "BKSP")   code = KEY_BACKSPACE;
    else if (k == "TAB")    code = KEY_TAB;
    else if (k == "ESC")    code = KEY_ESC;
    else if (k == "CAPS")   code = KEY_CAPS_LOCK;
    else if (k == "DEL")    code = KEY_DELETE;
    else if (k == "INS")    code = KEY_INSERT;
    else if (k == "HOME")   code = KEY_HOME;
    else if (k == "END")    code = KEY_END;
    else if (k == "PGUP")   code = KEY_PAGE_UP;
    else if (k == "PGDN")   code = KEY_PAGE_DOWN;
    else if (k == "UP")     code = KEY_UP_ARROW;
    else if (k == "DOWN")   code = KEY_DOWN_ARROW;
    else if (k == "LEFT")   code = KEY_LEFT_ARROW;
    else if (k == "RIGHT")  code = KEY_RIGHT_ARROW;
    else if (k == "SPACE")  code = ' ';
    else if (k == "F1")     code = KEY_F1;
    else if (k == "F2")     code = KEY_F2;
    else if (k == "F3")     code = KEY_F3;
    else if (k == "F4")     code = KEY_F4;
    else if (k == "F5")     code = KEY_F5;
    else if (k == "F6")     code = KEY_F6;
    else if (k == "F7")     code = KEY_F7;
    else if (k == "F8")     code = KEY_F8;
    else if (k == "F9")     code = KEY_F9;
    else if (k == "F10")    code = KEY_F10;
    else if (k == "F11")    code = KEY_F11;
    else if (k == "F12")    code = KEY_F12;

    if (code) Keyboard.press(code);
  }

  delay(40);
  Keyboard.releaseAll();

  server.send(200, "text/plain", "OK:" + k);
}

void handleRoot() { server.send_P(200, "text/html", HTML); }
void handleNotFound() { server.send(404, "text/plain", "Not found"); }


// ─────────────────────────────────────────────────────────────────
//  Setup & Loop
// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // USB must init before WiFi
  USB.begin();
  Keyboard.begin();
  delay(1500); // allow USB enumeration on the PC

  WiFi.softAP(SSID, PASSWORD);
  Serial.print("[WiFi] AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/",    HTTP_GET, handleRoot);
  server.on("/key", HTTP_GET, handleKey);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Server ready.");
}

void loop() {
  server.handleClient();
}