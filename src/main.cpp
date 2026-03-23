/*
 * ESP32-S3 WiFi HID Keyboard
 * --------------------------
 * The ESP32-S3 creates a WiFi hotspot. Connect your laptop to it,
 * open 192.168.4.1 in a browser, and click keys. The ESP32-S3
 * forwards every keystroke to the PC via USB-HID.
 *
 * Wiring:  USB-C (or USB_D+/D- pins) → PC
 * Library: Built-in ESP32 Arduino USB HID
 */

#include <WiFi.h>
#include <WebServer.h>
#include "USB.h"
#include "USBHIDKeyboard.h"

// ── WiFi credentials ─────────────────────────────────────────────
const char* SSID     = "ESP32-KB";
const char* PASSWORD = "12345678";

WebServer      server(80);
USBHIDKeyboard Keyboard;

// ── Web page (stored in flash, ~3 KB) ────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Keyboard</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&display=swap');
  :root{--bg:#0d0d0f;--kb:#161618;--key:#1e1e24;--keyt:#b0b8c8;--bdr:#2a2a35;--bot:#0a0a0c;--acc:#00e5ff;--accd:#007a8a;--sh:rgba(0,0,0,.6)}
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;font-family:'Share Tech Mono',monospace;color:var(--keyt)}
  header{text-align:center;margin-bottom:18px}
  header h1{font-size:13px;letter-spacing:6px;color:var(--acc);text-transform:uppercase}
  header p{font-size:10px;letter-spacing:2px;color:#444;margin-top:4px}
  #kb{background:var(--kb);padding:14px 16px;border-radius:12px;border:1px solid #222;box-shadow:0 20px 60px rgba(0,0,0,.8),inset 0 1px 0 #2a2a35}
  .row{display:flex;gap:4px;margin-bottom:4px}
  .spacer{display:inline-block}
  /* ── Key base ── */
  .k{background:var(--key);color:var(--keyt);border:1px solid var(--bdr);border-bottom:3px solid var(--bot);border-radius:5px;cursor:pointer;font-family:inherit;font-size:10px;height:38px;min-width:36px;display:inline-flex;align-items:center;justify-content:center;text-align:center;line-height:1.2;padding:0 5px;user-select:none;transition:background .08s,transform .08s,border-bottom-width .08s;box-shadow:0 2px 4px var(--sh)}
  .k:hover{background:#252530;color:#fff;border-color:#3a3a48}
  .k.press{background:var(--acc);color:#000;border-bottom-width:1px;transform:translateY(2px);box-shadow:none}
  /* ── Width variants ── */
  .w56{min-width:56px}.w64{min-width:64px}.w84{min-width:84px}
  .w90{min-width:90px}.w108{min-width:108px}.w56{min-width:56px}
  .w52{min-width:52px}.w220{min-width:220px}
  /* ── Status bar ── */
  #bar{margin-top:12px;font-size:11px;letter-spacing:2px;height:18px;color:#555;transition:color .3s}
  #bar.ok{color:var(--acc)}
  #bar.err{color:#ff4444}
</style></head>
<body>
<header>
  <h1>&#9000; ESP32-S3 Keyboard</h1>
  <p>WIRELESS HID BRIDGE</p>
</header>
<div id="kb"></div>
<div id="bar">READY</div>

<script>
// Layout: [{l:label, k:key_code, c:extra_css_class}] | null = spacer gap
const ROWS=[
  // ── Function row ─────────────────────────────────────────────────
  [{l:'Esc',k:'ESC'},{l:'',k:'',s:14},
   {l:'F1',k:'F1'},{l:'F2',k:'F2'},{l:'F3',k:'F3'},{l:'F4',k:'F4'},{l:'',k:'',s:10},
   {l:'F5',k:'F5'},{l:'F6',k:'F6'},{l:'F7',k:'F7'},{l:'F8',k:'F8'},{l:'',k:'',s:10},
   {l:'F9',k:'F9'},{l:'F10',k:'F10'},{l:'F11',k:'F11'},{l:'F12',k:'F12'}],
  // ── Number row ───────────────────────────────────────────────────
  [{l:'`',k:'`'},{l:'1',k:'1'},{l:'2',k:'2'},{l:'3',k:'3'},{l:'4',k:'4'},
   {l:'5',k:'5'},{l:'6',k:'6'},{l:'7',k:'7'},{l:'8',k:'8'},{l:'9',k:'9'},
   {l:'0',k:'0'},{l:'-',k:'-'},{l:'=',k:'='},{l:'&#9003;Bksp',k:'BKSP',c:'w90'}],
  // ── QWERTY row ───────────────────────────────────────────────────
  [{l:'Tab',k:'TAB',c:'w56'},{l:'Q',k:'q'},{l:'W',k:'w'},{l:'E',k:'e'},
   {l:'R',k:'r'},{l:'T',k:'t'},{l:'Y',k:'y'},{l:'U',k:'u'},{l:'I',k:'i'},
   {l:'O',k:'o'},{l:'P',k:'p'},{l:'[',k:'['},{l:']',k:']'},{l:'\\',k:'BK'}],
  // ── ASDF row ─────────────────────────────────────────────────────
  [{l:'Caps',k:'CAPS',c:'w64'},{l:'A',k:'a'},{l:'S',k:'s'},{l:'D',k:'d'},
   {l:'F',k:'f'},{l:'G',k:'g'},{l:'H',k:'h'},{l:'J',k:'j'},{l:'K',k:'k'},
   {l:'L',k:'l'},{l:';',k:';'},{l:"'",k:'SQ'},{l:'&#9166; Enter',k:'ENTER',c:'w84'}],
  // ── ZXCV row ─────────────────────────────────────────────────────
  [{l:'&#8679; Shift',k:'LSHIFT',c:'w108'},{l:'Z',k:'z'},{l:'X',k:'x'},
   {l:'C',k:'c'},{l:'V',k:'v'},{l:'B',k:'b'},{l:'N',k:'n'},{l:'M',k:'m'},
   {l:',',k:','},{l:'.',k:'.'},{l:'/',k:'/'},{l:'&#8679; Shift',k:'RSHIFT',c:'w84'}],
  // ── Bottom row ───────────────────────────────────────────────────
  [{l:'Ctrl',k:'LCTRL',c:'w52'},{l:'&#9678; Win',k:'WIN',c:'w52'},
   {l:'Alt',k:'LALT',c:'w52'},{l:'Space',k:'SPACE',c:'w220'},
   {l:'Alt',k:'RALT',c:'w52'},{l:'Ctrl',k:'RCTRL',c:'w52'},
   {l:'&#9664;',k:'LEFT'},{l:'&#9650;',k:'UP'},{l:'&#9660;',k:'DOWN'},{l:'&#9654;',k:'RIGHT'}]
];

const kb=document.getElementById('kb');
const bar=document.getElementById('bar');

ROWS.forEach(row=>{
  const d=document.createElement('div');
  d.className='row';
  row.forEach(key=>{
    // Gap spacer
    if(key.s){
      const g=document.createElement('div');
      g.className='spacer';
      g.style.width=key.s+'px';
      d.appendChild(g); return;
    }
    const b=document.createElement('div');
    b.className='k'+(key.c?' '+key.c:'');
    b.innerHTML=key.l;
    b.addEventListener('mousedown',()=>b.classList.add('press'));
    b.addEventListener('mouseup',()=>b.classList.remove('press'));
    b.addEventListener('mouseleave',()=>b.classList.remove('press'));
    b.addEventListener('click',()=>sendKey(key.k,key.l));
    d.appendChild(b);
  });
  kb.appendChild(d);
});

function sendKey(k,label){
  if(!k)return;
  bar.className='';
  bar.textContent='SENDING...';
  fetch('/key?k='+encodeURIComponent(k))
    .then(r=>r.text())
    .then(t=>{bar.className='ok';bar.textContent=t;})
    .catch(()=>{bar.className='err';bar.textContent='CONNECTION ERROR';});
}
</script>
</body></html>
)rawliteral";


// ── Key dispatcher ────────────────────────────────────────────────
void handleKey() {
  if (!server.hasArg("k")) {
    server.send(400, "text/plain", "Missing param");
    return;
  }

  String k = server.arg("k");

  // ── Printable single chars (letters, digits, symbols) ───────────
  if (k.length() == 1) {
    char c = k[0];
    Keyboard.press(c);
    delay(50);
    Keyboard.releaseAll();
    String msg = "SENT: "; msg += c;
    server.send(200, "text/plain", msg);
    return;
  }

  // ── Special keys ────────────────────────────────────────────────
  uint8_t code = 0;
  String  label = k;

  if      (k == "ENTER")   { code = KEY_RETURN;     label = "ENTER"; }
  else if (k == "BKSP")    { code = KEY_BACKSPACE;  label = "BACKSPACE"; }
  else if (k == "TAB")     { code = KEY_TAB;        label = "TAB"; }
  else if (k == "ESC")     { code = KEY_ESC;        label = "ESCAPE"; }
  else if (k == "CAPS")    { code = KEY_CAPS_LOCK;  label = "CAPS LOCK"; }
  else if (k == "DEL")     { code = KEY_DELETE;     label = "DELETE"; }
  else if (k == "UP")      { code = KEY_UP_ARROW;   label = "UP"; }
  else if (k == "DOWN")    { code = KEY_DOWN_ARROW; label = "DOWN"; }
  else if (k == "LEFT")    { code = KEY_LEFT_ARROW; label = "LEFT"; }
  else if (k == "RIGHT")   { code = KEY_RIGHT_ARROW;label = "RIGHT"; }
  else if (k == "LSHIFT")  { code = KEY_LEFT_SHIFT; label = "L-SHIFT"; }
  else if (k == "RSHIFT")  { code = KEY_RIGHT_SHIFT;label = "R-SHIFT"; }
  else if (k == "LCTRL")   { code = KEY_LEFT_CTRL;  label = "L-CTRL"; }
  else if (k == "RCTRL")   { code = KEY_RIGHT_CTRL; label = "R-CTRL"; }
  else if (k == "LALT")    { code = KEY_LEFT_ALT;   label = "L-ALT"; }
  else if (k == "RALT")    { code = KEY_RIGHT_ALT;  label = "R-ALT"; }
  else if (k == "WIN")     { code = KEY_LEFT_GUI;   label = "WIN"; }
  else if (k == "SPACE")   { code = ' ';            label = "SPACE"; }
  else if (k == "BK")      { code = '\\';           label = "BACKSLASH"; } // backslash key
  else if (k == "SQ")      { code = '\'';           label = "QUOTE"; }     // single-quote key
  else if (k == "F1")      { code = KEY_F1;         label = "F1"; }
  else if (k == "F2")      { code = KEY_F2;         label = "F2"; }
  else if (k == "F3")      { code = KEY_F3;         label = "F3"; }
  else if (k == "F4")      { code = KEY_F4;         label = "F4"; }
  else if (k == "F5")      { code = KEY_F5;         label = "F5"; }
  else if (k == "F6")      { code = KEY_F6;         label = "F6"; }
  else if (k == "F7")      { code = KEY_F7;         label = "F7"; }
  else if (k == "F8")      { code = KEY_F8;         label = "F8"; }
  else if (k == "F9")      { code = KEY_F9;         label = "F9"; }
  else if (k == "F10")     { code = KEY_F10;        label = "F10"; }
  else if (k == "F11")     { code = KEY_F11;        label = "F11"; }
  else if (k == "F12")     { code = KEY_F12;        label = "F12"; }

  if (code != 0) {
    Keyboard.press(code);
    delay(50);
    Keyboard.releaseAll();
    String msg = "SENT: "; msg += label;
    server.send(200, "text/plain", msg);
  } else {
    server.send(400, "text/plain", "UNKNOWN KEY: " + k);
  }
}

void handleRoot() {
  server.send_P(200, "text/html", HTML);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}


// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // USB must be started before WiFi
  USB.begin();
  Keyboard.begin();

  // Short delay to allow USB enumeration on the PC
  delay(1500);

  // Start WiFi access point
  WiFi.softAP(SSID, PASSWORD);
  Serial.print("[WiFi] AP started. Connect to: ");
  Serial.println(SSID);
  Serial.print("[WiFi] Open browser at: http://");
  Serial.println(WiFi.softAPIP());

  // Register routes
  server.on("/",    HTTP_GET, handleRoot);
  server.on("/key", HTTP_GET, handleKey);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[HTTP] Server started.");
}


// ── Loop ──────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
}