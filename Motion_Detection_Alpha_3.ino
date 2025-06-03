/*  Tap-Guard v7 – sleek beer-glass UI
 *  SSID/PASS → DevilDogVIP / yvetteway
 *  Slider 1-10 (10 = most sensitive).  LED + web alert when armed & motion.
 */
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <time.h>

#define LED_PIN 2
const int THR_MIN = 1;   // most sensitive
const int THR_MAX = 180; // least sensitive
int  level = 10;         // 1-10 GUI value
bool armed = false;
bool alertOn = false;
uint32_t lastMove = 0;

Adafruit_MPU6050 mpu;
WebServer server(80);

int thrVal() {                      // map 1-10 → MPU threshold
  return THR_MAX - (level-1) * ((THR_MAX-THR_MIN)/9.0f);
}

/* ===== HTML UI ===== */
const char html[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content=width=device-width,initial-scale=1>
<title>Tap-Guard</title>
<style>
html,body{margin:0;height:100%;font:italic 28px Helvetica,Arial,sans-serif;background:#121212;color:#fafafa;display:flex;flex-direction:column;align-items:center;justify-content:flex-start}
h1{margin-top:24px;letter-spacing:1px;opacity:.15;font-size:48px;position:absolute;top:20px;z-index:-1;width:100%;text-align:center}
#glass{margin-top:80px}
svg{filter:drop-shadow(0 4px 6px rgba(0,0,0,.4))}
#alert{margin:18px 0;font-size:34px;font-weight:bold;transition:.2s}
.btn{padding:14px 28px;border:none;border-radius:12px;font:600 20px Helvetica,Arial;background:#00b894;color:#fff;cursor:pointer;box-shadow:0 4px 8px #0004}
.btn.red{background:#ff3b30}
.range{width:260px;margin:16px}
small{font-size:16px;opacity:.7}
#battery{width:120px;height:48px;border:4px solid #fafafa;border-radius:6px;position:relative;margin-top:12px}
#battery:after{content:'';position:absolute;right:-14px;top:12px;width:10px;height:24px;background:#fafafa;border-radius:2px}
#level{background:#00b894;height:100%;border-radius:3px}
</style></head><body>
<h1>Tap-Guard</h1>

<svg id="glass" width="160" height="220" viewBox="0 0 100 140">
  <path d="M20 5 L80 5 L70 135 L30 135 Z" fill="#302d2d" stroke="#c9c5c5" stroke-width="4"/>
  <clipPath id="beerClip"><path d="M20 5 L80 5 L70 135 L30 135 Z"/></clipPath>
  <rect id="beer" x="22" y="30" width="56" height="100" fill="#f9c440" clip-path="url(#beerClip)"/>
  <circle id="pill" cx="50" cy="-10" r="7" fill="#fff" stroke="#d90429" stroke-width="3"/>
  <animate xlink:href="#pill" attributeName="cy" from="-10" to="30" dur="0.8s" begin="alert.begin" fill="freeze"/>
</svg>

<div id="alert" class="">—</div>

<button id="arm" class="btn">ARM</button>
<input id="sens" class="range" type="range" min="1" max="10" value="10">
<small>Sensitivity</small>

<div id="battery"><div id="level" style="width:85%"></div></div>
<small>Battery</small>

<script>
let armed=false,alerting=false;
const alertTxt=document.getElementById('alert'),armBtn=document.getElementById('arm');
const beer=document.getElementById('beer'),pill=document.getElementById('pill');
const rng=document.getElementById('sens'),level=document.getElementById('level');

function post(p){fetch(p);}
armBtn.onclick=()=>{armed=!armed;alerting=false;setUI();post('/arm?state='+(armed?1:0));};
rng.oninput=()=>{post('/sens?val='+rng.value);};
function setUI(){
 armBtn.textContent=armed?'DISARM':'ARM';
 armBtn.classList.toggle('red',armed);
 alertTxt.textContent=alerting?'YOUR DRINK IS SPIKED!':armed?'ARMED':'DISARMED';
 beer.setAttribute("fill",alerting?'#ff3b30':'#f9c440');
}
async function loop(){
 const s=await (await fetch('/state')).json(); // {a:0/1,m:0/1,b:0-100}
 armed=s.a; alerting=s.m; level.style.width=s.b+'%';
 setUI();
 setTimeout(loop,100);
}
loop();
</script></body></html>
)HTML";
/* ==================== */

/* ===== HTTP ===== */
void hRoot()  { server.send_P(200,"text/html",html); }
void hArm()   { armed = server.arg("state")=="1"; alertOn=false; server.send(200,""); }
void hSens()  { level = constrain(server.arg("val").toInt(),1,10); mpu.setMotionDetectionThreshold(thrVal()); server.send(200,""); }
void hState(){                    // JSON → a=armed, m=motion(alert), b=battery%
  String j="{\"a\":"+(String)armed+",\"m\":"+(String)alertOn+",\"b\":85}";
  server.send(200,"application/json",j);
}
/* ================= */

void setup(){
  pinMode(LED_PIN,OUTPUT); Serial.begin(115200);

  WiFi.begin("DevilDogVIP","yvetteway");
  while(WiFi.status()!=WL_CONNECTED) delay(200);
  Serial.print("URL → http://"); Serial.println(WiFi.localIP());

  if(!mpu.begin()){Serial.println("MPU HALT");while(1);}
  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionDuration(30);
  mpu.setMotionDetectionThreshold(thrVal());
  mpu.setMotionInterrupt(true);

  server.on("/",hRoot);
  server.on("/arm",hArm);
  server.on("/sens",hSens);
  server.on("/state",hState);
  server.begin();
}

void loop(){
  server.handleClient();
  if(armed && mpu.getMotionInterruptStatus()){
    sensors_event_t a,g,t; mpu.getEvent(&a,&g,&t);
    Serial.printf("MOTION Ax%.2f Az%.2f thr%d\n",a.acceleration.x,a.acceleration.z,thrVal());
    digitalWrite(LED_PIN,HIGH); alertOn=true; lastMove=millis();
  }
  if(alertOn && millis()-lastMove>1500){ alertOn=false; digitalWrite(LED_PIN,LOW);}
}
