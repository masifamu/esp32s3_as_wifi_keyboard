/*
 * ESP32-S3  —  WiFi Keyboard + Touchpad Passthrough
 * ───────────────────────────────────────────────────
 * Connect your laptop to the ESP32's WiFi AP, open 192.168.4.1.
 *
 * • Keyboard panel : type on the laptop → keystrokes go to the PC
 * • Touchpad panel : move / click / scroll on the laptop touchpad
 *                   inside the capture zone → mouse goes to the PC
 *
 * Board settings (Arduino IDE):
 *   Board    → ESP32S3 Dev Module
 *   USB Mode → USB-OTG (TinyUSB)
 *
 * Wiring: USB-C data cable from ESP32-S3 → PC
 */

#include <WiFi.h>
#include <WebServer.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"

const char* SSID     = "ESP32-KB";
const char* PASSWORD = "12345678";

WebServer      server(80);
USBHIDKeyboard Keyboard;
USBHIDMouse    Mouse;

// ── Mouse button state (tracked across requests) ─────────────────
uint8_t prevButtons = 0;

// ─────────────────────────────────────────────────────────────────
//  HTML — stored in flash (~6 KB)
// ─────────────────────────────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Passthrough</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&display=swap');
:root{
  --bg:#07080c;--panel:#0d0f16;--border:#1c2030;--acc:#39ff7e;
  --blue:#00b8ff;--warn:#ff6b35;--text:#b8c8d8;--dim:#2a3040;
  --btn-bg:#111520;--btn-border:#1e2535;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%;background:var(--bg);font-family:'IBM Plex Mono',monospace;color:var(--text)}
body{display:flex;flex-direction:column;align-items:center;justify-content:flex-start;padding:18px 12px 24px;gap:14px;min-height:100vh}

/* ── Header ── */
.hdr{text-align:center}
.hdr-title{font-size:11px;letter-spacing:7px;color:var(--acc);font-weight:600;text-transform:uppercase}
.hdr-sub{font-size:9px;letter-spacing:3px;color:#2a3040;margin-top:4px}

/* ── Two-column layout ── */
.cols{display:flex;gap:12px;width:min(900px,100%);align-items:flex-start;flex-wrap:wrap}
.col{flex:1;min-width:280px}

/* ── Panel card ── */
.card{background:var(--panel);border:1px solid var(--border);border-radius:10px;overflow:hidden}
.card-head{display:flex;align-items:center;justify-content:space-between;padding:9px 14px;border-bottom:1px solid var(--border)}
.card-title{font-size:9px;letter-spacing:4px;color:#3a4555;font-weight:600}
.pill{display:flex;align-items:center;gap:5px;font-size:9px;letter-spacing:2px;color:#2a3040}
.dot{width:6px;height:6px;border-radius:50%;background:#1a2030}
.dot.live{background:var(--acc);box-shadow:0 0 7px var(--acc);animation:blink 1.8s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.3}}
.dot.live-blue{background:var(--blue);box-shadow:0 0 7px var(--blue);animation:blink 1.8s infinite}

/* ── Log feed ── */
.log-wrap{padding:8px 10px 6px}
.log{height:130px;overflow-y:auto;display:flex;flex-direction:column-reverse;gap:2px;
     scrollbar-width:thin;scrollbar-color:#1c2030 transparent}
.entry{display:flex;align-items:center;gap:7px;padding:3px 6px;border-radius:3px;animation:sl .15s ease}
@keyframes sl{from{opacity:0;transform:translateY(-3px)}to{opacity:1;transform:none}}
.entry.ok{background:rgba(57,255,126,.03)}
.entry.err{background:rgba(255,107,53,.05)}
.ts{font-size:8px;color:#252f3e;min-width:44px}
.badge{font-size:9px;font-weight:600;padding:1px 5px;border-radius:2px}
.badge.ok{background:rgba(57,255,126,.12);color:var(--acc)}
.badge.err{background:rgba(255,107,53,.12);color:var(--warn)}
.badge.mv{background:rgba(0,184,255,.1);color:var(--blue)}
.kname{font-size:10px;flex:1;color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.rtt{font-size:9px;color:#252f3e;min-width:30px;text-align:right}

/* ── Footer strip ── */
.card-foot{padding:7px 14px;border-top:1px solid var(--border);
           display:flex;align-items:center;justify-content:space-between;font-size:9px;color:#2a3040}
.card-foot span{color:#3a4555}

/* ── Capture zone (keyboard) ── */
#kb-zone{margin:8px 10px;border:1px solid var(--border);border-radius:6px;
         outline:none;position:relative;min-height:42px;
         display:flex;align-items:center;justify-content:center;cursor:text}
#kb-zone:focus{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc),0 0 20px rgba(57,255,126,.07)}
.zone-hint{font-size:10px;letter-spacing:3px;color:#2a3040;pointer-events:none;padding:12px}
.mods{display:flex;gap:4px;padding:0 10px 8px}
.mod{font-size:8px;padding:2px 5px;border-radius:2px;border:1px solid var(--border);color:#252f3e;letter-spacing:1px;transition:all .1s}
.mod.on{border-color:var(--blue);color:var(--blue);background:rgba(0,184,255,.08)}

/* ── Touchpad capture area ── */
.tp-wrap{padding:10px}
#tp-zone{
  width:100%;aspect-ratio:16/9;max-height:180px;
  background:repeating-linear-gradient(0deg,transparent,transparent 24px,rgba(28,32,48,.5) 24px,rgba(28,32,48,.5) 25px),
             repeating-linear-gradient(90deg,transparent,transparent 24px,rgba(28,32,48,.5) 24px,rgba(28,32,48,.5) 25px),
             var(--btn-bg);
  border:1px solid var(--btn-border);
  border-radius:8px;cursor:crosshair;position:relative;overflow:hidden;
  display:flex;align-items:center;justify-content:center;
  user-select:none;-webkit-user-select:none;
}
#tp-zone.locked{border-color:var(--blue);box-shadow:0 0 0 1px var(--blue),0 0 24px rgba(0,184,255,.08);cursor:none}
#tp-zone.locked .tp-idle{display:none}
#tp-zone:not(.locked) .tp-active{display:none}
.tp-idle{font-size:10px;letter-spacing:3px;color:#2a3040;text-align:center;pointer-events:none}
.tp-idle div:first-child{font-size:18px;margin-bottom:6px;color:#1c2030}
.tp-active{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;pointer-events:none}
/* Crosshair ripple */
.ripple{width:20px;height:20px;border-radius:50%;border:1px solid var(--blue);opacity:.7;
        animation:rip 1s ease-out infinite}
@keyframes rip{0%{transform:scale(.5);opacity:.7}100%{transform:scale(2.5);opacity:0}}

/* ── Click buttons ── */
.tp-btns{display:flex;gap:6px;margin-top:8px}
.tp-btn{flex:1;height:32px;background:var(--btn-bg);border:1px solid var(--btn-border);
        border-bottom:2px solid #080a0f;border-radius:5px;cursor:pointer;
        font-family:inherit;font-size:9px;letter-spacing:2px;color:#3a4555;
        transition:all .08s;display:flex;align-items:center;justify-content:center}
.tp-btn:active,.tp-btn.pressing{background:#161a28;border-bottom-width:1px;
  transform:translateY(1px);color:var(--blue);border-color:var(--blue)}

/* ── Sensitivity slider ── */
.sens-row{display:flex;align-items:center;gap:8px;padding:6px 10px 4px;font-size:9px;color:#2a3040}
.sens-row input[type=range]{flex:1;accent-color:var(--blue);height:2px}
.sens-val{min-width:28px;text-align:right;color:#3a4555}

/* ── Overlay (click to activate) ── */
.overlay{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;
         gap:7px;background:rgba(7,8,12,.88);cursor:pointer;z-index:5;border-radius:5px;transition:opacity .2s}
.overlay.hidden{opacity:0;pointer-events:none}
.ov-icon{font-size:22px;color:#2a3040}
.ov-text{font-size:9px;letter-spacing:4px;color:var(--acc)}
.ov-sub{font-size:8px;color:#2a3040;letter-spacing:2px}
</style>
</head>
<body>

<div class="hdr">
  <div class="hdr-title">&#9658; ESP32-S3 PASSTHROUGH</div>
  <div class="hdr-sub">KEYBOARD &amp; TOUCHPAD &nbsp;·&nbsp; WIRELESS HID BRIDGE</div>
</div>

<div class="cols">

  <!-- ══════════════ KEYBOARD PANEL ══════════════ -->
  <div class="col">
    <div class="card">
      <div class="card-head">
        <span class="card-title">KEYBOARD</span>
        <div class="pill"><div class="dot" id="kb-dot"></div><span id="kb-status">IDLE</span></div>
      </div>

      <!-- Capture zone -->
      <div id="kb-zone" tabindex="0">
        <div class="overlay" id="kb-overlay">
          <div class="ov-icon">&#9000;</div>
          <div class="ov-text">Click to Activate</div>
          <div class="ov-sub">Then type normally</div>
        </div>
        <span class="zone-hint" id="kb-hint">CLICK ABOVE TO START</span>
      </div>

      <!-- Modifier indicators -->
      <div class="mods">
        <div class="mod" id="m-ctrl">CTRL</div>
        <div class="mod" id="m-alt">ALT</div>
        <div class="mod" id="m-shift">SHIFT</div>
        <div class="mod" id="m-win">WIN</div>
      </div>

      <div class="log-wrap">
        <div class="log" id="kb-log"></div>
      </div>

      <div class="card-foot">
        <span>TAB · F-KEYS · COMBOS FORWARDED</span>
        <span>lat: <span id="kb-lat">—</span></span>
      </div>
    </div>
  </div>

  <!-- ══════════════ TOUCHPAD PANEL ══════════════ -->
  <div class="col">
    <div class="card">
      <div class="card-head">
        <span class="card-title">TOUCHPAD / MOUSE</span>
        <div class="pill"><div class="dot" id="tp-dot"></div><span id="tp-status">IDLE</span></div>
      </div>

      <div class="tp-wrap">
        <!-- Touchpad surface -->
        <div id="tp-zone">
          <div class="tp-idle">
            <div>&#8645;</div>
            <div>CLICK TO CAPTURE MOUSE</div>
          </div>
          <div class="tp-active">
            <div class="ripple"></div>
          </div>
        </div>

        <!-- Sensitivity -->
        <div class="sens-row">
          <span>SENS</span>
          <input type="range" id="sens" min="10" max="200" value="60">
          <span class="sens-val" id="sens-val">1.0×</span>
        </div>

        <!-- Click buttons -->
        <div class="tp-btns">
          <button class="tp-btn" id="btn-left"  data-btn="1">&#9664; LEFT</button>
          <button class="tp-btn" id="btn-mid"   data-btn="4">MID</button>
          <button class="tp-btn" id="btn-right" data-btn="2">RIGHT &#9654;</button>
        </div>
      </div>

      <div class="log-wrap">
        <div class="log" id="tp-log"></div>
      </div>

      <div class="card-foot">
        <span>MOVE · LEFT/RIGHT/MID CLICK · SCROLL</span>
        <span>lat: <span id="tp-lat">—</span></span>
      </div>
    </div>
  </div>

</div><!-- /cols -->

<script>
// ══════════════════════════════════════════════════════════════════
//  KEYBOARD PASSTHROUGH
// ══════════════════════════════════════════════════════════════════
const kbZone    = document.getElementById('kb-zone');
const kbOverlay = document.getElementById('kb-overlay');
const kbDot     = document.getElementById('kb-dot');
const kbStatus  = document.getElementById('kb-status');
const kbHint    = document.getElementById('kb-hint');
const kbLog     = document.getElementById('kb-log');
const kbLat     = document.getElementById('kb-lat');

const MODIFIER_KEYS = new Set(['Control','Alt','Shift','Meta','AltGraph']);
const SKIP_KEYS     = new Set(['Unidentified']);

const KEY_MAP = {
  'Enter':'ENTER','Backspace':'BKSP','Tab':'TAB','Escape':'ESC',
  'Delete':'DEL','CapsLock':'CAPS','Insert':'INS',
  'Home':'HOME','End':'END','PageUp':'PGUP','PageDown':'PGDN',
  'ArrowUp':'UP','ArrowDown':'DOWN','ArrowLeft':'LEFT','ArrowRight':'RIGHT',
  ' ':'SPACE',
  'F1':'F1','F2':'F2','F3':'F3','F4':'F4','F5':'F5','F6':'F6',
  'F7':'F7','F8':'F8','F9':'F9','F10':'F10','F11':'F11','F12':'F12',
};

function mapKey(e) {
  const k = e.key;
  if (k.length === 1) return k;
  return KEY_MAP[k] || null;
}

// Serial queue — one request at a time
function makeQueue() {
  let busy = false, q = [];
  return {
    push(fn) { q.push(fn); if (!busy) drain(); },
  };
  function drain() {
    if (!q.length) { busy = false; return; }
    busy = true;
    q.shift()().finally(drain);
  }
}
const kbQueue = makeQueue();

function addLog(logEl, badge, badgeClass, label, rtt) {
  const ts  = new Date().toTimeString().slice(0,8);
  const el  = document.createElement('div');
  el.className = 'entry ' + (badgeClass === 'err' ? 'err' : 'ok');
  el.innerHTML =
    `<span class="ts">${ts}</span>` +
    `<span class="badge ${badgeClass}">${badge}</span>` +
    `<span class="kname">${label}</span>` +
    `<span class="rtt">${rtt != null ? rtt+'ms' : ''}</span>`;
  logEl.prepend(el);
  while (logEl.children.length > 50) logEl.removeChild(logEl.lastChild);
}

let kbActive = false;

function kbActivate() {
  kbActive = true;
  kbOverlay.classList.add('hidden');
  kbDot.className = 'dot live';
  kbStatus.textContent = 'CAPTURING';
  kbHint.textContent = 'TYPING IS BEING FORWARDED';
  kbZone.focus();
}
function kbDeactivate() {
  kbActive = false;
  kbOverlay.classList.remove('hidden');
  kbDot.className = 'dot';
  kbStatus.textContent = 'IDLE';
  kbHint.textContent = 'CLICK ABOVE TO START';
}

kbOverlay.addEventListener('click', kbActivate);
kbZone.addEventListener('blur',  kbDeactivate);
kbZone.addEventListener('focus', () => { if (!kbActive) kbActivate(); });

window.addEventListener('keydown', e => {
  const id = {Control:'m-ctrl',Alt:'m-alt',Shift:'m-shift',Meta:'m-win'}[e.key];
  if (id) document.getElementById(id).classList.add('on');
});
window.addEventListener('keyup', e => {
  const id = {Control:'m-ctrl',Alt:'m-alt',Shift:'m-shift',Meta:'m-win'}[e.key];
  if (id) document.getElementById(id).classList.remove('on');
});

kbZone.addEventListener('keydown', e => {
  if (e.key === 'Tab') e.preventDefault();
  if (e.ctrlKey && 'wtnr'.includes(e.key)) e.preventDefault();
  if (!kbActive || SKIP_KEYS.has(e.key) || MODIFIER_KEYS.has(e.key)) return;

  const keyCode = mapKey(e);
  if (!keyCode) return;

  const parts = [];
  if (e.ctrlKey)  parts.push('Ctrl');
  if (e.altKey)   parts.push('Alt');
  if (e.shiftKey && keyCode.length > 1) parts.push('Shift');
  if (e.metaKey)  parts.push('Win');
  const comboStr = parts.length ? parts.join('+')+'+' : '';
  const label = comboStr + keyCode;

  const params = new URLSearchParams({
    k: keyCode,
    ctrl:  e.ctrlKey  ? '1':'0',
    alt:   e.altKey   ? '1':'0',
    shift: e.shiftKey ? '1':'0',
    gui:   e.metaKey  ? '1':'0',
  });

  kbQueue.push(() => {
    const t0 = performance.now();
    return fetch('/key?' + params)
      .then(r => r.text())
      .then(() => {
        const rtt = Math.round(performance.now()-t0);
        kbLat.textContent = rtt+'ms';
        addLog(kbLog, 'OK', 'ok', label, rtt);
      })
      .catch(() => addLog(kbLog, 'ERR', 'err', label, null));
  });
});


// ══════════════════════════════════════════════════════════════════
//  TOUCHPAD / MOUSE PASSTHROUGH
// ══════════════════════════════════════════════════════════════════
const tpZone   = document.getElementById('tp-zone');
const tpDot    = document.getElementById('tp-dot');
const tpStatus = document.getElementById('tp-status');
const tpLog    = document.getElementById('tp-log');
const tpLat    = document.getElementById('tp-lat');
const sensEl   = document.getElementById('sens');
const sensVal  = document.getElementById('sens-val');

let tpLocked = false;

// ── Sensitivity ──────────────────────────────────────────────────
sensEl.addEventListener('input', () => {
  const v = (sensEl.value / 60).toFixed(1);
  sensVal.textContent = v + '×';
});

function getSens() { return sensEl.value / 60; }

// ── Movement accumulator (flushed at 60fps) ──────────────────────
let accDx = 0, accDy = 0;
let moveBusy = false;

setInterval(() => {
  if (!tpLocked || moveBusy) return;
  if (accDx === 0 && accDy === 0) return;

  const dx = Math.max(-127, Math.min(127, Math.round(accDx)));
  const dy = Math.max(-127, Math.min(127, Math.round(accDy)));
  accDx = 0; accDy = 0;

  moveBusy = true;
  fetch(`/mouse?dx=${dx}&dy=${dy}&lb=0&rb=0&mb=0&scroll=0`)
    .then(r => r.text())
    .then(t => {
      tpLat.textContent = ''; // skip latency for move — too noisy
    })
    .catch(() => {})
    .finally(() => { moveBusy = false; });
}, 16);

// ── Pointer Lock ──────────────────────────────────────────────────
tpZone.addEventListener('click', () => {
  if (!tpLocked) tpZone.requestPointerLock();
});

document.addEventListener('pointerlockchange', () => {
  tpLocked = document.pointerLockElement === tpZone;
  tpZone.classList.toggle('locked', tpLocked);
  tpDot.className = tpLocked ? 'dot live-blue' : 'dot';
  tpStatus.textContent = tpLocked ? 'CAPTURING' : 'IDLE';
  if (!tpLocked) { accDx = 0; accDy = 0; }
});

document.addEventListener('pointerlockerror', () => {
  tpStatus.textContent = 'LOCK ERROR';
  addLog(tpLog, 'ERR', 'err', 'Pointer lock failed — try HTTPS or another browser', null);
});

// ── Mouse movement ────────────────────────────────────────────────
document.addEventListener('mousemove', e => {
  if (!tpLocked) return;
  const s = getSens();
  accDx += e.movementX * s;
  accDy += e.movementY * s;
});

// ── Scroll wheel ──────────────────────────────────────────────────
const scrollQueue = makeQueue();
document.addEventListener('wheel', e => {
  if (!tpLocked) return;
  e.preventDefault();
  const scroll = e.deltaY > 0 ? -2 : 2; // -2=down, +2=up (HID convention)
  scrollQueue.push(() => {
    const t0 = performance.now();
    return fetch(`/mouse?dx=0&dy=0&lb=0&rb=0&mb=0&scroll=${scroll}`)
      .then(r => r.text())
      .then(() => {
        const rtt = Math.round(performance.now()-t0);
        tpLat.textContent = rtt+'ms';
        addLog(tpLog, 'SCR', 'mv', scroll > 0 ? '▲ SCROLL UP' : '▼ SCROLL DOWN', rtt);
      })
      .catch(() => addLog(tpLog, 'ERR', 'err', 'Scroll failed', null));
  });
}, { passive: false });

// ── Mouse buttons (via pointer lock) ─────────────────────────────
const btnNames = { 0:'LEFT', 1:'MIDDLE', 2:'RIGHT' };
const btnBits  = { 0:1,      1:4,        2:2 };

document.addEventListener('mousedown', e => {
  if (!tpLocked) return;
  const bit  = btnBits[e.button] || 0;
  const name = btnNames[e.button] || '?';
  sendClick(bit, 1, name + ' ↓');
});
document.addEventListener('mouseup', e => {
  if (!tpLocked) return;
  const bit  = btnBits[e.button] || 0;
  const name = btnNames[e.button] || '?';
  sendClick(bit, 0, name + ' ↑');
});

// Prevent context menu while locked
document.addEventListener('contextmenu', e => { if (tpLocked) e.preventDefault(); });

const clickQueue = makeQueue();
function sendClick(btn, pressed, label) {
  clickQueue.push(() => {
    const lb = btn===1 ? pressed : 0;
    const rb = btn===2 ? pressed : 0;
    const mb = btn===4 ? pressed : 0;
    const t0 = performance.now();
    return fetch(`/mouse?dx=0&dy=0&lb=${lb}&rb=${rb}&mb=${mb}&scroll=0&press=${pressed}`)
      .then(r => r.text())
      .then(() => {
        const rtt = Math.round(performance.now()-t0);
        tpLat.textContent = rtt+'ms';
        addLog(tpLog, pressed?'DN':'UP', 'mv', label, rtt);
      })
      .catch(() => addLog(tpLog, 'ERR', 'err', label + ' failed', null));
  });
}

// ── On-screen click buttons (send click + release) ───────────────
document.querySelectorAll('.tp-btn').forEach(btn => {
  const bit = parseInt(btn.dataset.btn);
  const name = btn.textContent.replace(/[◀▶]/g,'').trim();
  btn.addEventListener('mousedown', e => {
    e.stopPropagation(); // don't trigger tp-zone click
    btn.classList.add('pressing');
    sendClick(bit, 1, name + ' ↓');
  });
  btn.addEventListener('mouseup', e => {
    btn.classList.remove('pressing');
    sendClick(bit, 0, name + ' ↑');
  });
  btn.addEventListener('mouseleave', () => btn.classList.remove('pressing'));
});

// ── ESC to exit pointer lock ──────────────────────────────────────
// (browsers do this natively, but we add a note)
document.addEventListener('keydown', e => {
  if (e.key === 'Escape' && tpLocked) {
    document.exitPointerLock();
  }
});
</script>
</body></html>
)rawliteral";


// ─────────────────────────────────────────────────────────────────
//  /key  — keyboard handler
// ─────────────────────────────────────────────────────────────────
void handleKey() {
  if (!server.hasArg("k")) { server.send(400, "text/plain", "ERR:no key"); return; }

  String k     = server.arg("k");
  bool ctrl    = server.arg("ctrl")  == "1";
  bool alt     = server.arg("alt")   == "1";
  bool shift   = server.arg("shift") == "1";
  bool gui     = server.arg("gui")   == "1";

  if (ctrl)  Keyboard.press(KEY_LEFT_CTRL);
  if (alt)   Keyboard.press(KEY_LEFT_ALT);
  if (shift) Keyboard.press(KEY_LEFT_SHIFT);
  if (gui)   Keyboard.press(KEY_LEFT_GUI);

  if (k.length() == 1) {
    Keyboard.press((uint8_t)k[0]);
  } else {
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
    else if (k == "F1")     code = KEY_F1;  else if (k == "F2")  code = KEY_F2;
    else if (k == "F3")     code = KEY_F3;  else if (k == "F4")  code = KEY_F4;
    else if (k == "F5")     code = KEY_F5;  else if (k == "F6")  code = KEY_F6;
    else if (k == "F7")     code = KEY_F7;  else if (k == "F8")  code = KEY_F8;
    else if (k == "F9")     code = KEY_F9;  else if (k == "F10") code = KEY_F10;
    else if (k == "F11")    code = KEY_F11; else if (k == "F12") code = KEY_F12;
    if (code) Keyboard.press(code);
  }

  delay(40);
  Keyboard.releaseAll();
  server.send(200, "text/plain", "OK:" + k);
}


// ─────────────────────────────────────────────────────────────────
//  /mouse — mouse handler
// ─────────────────────────────────────────────────────────────────
void handleMouse() {
  int8_t dx     = (int8_t)constrain(server.arg("dx").toInt(),     -127, 127);
  int8_t dy     = (int8_t)constrain(server.arg("dy").toInt(),     -127, 127);
  int8_t scroll = (int8_t)constrain(server.arg("scroll").toInt(), -127, 127);
  bool   lb     = server.arg("lb") == "1";
  bool   rb     = server.arg("rb") == "1";
  bool   mb     = server.arg("mb") == "1";
  bool   press  = server.arg("press") == "1"; // 1=down, 0=up (for explicit clicks)

  // ── Buttons ────────────────────────────────────────────────────
  // Build desired button bitmask
  uint8_t desired = 0;
  if (lb) desired |= MOUSE_LEFT;
  if (rb) desired |= MOUSE_RIGHT;
  if (mb) desired |= MOUSE_MIDDLE;

  // Press newly-pressed buttons
  uint8_t toPress   = desired & ~prevButtons;
  uint8_t toRelease = prevButtons & ~desired;
  if (toPress   & MOUSE_LEFT)   Mouse.press(MOUSE_LEFT);
  if (toPress   & MOUSE_RIGHT)  Mouse.press(MOUSE_RIGHT);
  if (toPress   & MOUSE_MIDDLE) Mouse.press(MOUSE_MIDDLE);
  if (toRelease & MOUSE_LEFT)   Mouse.release(MOUSE_LEFT);
  if (toRelease & MOUSE_RIGHT)  Mouse.release(MOUSE_RIGHT);
  if (toRelease & MOUSE_MIDDLE) Mouse.release(MOUSE_MIDDLE);
  prevButtons = desired;

  // ── Movement + scroll ─────────────────────────────────────────
  if (dx != 0 || dy != 0 || scroll != 0) {
    Mouse.move(dx, dy, scroll);
  }

  server.send(200, "text/plain", "OK");
}


void handleRoot()     { server.send_P(200, "text/html", HTML); }
void handleNotFound() { server.send(404, "text/plain", "Not found"); }


// ─────────────────────────────────────────────────────────────────
//  Setup & Loop
// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // USB must init before WiFi
  USB.begin();
  Keyboard.begin();
  Mouse.begin();
  delay(1500);   // allow USB enumeration on the PC

  WiFi.softAP(SSID, PASSWORD);
  Serial.print("[WiFi] AP IP: ");
  Serial.println(WiFi.softAPIP());   // → 192.168.4.1

  server.on("/",      HTTP_GET, handleRoot);
  server.on("/key",   HTTP_GET, handleKey);
  server.on("/mouse", HTTP_GET, handleMouse);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[HTTP] Server ready.");
}

void loop() {
  server.handleClient();
}