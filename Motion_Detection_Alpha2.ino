/*  Tap-Guard v6  —  ESP32 + MPU6050 drink-tamper alarm
 *  Wi-Fi SSID/Pass  →  DevilDogVIP / yvetteway
 *  Sensitivity: 0 (low) … 1 (high). 0.90 default.
 *  HTML: sans-serif, drop-shadows, SVG glass icon.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <time.h>

#define LED_PIN 2
const int    THR_MIN = 1;      // MPU6050 register units
const int    THR_MAX = 120;    // least sensitive
float sens = 0.90;             // user 0-1, 1 = hair-trigger
int   calcThr() { return max(THR_MIN, int(THR_MAX - sens * (THR_MAX - THR_MIN))); }

#define MOT_DUR 30             // 1 LSB = 1 ms
const char* ssid = "DevilDogVIP";
const char* pass = "yvetteway";

Adafruit_MPU6050 mpu;
WebServer server(80);

struct Ev { time_t ts; float ax,ay,az,gx,gy,gz; };
const int MAXEV = 350;
Ev  ev[MAXEV];
int head = -1;

bool moving = false;
uint32_t lastHit = 0;

/* ---------- INLINE HTML ---------- */
const char dashboard[] PROGMEM = R"html(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content=width=device-width,initial-scale=1>
<title>Tap-Guard</title>
<style>
body{margin:0;font-family:Helvetica,Arial,sans-serif;background:#eef1f3;color:#1d1d1f}
header{background:#fff;text-align:center;padding:20px;box-shadow:0 4px 10px #0002}
h1{margin:0;font-size:30px;text-shadow:0 2px 4px #0002}
#state{font-size:46px;font-weight:700;margin:14px 0;text-shadow:0 2px 4px #0002}
.armed{color:#34c759}.moving{color:#ff3b30}
.card{background:#fff;border-radius:14px;padding:14px;margin:10px auto;max-width:880px;
      box-shadow:0 6px 14px #0003}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{border:1px solid #ccc;padding:4px;text-align:center}
svg{filter:drop-shadow(0 2px 4px rgba(0,0,0,.25))}
button,input[type=number]{margin:6px;padding:8px 14px;border:0;border-radius:8px;
      font-weight:600;cursor:pointer}
button{background:#0071e3;color:#fff}
input[type=number]{width:110px;color:#6c6c6c}
input.clean{color:#1d1d1f}
</style>
</head><body>
<header><h1>Tap-Guard Coaster</h1></header>
<center>
<svg id=glass width=120 height=180>
 <rect x=30 y=10 width=60 height=160 rx=8 ry=8 fill="none" stroke="#555" stroke-width="4"/>
 <rect id=fill x=34 y=14 width=52 height=152 rx=4 ry=4 fill="#34c7f9"/>
</svg>
<div id=state class="armed">ARMED</div>
</center>

<div class=card>
 Sensitivity
 <input id=val type=number step=0.0005 min=0 max=1 placeholder="0.90">
 <button onclick="setSens()">Set</button>
 <button onclick="defSens()">Default</button>
 <button onclick="clrLog()">Clear Log</button>
 <button onclick="dlCSV()">CSV</button>
</div>

<div class=card>
<table><thead>
 <tr><th>ID</th><th>Time</th><th>Ax</th><th>Ay</th><th>Az</th><th>Gx</th><th>Gy</th><th>Gz</th></tr>
</thead><tbody id=log></tbody></table>
</div>

<script>
let csv="", box=document.getElementById("val");
box.oninput=()=>box.classList.add("clean");
const req=p=>fetch(p);

function setSens(){req("/sens?val="+(box.value||box.placeholder));}
function defSens(){req("/sens?val=0.90").then(()=>{box.value="";box.classList.remove("clean");});}
function clrLog(){req("/reset");}
function dlCSV(){const a=document.createElement("a");
 a.href=URL.createObjectURL(new Blob([csv],{type:"text/csv"}));
 a.download="tapguard.csv";a.click();}

/* poll 10 ×/s */
async function tick(){
 const st=await (await req("/state")).text();
 const div=document.getElementById("state"),glass=document.getElementById("fill");
 if(st==="1"){div.textContent="MOVING";div.className="moving";glass.setAttribute("fill","#ff3b30");}
 else        {div.textContent="ARMED"; div.className="armed"; glass.setAttribute("fill","#34c7f9");}

 csv=await (await req("/log")).text();
 const rows=csv.trim().split("\n").slice(1);         // skip header
 const tb=document.getElementById("log"); tb.innerHTML="";
 rows.forEach(line=>{
   const c=line.split(","); if(c.length<8) return;
   const tr=document.createElement("tr");
   c.forEach(v=>{const d=document.createElement("td");d.textContent=v;tr.appendChild(d);});
   tb.prepend(tr);                                   // newest on top
 });
 setTimeout(tick,100);
}
tick();
</script></body></html>
)html";
/* ---------- END HTML ---------- */

/* ---------- HTTP HANDLERS ---------- */
void hRoot()           { server.send_P(200,"text/html",dashboard); }
void hState()          { server.send(200,"text/plain", moving ? "1" : "0"); }
void hLog() {
  String out="id,time,ax,ay,az,gx,gy,gz\n";
  for(int i=0;i<MAXEV;i++){
    int idx=(head-i+MAXEV)%MAXEV; if(ev[idx].ts==0) break;
    char ts[20]; strftime(ts,sizeof(ts),"%F %T",localtime(&ev[idx].ts));
    out += String(idx)+","+ts+","+
           String(ev[idx].ax,2)+","+String(ev[idx].ay,2)+","+String(ev[idx].az,2)+","+
           String(ev[idx].gx,2)+","+String(ev[idx].gy,2)+","+String(ev[idx].gz,2)+"\n";
  }
  server.send(200,"text/csv",out);
}
void hSens() {
  sens = constrain(server.arg("val").toFloat(),0.0f,1.0f);
  int thr = calcThr();
  mpu.setMotionDetectionThreshold(thr);
  Serial.printf("New sensitivity %.3f → thr %d\n", sens, thr);
  server.send(200,"");
}
void hReset() {
  memset(ev,0,sizeof(ev)); head=-1; moving=false;
  Serial.println("Log cleared");
  server.send(200,"");
}
/* ----------------------------------- */

void setup(){
  pinMode(LED_PIN,OUTPUT);
  Serial.begin(115200); delay(100);

  WiFi.begin(ssid,pass);
  Serial.print("Wi-Fi…");
  while(WiFi.status()!=WL_CONNECTED){ delay(250); Serial.print('.'); }
  Serial.printf("\nURL  http://%s/\n", WiFi.localIP().toString().c_str());

  configTime(0,0,"pool.ntp.org","time.nist.gov");

  if(!mpu.begin()){ Serial.println("MPU6050 init fail"); while(true){} }
  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionDuration(MOT_DUR);
  mpu.setMotionDetectionThreshold(calcThr());
  mpu.setMotionInterrupt(true);

  server.on("/",hRoot);
  server.on("/state",hState);
  server.on("/log",hLog);
  server.on("/sens",hSens);
  server.on("/reset",hReset);
  server.begin();
}

void loop(){
  server.handleClient();

  if(mpu.getMotionInterruptStatus()){
    sensors_event_t a,g,t; mpu.getEvent(&a,&g,&t);

    head = (head + 1) % MAXEV;
    ev[head] = { time(nullptr),
                 a.acceleration.x, a.acceleration.y, a.acceleration.z,
                 g.gyro.x,        g.gyro.y,        g.gyro.z };

    Serial.printf("MOTION @%lu  ax %.2f ay %.2f az %.2f  thr %d\n",
                  ev[head].ts, ev[head].ax, ev[head].ay, ev[head].az, calcThr());

    digitalWrite(LED_PIN, HIGH);
    moving = true;
    lastHit = millis();
  }

  if(moving && millis() - lastHit > 1200){
    moving = false;
    digitalWrite(LED_PIN, LOW);
  }
}
