/* Tap-Guard v5.1 */
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <time.h>

#define LED_PIN 2
float sens = 0.30;                   // 0-1  higher = more sensitive
int calcThr(){ return max(1, int(25 - sens * 24)); }
#define MOT_DUR 30
const char* ssid = "DevilDogVIP";
const char* pass = "yvetteway";

Adafruit_MPU6050 mpu;
WebServer server(80);

struct Ev{time_t ts; float ax,ay,az,gx,gy,gz;};
const int MAX = 300;
Ev ev[MAX]; int head = -1;
bool moving = false; uint32_t lastHit = 0;

/* ---------- UI ---------- */
const char page[] PROGMEM = R"html(
<!doctype html><html><head><meta charset=utf-8><meta name=viewport content=width=device-width,initial-scale=1>
<title>Tap-Guard</title><style>
body{margin:0;font-family:-apple-system,Helvetica,Arial;background:#eef1f3;color:#1d1d1f}
header{background:#fff;text-align:center;padding:20px;box-shadow:0 2px 6px #0003}
h1{margin:0;font-size:28px}#state{font-size:50px;font-weight:700;margin:16px 0}
.armed{color:#34c759}.moving{color:#ff3b30}
.card{background:#fff;border-radius:14px;padding:12px;margin:8px auto;max-width:820px;box-shadow:0 4px 10px #0002}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{border:1px solid #ccc;padding:4px;text-align:center}
button,input[type=number]{margin:6px;padding:8px 14px;border:0;border-radius:8px;font-weight:600;cursor:pointer}
button{background:#0071e3;color:#fff}input[type=number]{width:100px;color:#6c6c6c}
input.clean{color:#1d1d1f}
</style></head><body>
<header><h1>Tap-Guard Coaster</h1></header>
<center><div id=state class=armed>ARMED</div></center>
<div class=card>
 Sensitivity <input id=val type=number step=0.001 min=0 max=1 placeholder="0.30">
 <button onclick="setS()">Set</button>
 <button onclick="defS()">Default</button>
 <button onclick="clr()">Reset Log</button>
 <button onclick="csv()">CSV</button>
</div>
<div class=card>
<table><thead><tr><th>id</th><th>time</th><th>ax</th><th>ay</th><th>az</th><th>gx</th><th>gy</th><th>gz</th></tr></thead><tbody id=log></tbody></table>
</div>
<script>
let data="";const v=document.getElementById("val");v.oninput=()=>v.classList.add("clean");
const q=p=>fetch(p);
function setS(){q("/sens?val="+(v.value||v.placeholder));}
function defS(){q("/sens?val=0.30").then(()=>{v.value="";v.classList.remove("clean");});}
function clr(){q("/reset");}
function csv(){const a=document.createElement("a");a.href=URL.createObjectURL(new Blob([data],{type:"text/csv"}));a.download="tapguard.csv";a.click();}
async function loop(){
 const s=await (await q("/state")).text(),st=document.getElementById("state");
 st.textContent=s=="1"?"MOVING":"ARMED";st.className=s=="1"?"moving":"armed";
 data=await (await q("/log")).text();
 const rows=data.trim().split("\\n").slice(1),tb=document.getElementById("log");tb.innerHTML="";
 rows.forEach(l=>{const c=l.split(",");if(c.length<8)return;
  const tr=document.createElement("tr");c.forEach(v=>{const d=document.createElement("td");d.textContent=v;tr.appendChild(d);});tb.prepend(tr);});
 setTimeout(loop,250);}loop();
</script></body></html>
)html";
/* ------------------------ */

/* Handlers */
void handleRoot()      { server.send_P(200,"text/html",page); }
void handleState()     { server.send(200,"text/plain", moving ? "1" : "0"); }
void handleLog(){
  String out="id,time,ax,ay,az,gx,gy,gz\n";
  for(int i=0;i<MAX;i++){
    int idx=(head-i+MAX)%MAX; if(ev[idx].ts==0) break;
    char ts[20]; strftime(ts,sizeof(ts),"%F %T",localtime(&ev[idx].ts));
    out+=String(idx)+","+ts+","+
         String(ev[idx].ax,2)+","+String(ev[idx].ay,2)+","+String(ev[idx].az,2)+","+
         String(ev[idx].gx,2)+","+String(ev[idx].gy,2)+","+String(ev[idx].gz,2)+"\n";
  }
  server.send(200,"text/csv",out);
}
void handleSens(){
  sens = constrain(server.arg("val").toFloat(),0.0f,1.0f);
  mpu.setMotionDetectionThreshold(calcThr());
  server.send(200,"");
}
void handleReset(){
  memset(ev,0,sizeof(ev)); head=-1; moving=false;
  server.send(200,"");
}

/* Setup */
void setup(){
  pinMode(LED_PIN,OUTPUT); Serial.begin(115200);
  WiFi.begin(ssid,pass); while(WiFi.status()!=WL_CONNECTED) delay(200);
  configTime(0,0,"pool.ntp.org","time.nist.gov");

  if(!mpu.begin()){Serial.println("MPU fail"); while(1);}
  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionDuration(MOT_DUR);
  mpu.setMotionDetectionThreshold(calcThr());
  mpu.setMotionInterrupt(true);

  server.on("/",handleRoot);
  server.on("/state",handleState);
  server.on("/log",handleLog);
  server.on("/sens",handleSens);
  server.on("/reset",handleReset);
  server.begin();
}

/* Loop */
void loop(){
  server.handleClient();
  if(mpu.getMotionInterruptStatus()){
    sensors_event_t a,g,t; mpu.getEvent(&a,&g,&t);
    head=(head+1)%MAX;
    ev[head]={time(nullptr),a.acceleration.x,a.acceleration.y,a.acceleration.z,
                           g.gyro.x,g.gyro.y,g.gyro.z};
    digitalWrite(LED_PIN,HIGH); moving=true; lastHit=millis();
  }
  if(moving && millis()-lastHit>1200){ moving=false; digitalWrite(LED_PIN,LOW); }
}
