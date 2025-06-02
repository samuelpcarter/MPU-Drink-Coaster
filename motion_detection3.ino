//--------------------------------------------------------------------------------------------------
// Product:         DrinkGuard Coaster :: Tamper Detection System
// Focus:           Real-time motion detection with animated UI for drink security.
// Revision Notes:  UI overhaul for drink coaster theme, animations, increased sensitivity,
//                  removed notes/settings UI, aggressive page refresh.
//--------------------------------------------------------------------------------------------------

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
#else
  #error "Board not supported."
#endif

// --- Configuration ---
const char* WIFI_SSID     = "DevilDogVIP"; // Your WiFi SSID
const char* WIFI_PASSWORD   = "yvetteway";   // Your WiFi Password

const int I2C_SDA_PIN = 22;
const int I2C_SCL_PIN = 21;
#if defined(ESP32)
  const int LED_PIN = 13;
#elif defined(ESP8266)
  const int LED_PIN = 2; // D4 on NodeMCU
#else
  const int LED_PIN = 2;
#endif

// --- Sensitivity Settings (Optimized for Drink Coaster Tampering) ---
const float DEFAULT_TILT_THRESHOLD_DEGREES      = 0.3f;  // Increased sensitivity
const unsigned long DEFAULT_SUSTAINED_TILT_DURATION_MS = 80;   // Quicker detection
const float DEFAULT_SLIDE_THRESHOLD_ACCEL       = 0.25f; // Increased sensitivity (m/s^2)
const unsigned long DEFAULT_SUSTAINED_SLIDE_DURATION_MS= 80;   // Quicker detection

// --- Runtime Sensitivity Settings (Fixed to defaults in this version) ---
float currentTiltThresholdDegrees     = DEFAULT_TILT_THRESHOLD_DEGREES;
unsigned long currentSustainedTiltDurationMs = DEFAULT_SUSTAINED_TILT_DURATION_MS;
float currentSlideThresholdAccel      = DEFAULT_SLIDE_THRESHOLD_ACCEL;
unsigned long currentSustainedSlideDurationMs = DEFAULT_SUSTAINED_SLIDE_DURATION_MS;

// System Parameters
const unsigned long TELEMETRY_READ_INTERVAL_MS = 50; // Sensor read frequency
const unsigned long WEB_PAGE_REFRESH_INTERVAL_MS = 200; // UI refresh frequency
const unsigned long WEB_MOTION_INDICATOR_DURATION_MS = 1500; // How long UI shows motion after event
const unsigned long LED_MOTION_INDICATOR_DURATION_MS = 750;
const int           MAX_LOG_ENTRIES              = 10;
const unsigned long MIN_LOG_INTERVAL_MS          = 250;

// Global Objects & State
Adafruit_MPU6050 mpu;
#if defined(ESP32)
  WebServer server(80);
#elif defined(ESP8266)
  ESP8266WebServer server(80);
#endif

sensors_event_t currentAcceleration, currentGyro, currentTemperature;
float currentRoll_deg = 0.0, currentPitch_deg = 0.0;
float lastSignificantRoll = 0.0, lastSignificantPitch = 0.0; // For animation detail
float lastSignificantSlideX = 0.0; // For animation detail (e.g. direction)

unsigned long lastMotionTimestamp_ms = 0;
bool webShowsMotionActive = false; // General motion indicator for UI
bool ledIndicatesMotion = false;
bool isSustainedTilt = false;
bool isSustainedSlide = false;

// Motion detection state variables
float prevTiltRoll_deg = 0.0, prevTiltPitch_deg = 0.0;
bool firstTiltCheck = true;
unsigned long sustainedTiltStartTime_ms = 0;

float prevSlideAccelX = 0.0, prevSlideAccelY = 0.0;
bool firstSlideCheck = true;
unsigned long sustainedSlideStartTime_ms = 0;

struct MotionLogEntry {
  unsigned long timestamp_ms;
  String motionType;
  float rollAtDetection_deg, pitchAtDetection_deg;
  // Removed detailed sensor data from log struct for brevity, can be added back if needed
};
std::vector<MotionLogEntry> motionEventLog;
unsigned long lastLogEntryTime_ms = 0;

// Function Prototypes (declarations)
void initializeSensor();
void initializeNetworkServices();
void processSensorData();
void detectSustainedMotionEvents();
void updateSystemIndicatorsAndLogging(unsigned long currentTime);
void manageLedIndicator();
void serveRootPage();
void serveNotFound();
String formatTime(unsigned long ms_time);
String formatDuration(unsigned long totalMillis);
void addEventToMotionLog(unsigned long timestamp, const String& type, float roll, float pitch);


void setup(void) {
  Serial.begin(115200);
  unsigned long setupStartTime = millis();
  while (!Serial && (millis() - setupStartTime < 2000));

  Serial.println("\n[DrinkGuard Coaster :: Tamper Detection System]");
  pinMode(LED_PIN, OUTPUT);
#if defined(ESP8266) && (LED_PIN == 2 || LED_PIN == 0)
  digitalWrite(LED_PIN, HIGH); // Turn off (active LOW)
#else
  digitalWrite(LED_PIN, LOW);  // Turn off (active HIGH)
#endif

  initializeSensor();
  initializeNetworkServices();

  Serial.println("[SYSTEM] Sensitivity Settings (Coaster Optimized):");
  Serial.printf("  Tilt Thresh: %.2f deg, Tilt Duration: %lu ms\n", currentTiltThresholdDegrees, currentSustainedTiltDurationMs);
  Serial.printf("  Slide Thresh: %.2f m/s^2, Slide Duration: %lu ms\n", currentSlideThresholdAccel, currentSustainedSlideDurationMs);
  Serial.println("[SYSTEM] Initialization Complete.");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Dashboard: http://"); Serial.println(WiFi.localIP());
  }
}

void loop() {
  server.handleClient();
  unsigned long currentTime_ms = millis();
  static unsigned long lastTelemetryReadTime_ms = 0;

  if (currentTime_ms - lastTelemetryReadTime_ms >= TELEMETRY_READ_INTERVAL_MS) {
    lastTelemetryReadTime_ms = currentTime_ms;
    processSensorData();
    detectSustainedMotionEvents();
  }
  updateSystemIndicatorsAndLogging(currentTime_ms);
  manageLedIndicator();
  delay(1); // Minimal delay
}

// --- Sensor and System Functions ---
void initializeSensor() {
  Serial.println("  Initializing MPU6050 Sensor...");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!mpu.begin()) {
    Serial.println(">>> CRITICAL: MPU6050 FAILED. HALTED. <<<");
    while (true) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(100); }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G); // More sensitive range for subtle movements
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);    // More sensitive
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ); // Good balance of responsiveness and noise reduction
  Serial.println("  MPU6050 Initialized.");
}

void initializeNetworkServices() {
  Serial.print("  Connecting to WiFi: "); Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 30) {
    delay(500); Serial.print("."); wifi_attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  WiFi Connected. IP: " + WiFi.localIP().toString());
    server.on("/", HTTP_GET, serveRootPage);
    server.onNotFound(serveNotFound);
    server.begin();
    Serial.println("  Web Server Started.");
  } else {
    Serial.println("\n>>> CRITICAL: WiFi FAILED. HALTED. <<<");
    while (true) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(100); }
  }
}

void processSensorData() {
  mpu.getEvent(&currentAcceleration, &currentGyro, &currentTemperature);

  // More robust Roll/Pitch from accelerometer (complementary filter would be even better)
  currentRoll_deg = atan2(currentAcceleration.acceleration.y, currentAcceleration.acceleration.z) * 180.0 / M_PI;
  currentPitch_deg = atan2(-currentAcceleration.acceleration.x, sqrt(currentAcceleration.acceleration.y * currentAcceleration.acceleration.y + currentAcceleration.acceleration.z * currentAcceleration.acceleration.z)) * 180.0 / M_PI;
}

void detectSustainedMotionEvents() {
  unsigned long currentTime = millis();
  bool tiltDetectedThisCycle = false;
  bool slideDetectedThisCycle = false;

  // Tilt Detection
  float deltaRoll = std::abs(currentRoll_deg - prevTiltRoll_deg);
  float deltaPitch = std::abs(currentPitch_deg - prevTiltPitch_deg);

  if (firstTiltCheck) {
    prevTiltRoll_deg = currentRoll_deg;
    prevTiltPitch_deg = currentPitch_deg;
    firstTiltCheck = false;
  } else {
    if (deltaRoll > currentTiltThresholdDegrees || deltaPitch > currentTiltThresholdDegrees) {
      if (sustainedTiltStartTime_ms == 0) {
        sustainedTiltStartTime_ms = currentTime;
      } else if (currentTime - sustainedTiltStartTime_ms >= currentSustainedTiltDurationMs) {
        if (!isSustainedTilt) {
          isSustainedTilt = true;
          tiltDetectedThisCycle = true;
          lastSignificantRoll = currentRoll_deg; // Capture for animation
          lastSignificantPitch = currentPitch_deg;
          addEventToMotionLog(currentTime, "Tilt Detected", currentRoll_deg, currentPitch_deg);
        }
      }
    } else {
      sustainedTiltStartTime_ms = 0;
      isSustainedTilt = false;
    }
  }
  prevTiltRoll_deg = currentRoll_deg;
  prevTiltPitch_deg = currentPitch_deg;

  // Slide Detection (using X and Y acceleration, simplified)
  // A more advanced approach might use velocity or displacement estimation
  float netAccelChange = sqrt(pow(currentAcceleration.acceleration.x - prevSlideAccelX, 2) +
                               pow(currentAcceleration.acceleration.y - prevSlideAccelY, 2));

  if (firstSlideCheck) {
    prevSlideAccelX = currentAcceleration.acceleration.x;
    prevSlideAccelY = currentAcceleration.acceleration.y;
    firstSlideCheck = false;
  } else {
    if (netAccelChange > currentSlideThresholdAccel) {
      if (sustainedSlideStartTime_ms == 0) {
        sustainedSlideStartTime_ms = currentTime;
      } else if (currentTime - sustainedSlideStartTime_ms >= currentSustainedSlideDurationMs) {
        if (!isSustainedSlide) {
          isSustainedSlide = true;
          slideDetectedThisCycle = true;
          // Capture rough direction/magnitude for animation if needed
          lastSignificantSlideX = (currentAcceleration.acceleration.x > prevSlideAccelX + currentSlideThresholdAccel/2.0) ? 1 : ((currentAcceleration.acceleration.x < prevSlideAccelX - currentSlideThresholdAccel/2.0) ? -1 : 0);
          addEventToMotionLog(currentTime, "Slide Detected", currentRoll_deg, currentPitch_deg);
        }
      }
    } else {
      sustainedSlideStartTime_ms = 0;
      isSustainedSlide = false;
    }
  }
  prevSlideAccelX = currentAcceleration.acceleration.x;
  prevSlideAccelY = currentAcceleration.acceleration.y;

  if (tiltDetectedThisCycle || slideDetectedThisCycle) {
    lastMotionTimestamp_ms = currentTime;
  }
}

void updateSystemIndicatorsAndLogging(unsigned long currentTime_ms) {
  // Web UI motion indicator: stays active for a period after last motion
  if (currentTime_ms - lastMotionTimestamp_ms < WEB_MOTION_INDICATOR_DURATION_MS && lastMotionTimestamp_ms != 0) {
    webShowsMotionActive = true;
  } else {
    webShowsMotionActive = false;
    isSustainedTilt = false; // Also reset specific states when UI indicator expires
    isSustainedSlide = false;
    lastSignificantRoll = 0; lastSignificantPitch = 0; lastSignificantSlideX = 0; // Reset animation cues
  }

  // LED motion indicator
  if (currentTime_ms - lastMotionTimestamp_ms < LED_MOTION_INDICATOR_DURATION_MS && lastMotionTimestamp_ms != 0) {
    ledIndicatesMotion = true;
  } else {
    ledIndicatesMotion = false;
  }
}

void manageLedIndicator() {
#if defined(ESP8266) && (LED_PIN == 2 || LED_PIN == 0)
  digitalWrite(LED_PIN, ledIndicatesMotion ? LOW : HIGH); // Active LOW
#else
  digitalWrite(LED_PIN, ledIndicatesMotion ? HIGH : LOW); // Active HIGH
#endif
}

void addEventToMotionLog(unsigned long ts, const String& type, float r, float p) {
  unsigned long currentTime = millis();
  if (currentTime - lastLogEntryTime_ms < MIN_LOG_INTERVAL_MS && !motionEventLog.empty()) {
    // Debounce logging for rapidly successive events of same broad type
    return;
  }
  if (motionEventLog.size() >= MAX_LOG_ENTRIES) {
    motionEventLog.erase(motionEventLog.begin());
  }
  MotionLogEntry newEntry = {ts, type, r, p};
  motionEventLog.push_back(newEntry);
  lastLogEntryTime_ms = currentTime;
  Serial.println("[LOG] " + formatTime(ts) + " - " + type);
}

String formatTime(unsigned long ms_time) {
  unsigned long seconds = ms_time / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  seconds %= 60; minutes %= 60; hours %= 24; // Use 24 for hours to wrap around
  char timeBuffer[9]; // HH:MM:SS
  sprintf(timeBuffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(timeBuffer);
}

String formatDuration(unsigned long totalMillis) {
  // Simplified for brevity, can be expanded
  unsigned long secs = totalMillis / 1000;
  unsigned long mins = secs / 60;
  secs %= 60;
  return String(mins) + "m " + String(secs) + "s";
}

// --- Web Server Handlers ---
void serveRootPage() {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='" + String(WEB_PAGE_REFRESH_INTERVAL_MS / 1000.0, 1) + "'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>DrinkGuard Coaster - Status</title>";
  
  html += "<style>";
  html += "body{font-family: 'Arial', sans-serif; margin:0; padding:0; background-color:#1a1a2e; color:#e0e0fc; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:100vh; overflow:hidden;}";
  html += ".container{text-align:center; padding:20px; background-color:#162447; border-radius:15px; box-shadow:0 0 25px rgba(224, 224, 252, 0.3); width:90%; max-width:450px;}";
  html += "h1{color:#ff7f50; margin-bottom:10px; font-size:2.2em; text-shadow: 0 0 10px #ff7f50;}";
  html += "p.status-text{font-size:1.5em; margin:15px 0; height:30px; font-weight:bold; transition: color 0.3s ease;}";
  html += ".status-secure{color:#32cd32;} .status-alert{color:#ff4757;}";
  
  // Drink and Coaster Visualization Styles
  html += ".visualization{position:relative; width:150px; height:200px; margin:20px auto; perspective: 500px;}";
  html += ".drink-glass{width:100px; height:150px; background:rgba(173, 216, 230, 0.3); /* Light blue with alpha */ border:2px solid rgba(173, 216, 230, 0.7); border-bottom:none; border-radius:10px 10px 0 0; position:absolute; bottom:0; left:25px; display:flex; align-items:flex-end; justify-content:center;}";
  html += ".drink-liquid{width:90%; height:70%; background:linear-gradient(to top, #ff7f50, #ffae73); /* Drink color */ border-radius:0 0 8px 8px; margin-bottom:5%;}";
  html += ".coaster{width:120px; height:20px; background-color:#4a4e69; border:2px solid #3a3d52; border-radius:3px; position:absolute; top:30px; /* Adjust to sit on top of drink */ left:15px; transform-style:preserve-3d; transition: transform 0.15s linear, background-color 0.3s ease;}";
  html += ".coaster.alert-coaster{background-color:#c70039; box-shadow: 0 0 15px #ff4757;}";

  html += ".log-section{margin-top:30px; max-height:150px; overflow-y:auto; background:rgba(0,0,0,0.2); border-radius:8px; padding:10px;}";
  html += "h2{color:#bbb; font-size:1.2em; margin-bottom:10px;}";
  html += "ul{list-style-type:none; padding:0; margin:0;}";
  html += "li{font-size:0.9em; color:#ccc; padding:5px 0; border-bottom:1px dashed #444;} li:last-child{border-bottom:none;}";
  html += ".log-time{font-weight:bold; color:#ff7f50; margin-right:8px;}";
  html += ".footer-info{font-size:0.8em; color:#777; margin-top:20px;}";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>DrinkGuard</h1>";

  // Visualization Area
  html += "<div class='visualization'>";
  html += "<div class='drink-glass'><div class='drink-liquid'></div></div>";
  html += "<div id='coaster' class='coaster'></div>";
  html += "</div>";

  html += "<p id='statusText' class='status-text'></p>";
  
  html += "<div class='log-section'><h2>Event Log</h2><ul>";
  if (motionEventLog.empty()) {
    html += "<li>No events yet.</li>";
  } else {
    for (int i = motionEventLog.size() - 1; i >= 0; i--) {
      const MotionLogEntry& entry = motionEventLog[i];
      html += "<li><span class='log-time'>" + formatTime(entry.timestamp_ms) + "</span>" + entry.motionType + "</li>";
    }
  }
  html += "</ul></div>"; // end log-section

  html += "<p class='footer-info'>Uptime: " + formatDuration(millis()) + " | IP: " + WiFi.localIP().toString() + "</p>";
  html += "</div>"; // end container

  // JavaScript for animations and status update
  html += "<script>";
  html += "const motionIsActive = " + String(webShowsMotionActive ? "true" : "false") + ";\n";
  html += "const isTilt = " + String(isSustainedTilt ? "true" : "false") + ";\n";
  html += "const isSlide = " + String(isSustainedSlide ? "true" : "false") + ";\n";
  // Get precise values for more nuanced animation if available
  html += "const rollDeg = " + String(lastSignificantRoll, 1) + ";\n";
  html += "const pitchDeg = " + String(lastSignificantPitch, 1) + ";\n";
  // Simple slide indicator: 1 for positive X-ish, -1 for negative X-ish, 0 for no slide or Y slide
  html += "const slideDirX = " + String(lastSignificantSlideX, 0) + ";\n";

  html += "const statusTextEl = document.getElementById('statusText');\n";
  html += "const coasterEl = document.getElementById('coaster');\n";
  
  html += "if(motionIsActive){\n";
  html += "  statusTextEl.textContent = 'ALERT: TAMPERING DETECTED!';\n";
  html += "  statusTextEl.className = 'status-text status-alert';\n";
  html += "  coasterEl.classList.add('alert-coaster');\n";
  html += "  let transformStyle = '';\n";
  html += "  if(isTilt){\n";
  // Apply rotation based on roll and pitch. Prioritize pitch for front/back tilt visual.
  // Max visual tilt to avoid extreme animation.
  html += "    let visualPitch = Math.max(-25, Math.min(25, pitchDeg));\n";
  html += "    let visualRoll = Math.max(-25, Math.min(25, rollDeg));\n";
  html += "    transformStyle += 'rotateX(' + (visualPitch * -1) + 'deg) rotateY(' + visualRoll + 'deg) ';\n"; // Inverted pitch for intuitive visual
  html += "  }\n";
  html += "  if(isSlide){\n";
  html += "    transformStyle += 'translateX(' + (slideDirX * 15) + 'px) ';\n"; // Slide 15px left/right
  html += "  }\n";
  html += "  coasterEl.style.transform = transformStyle;\n";
  html += "} else {\n";
  html += "  statusTextEl.textContent = 'Status: Drink Secure';\n";
  html += "  statusTextEl.className = 'status-text status-secure';\n";
  html += "  coasterEl.classList.remove('alert-coaster');\n";
  html += "  coasterEl.style.transform = 'rotateX(0deg) rotateY(0deg) translateX(0px)';\n";
  html += "}\n";
  html += "</script>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void serveNotFound() {
  String message = "<!DOCTYPE html><html><head><title>Not Found</title>";
  message += "<style>body{font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;background-color:#1a1a2e;color:#e0e0fc;margin:0;}";
  message += ".message-box{text-align:center;padding:30px;background-color:#162447;border-radius:8px;box-shadow:0 0 15px rgba(224,224,252,0.2);}</style></head>";
  message += "<body><div class='message-box'><h1>404 - Not Found</h1><p>The page you requested does not exist.</p>";
  message += "<p><a href='/' style='color:#ff7f50;'>Return to DrinkGuard Status</a></p></div></body></html>";
  server.send(404, "text/html", message);
}