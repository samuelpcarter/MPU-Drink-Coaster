/* Tap-Guard v8 – Elegant & Responsive UI
 * SSID/PASS → DevilDogVIP / yvetteway (Keep your credentials secure)
 * Slider 1-10 (5 = default optimal, 10 = most sensitive). LED + web alert when armed & motion.
 * Designed with a more refined aesthetic.
 */
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <time.h> // Not strictly used in this revision but often useful

#define LED_PIN 2

// --- Sensitivity Configuration ---
// GUI slider level (1-10). Default is 5.
int  level = 5; 
bool armed = false;
bool alertOn = false; // True when motion detected while armed
uint32_t lastMove = 0; // Timestamp of last motion detection

Adafruit_MPU6050 mpu;
WebServer server(80);

// Placeholder for actual battery level reading
int getBatteryLevel() {
  // TODO: Implement actual battery voltage reading and percentage calculation
  // For example, read an analog pin connected to a voltage divider on the battery
  // and map the voltage to a 0-100% scale.
  // For now, returning a static value or a slowly decreasing simulated value.
  static int simBattery = 95; // Initial simulated battery
  // simBattery = max(0, simBattery - 1); // Example: simulate decrease over time if called periodically
  return simBattery; 
}

// Maps GUI slider level (1-10) to MPU6050 motion detection threshold.
int thrVal() {
  const int DEVICE_THR_MOST_SENSITIVE = 1;  // MPU's most sensitive setting
  const int DEVICE_THR_DEFAULT_OPTIMAL = 1; // Desired for GUI level 5 ("coaster" detection)
  const int DEVICE_THR_LEAST_SENSITIVE = 80; // MPU's least sensitive for this app (at GUI level 1)

  if (level == 5) {
    return DEVICE_THR_DEFAULT_OPTIMAL;
  } else if (level > 5) { // GUI Levels 6-10 (higher sensitivity)
    // Since default optimal is already the most sensitive MPU can do (1), these also map to 1.
    return DEVICE_THR_MOST_SENSITIVE;
  } else { // GUI Levels 1-4 (lower sensitivity)
    // Map GUI 1-4 to physical LEAST_SENSITIVE down to (DEFAULT_OPTIMAL + a bit more for smoother transition)
    // Threshold for level 4 will be DEVICE_THR_DEFAULT_OPTIMAL + 1 = 2
    return map(level, 1, 4, DEVICE_THR_LEAST_SENSITIVE, DEVICE_THR_DEFAULT_OPTIMAL + 1);
  }
}

/* ===== HTML UI ===== */
const char html[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Tap-Guard</title>
<style>
html,body{margin:0;padding:0;height:100%;font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;background:#2c3e50;color:#ecf0f1;overflow-x:hidden;}
body{display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:10px;box-sizing:border-box;min-height:100vh;}
h1{margin-top:0;margin-bottom:10px;letter-spacing:1px;opacity:.25;font-size:clamp(30px, 8vw, 48px);width:100%;text-align:center;font-style:italic;}
#glass-container{position:relative;margin-bottom:20px;filter:drop-shadow(0 6px 10px rgba(0,0,0,.3));}
#glass-base{width:70px;height:15px;background:rgba(0,0,0,0.15);border-radius:50%;position:absolute;bottom:-5px;left:50%;transform:translateX(-50%);z-index:-1;box-shadow:0 2px 3px rgba(0,0,0,0.2) inset;}
svg#glass{width:clamp(120px, 40vw, 180px);height:auto;max-height:280px;}
.liquid-fill{transition:fill .3s ease;}
.bubble{fill:#fff;opacity:0.6;animation:rise 4s infinite ease-in-out;rx:2;ry:2;}
.bubble.b2{animation-delay:1s;animation-duration:4.5s;}
.bubble.b3{animation-delay:2s;animation-duration:3.5s;}
.bubble.b4{animation-delay:0.5s;animation-duration:5s;}
@keyframes rise{0%{opacity:0;transform:translateY(0px);} 25%{opacity:0.7;} 75%{opacity:0.7;} 100%{opacity:0;transform:translateY(-80px);}}
#pill{transition:transform 0.5s cubic-bezier(0.68, -0.55, 0.27, 1.55), opacity 0.5s;transform:translateY(-50px) scale(0.5);opacity:0;pointer-events:none;}
#pill.animate{transform:translateY(30px) scale(1);opacity:1;}
#alert{margin:15px 0;font-size:clamp(24px, 6vw, 32px);font-weight:bold;transition:.2s;min-height:35px;}
.btn{padding:12px 24px;border:none;border-radius:8px;font-weight:600;font-size:clamp(16px, 4vw, 18px);background:#1abc9c;color:#fff;cursor:pointer;box-shadow:0 4px 6px rgba(0,0,0,.2);transition:background .2s, transform .1s;margin-top:10px;}
.btn:active{transform:scale(0.97);}
.btn.red{background:#e74c3c;}
.controls{margin:10px 0;}
.range{width:clamp(220px, 70vw, 280px);margin:8px auto;}
.sensitivity-label{font-size:clamp(14px, 3.5vw, 16px);opacity:.8;margin-top:5px;}
#battery-container{position:absolute;top:15px;right:15px;text-align:center;filter:drop-shadow(0 2px 4px rgba(0,0,0,.2));}
#battery{width:60px;height:28px;border:3px solid #ecf0f1;border-radius:5px;position:relative;margin:0 auto 5px auto;}
#battery:after{content:'';position:absolute;right:-7px;top:6px;width:5px;height:12px;background:#ecf0f1;border-radius:0 2px 2px 0;}
#battery-level{background:#1abc9c;height:100%;border-radius:2px 0 0 2px;transition:width .5s ease;} /* Adjusted border-radius for level */
.battery-text{font-size:12px;opacity:.8;}
</style></head><body>
<h1>Tap-Guard</h1>

<div id="battery-container">
  <div id="battery"><div id="battery-level" style="width:85%"></div></div>
  <small class="battery-text"><span id="battery-percent">85</span>%</small>
</div>

<div id="glass-container">
  <svg id="glass" viewBox="0 0 100 150">
    <defs>
      <clipPath id="glassClip">
        <path d="M15 5 Q20 0 50 0 Q80 0 85 5 L75 145 Q75 150 50 150 Q25 150 25 145 Z"/>
      </clipPath>
    </defs>
    <path d="M15 5 Q20 0 50 0 Q80 0 85 5 L75 145 Q75 150 50 150 Q25 150 25 145 Z" fill="#4a4a4a" stroke="#bfbfbf" stroke-width="3"/>
    <rect id="liquid" x="17" y="25" width="66" height="120" class="liquid-fill" fill="#f39c12" clip-path="url(#glassClip)"/>
    <circle class="bubble b1" cx="40" cy="120" r="2.5"/>
    <circle class="bubble b2" cx="50" cy="125" r="2"/>
    <circle class="bubble b3" cx="60" cy="120" r="3"/>
    <circle class="bubble b4" cx="45" cy="115" r="1.5"/>
    <circle class="bubble b1" cx="55" cy="100" r="2.5" style="animation-delay:0.2s;"/>
    <circle class="bubble b2" cx="35" cy="90" r="2" style="animation-delay:1.2s;"/>

    <circle id="pill" cx="50" cy="30" r="7" fill="#fff" stroke="#e74c3c" stroke-width="2.5"/>
  </svg>
  <div id="glass-base"></div>
</div>

<div id="alert" class="">DISARMED</div>
<button id="arm" class="btn">ARM</button>

<div class="controls">
  <input id="sens" class="range" type="range" min="1" max="10" value="5">
  <div class="sensitivity-label">Sensitivity: <span id="sensVal">5</span></div>
</div>

<script>
let currentArmed=false, currentAlerting=false;
const alertTxt=document.getElementById('alert');
const armBtn=document.getElementById('arm');
const liquid=document.getElementById('liquid'); // Changed from 'beer' to 'liquid'
const pill=document.getElementById('pill');
const rangeSlider=document.getElementById('sens');
const sensValSpan=document.getElementById('sensVal');
const batteryLevelDiv=document.getElementById('battery-level');
const batteryPercentSpan=document.getElementById('battery-percent');

function post(path){fetch(path).catch(err => console.error("Fetch error:", err));}

armBtn.onclick=()=>{
  currentArmed=!currentArmed;
  currentAlerting=false; // Reset alert visual on arm/disarm
  pill.classList.remove('animate'); // Ensure pill is hidden
  updateUI();
  post('/arm?state='+(currentArmed?1:0));
};

rangeSlider.oninput=()=>{
  sensValSpan.textContent=rangeSlider.value;
  post('/sens?val='+rangeSlider.value);
};

function updateUI(){
  armBtn.textContent=currentArmed?'DISARM':'ARM';
  armBtn.classList.toggle('red',currentArmed);
  
  if(currentAlerting){
    alertTxt.textContent='YOUR DRINK IS SPIKED!';
    alertTxt.style.color = '#e74c3c'; // Alert color
    liquid.setAttribute("fill",'#e74c3c'); // Liquid turns red
    pill.classList.add('animate');
  } else if (currentArmed) {
    alertTxt.textContent='ARMED';
    alertTxt.style.color = '#1abc9c'; // Armed color (teal)
    liquid.setAttribute("fill",'#f39c12'); // Normal liquid color
    pill.classList.remove('animate');
  } else {
    alertTxt.textContent='DISARMED';
    alertTxt.style.color = '#ecf0f1'; // Disarmed color (default text)
    liquid.setAttribute("fill",'#f39c12'); // Normal liquid color
    pill.classList.remove('animate');
  }
}

async function fetchDataLoop(){
  try {
    const response = await fetch('/state');
    if (!response.ok) {
      console.error("Error fetching state:", response.status);
      setTimeout(fetchDataLoop,1000); // Try again after a second
      return;
    }
    const s = await response.json(); // {a:0/1, m:0/1, b:0-100, l:1-10}
    
    let uiNeedsUpdate = false;
    if (currentArmed !== (s.a === 1)) {
        currentArmed = (s.a === 1);
        uiNeedsUpdate = true;
    }
    if (currentAlerting !== (s.m === 1)) {
        currentAlerting = (s.m === 1);
        uiNeedsUpdate = true;
    }
    if (parseInt(rangeSlider.value) !== s.l) {
        rangeSlider.value = s.l;
        sensValSpan.textContent = s.l;
        // No need to post back, this is state from server
    }

    batteryLevelDiv.style.width = s.b + '%';
    batteryPercentSpan.textContent = s.b;
    
    if(uiNeedsUpdate || (currentAlerting && !pill.classList.contains('animate')) || (!currentAlerting && pill.classList.contains('animate')) ){
        updateUI();
    }

  } catch (error) {
    console.error("State update error:", error);
  }
  setTimeout(fetchDataLoop, 250); // Fetch state more frequently for responsiveness
}

// Initial UI setup based on JS state (which will be updated by first fetch)
sensValSpan.textContent = rangeSlider.value; // Init slider text
updateUI();
fetchDataLoop(); // Start the loop to get state from server
</script></body></html>
)HTML";
/* ==================== */

/* ===== HTTP Handlers ===== */
void hRoot() { server.send_P(200, "text/html", html); }

void hArm() {
  armed = server.arg("state") == "1";
  if (!armed) { // If disarming, also clear any active alert
    alertOn = false; 
    digitalWrite(LED_PIN, LOW); // Turn off LED if disarming
  }
  Serial.println(armed ? "System ARMED" : "System DISARMED");
  server.send(200, "text/plain", "");
}

void hSens() {
  if (server.hasArg("val")) {
    int newLevel = server.arg("val").toInt();
    level = constrain(newLevel, 1, 10);
    mpu.setMotionDetectionThreshold(thrVal());
    Serial.print("Sensitivity level: "); Serial.print(level);
    Serial.print(", MPU Threshold: "); Serial.println(thrVal());
  }
  server.send(200, "text/plain", "");
}

void hState() {
  int currentBattery = getBatteryLevel();
  // JSON → a=armed, m=motion(alert), b=battery%, l=level
  String j = "{\"a\":" + String(armed ? 1 : 0) +
             ",\"m\":" + String(alertOn ? 1 : 0) +
             ",\"b\":" + String(currentBattery) +
             ",\"l\":" + String(level) + "}";
  server.send(200, "application/json", j);
}
/* ================= */

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(115200);

  WiFi.begin("DevilDogVIP", "yvetteway"); // Replace with your actual SSID and Password
  Serial.print("Connecting to WiFi...");
  int wifi_retries = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retries < 30) { // Retry for ~15 seconds
    delay(500);
    Serial.print(".");
    wifi_retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("IP Address: http://"); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi. Check credentials or network.");
    // Consider a fallback mode or error indication here
  }

  Serial.println("Initializing MPU6050...");
  if (!mpu.begin()) {
    Serial.println("MPU6050 connection failed! Halting.");
    while (1) { delay(100); } // Stop if MPU fails
  }
  Serial.println("MPU6050 initialized.");

  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionDuration(20); // Detect motion lasting at least 20ms
  mpu.setMotionDetectionThreshold(thrVal()); // Set initial threshold based on default level (5)
  mpu.setInterruptPinLatch(true);     // Keep interrupt active until status is read
  mpu.setInterruptPinPolarity(true);  // Interrupt pin is active HIGH
  mpu.setMotionInterrupt(true);       // Enable motion interrupt

  Serial.print("Initial MPU Threshold (level 5): "); Serial.println(thrVal());

  server.on("/", hRoot);
  server.on("/arm", HTTP_GET, hArm);
  server.on("/sens", HTTP_GET, hSens);
  server.on("/state", HTTP_GET, hState);
  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();

  if (armed && mpu.getMotionInterruptStatus()) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t); // Reading events clears the interrupt status if latch is enabled
    
    Serial.printf("MOTION DETECTED! Ax:%.2f, Ay:%.2f, Az:%.2f, Threshold:%d\n",
                  a.acceleration.x, a.acceleration.y, a.acceleration.z, thrVal());
                  
    digitalWrite(LED_PIN, HIGH);
    alertOn = true;
    lastMove = millis(); // Record time of motion
  }

  // If alert was on but no motion for 1.5 seconds, turn off the visual alert (but remains armed)
  // The LED will also turn off here. User might want LED to stay on until disarmed.
  // For now, alertOn flag handles the "YOUR DRINK IS SPIKED" message.
  if (alertOn && (millis() - lastMove > 2500)) { // Increased duration for alert message
    // alertOn = false; // Keep alertOn true until disarmed or new motion?
                      // For now, this makes the "SPIKED" message disappear after a while.
                      // If you want it to persist until disarm, remove this block or just handle LED.
    // digitalWrite(LED_PIN, LOW); // If LED should also turn off after timeout
  }
  
  // If system is disarmed, ensure LED is off.
  if (!armed) {
    digitalWrite(LED_PIN, LOW);
    if (alertOn) { // If it was alerting, and now disarmed, clear alert state
        alertOn = false;
    }
  }
}
