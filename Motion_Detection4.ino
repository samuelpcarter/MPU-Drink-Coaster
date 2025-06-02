//--------------------------------------------------------------------------------------------------
// Product:         DrinkGuard Coaster :: Tamper Detection System v2
// Focus:           User-adjustable sensitivity, improved UI/UX, reliable animation.
// Revision Notes:  Reduced default sensitivity, added UI controls for settings,
//                  lighter UI theme, improved animation logic, removed event log/IP/uptime.
//--------------------------------------------------------------------------------------------------

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
// #include <vector> // No longer needed as event log is removed
#include <cmath>
#include <cstdio>
#include <cstdlib> // For strtof, strtoul

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

// --- Default Sensitivity Settings (MUCH LESS SENSITIVE NOW) ---
const float DEFAULT_TILT_THRESHOLD_DEGREES      = 1.5f;
const unsigned long DEFAULT_SUSTAINED_TILT_DURATION_MS = 250;
const float DEFAULT_SLIDE_THRESHOLD_ACCEL       = 1.0f; // m/s^2
const unsigned long DEFAULT_SUSTAINED_SLIDE_DURATION_MS= 250;

// --- Runtime Adjustable Sensitivity Settings ---
float currentTiltThresholdDegrees     = DEFAULT_TILT_THRESHOLD_DEGREES;
unsigned long currentSustainedTiltDurationMs = DEFAULT_SUSTAINED_TILT_DURATION_MS;
float currentSlideThresholdAccel      = DEFAULT_SLIDE_THRESHOLD_ACCEL;
unsigned long currentSustainedSlideDurationMs = DEFAULT_SUSTAINED_SLIDE_DURATION_MS;

// System Parameters
const unsigned long TELEMETRY_READ_INTERVAL_MS = 50; // Sensor read frequency
const unsigned long WEB_PAGE_REFRESH_INTERVAL_MS = 250; // UI refresh (was 200, slightly increased)
const unsigned long WEB_MOTION_INDICATOR_DURATION_MS = 2000; // How long UI shows motion after event
const unsigned long LED_MOTION_INDICATOR_DURATION_MS = 1000;

// Global Objects & State
Adafruit_MPU6050 mpu;
#if defined(ESP32)
  WebServer server(80);
#elif defined(ESP8266)
  ESP8266WebServer server(80);
#endif

sensors_event_t currentAcceleration, currentGyro, currentTemperature;
float currentRoll_deg = 0.0, currentPitch_deg = 0.0;
float animRoll = 0.0, animPitch = 0.0; // Smoothed/latched values for animation
float animSlideX = 0.0;                // For animation detail

unsigned long lastMotionTimestamp_ms = 0;
bool webShowsMotionActive = false; // General motion indicator for UI
bool ledIndicatesMotion = false;
bool isSustainedTilt = false;
bool isSustainedSlide = false;
String lastMotionType = "None"; // To distinguish in UI

// Motion detection state variables
float prevTiltRoll_deg = 0.0, prevTiltPitch_deg = 0.0;
bool firstTiltCheck = true;
unsigned long sustainedTiltStartTime_ms = 0;

float prevSlideAccelX = 0.0, prevSlideAccelY = 0.0; // Using raw accel values for slide delta
bool firstSlideCheck = true;
unsigned long sustainedSlideStartTime_ms = 0;

// Function Prototypes
void initializeSensor();
void initializeNetworkServices();
void processSensorData();
void detectSustainedMotionEvents();
void updateSystemIndicators(unsigned long currentTime);
void manageLedIndicator();
void serveRootPage();
void handleUpdateSetting(); // New handler for settings
void serveNotFound();
String formatDuration(unsigned long totalMillis); // Kept for potential subtle footer

void setup(void) {
  Serial.begin(115200);
  unsigned long setupStartTime = millis();
  while (!Serial && (millis() - setupStartTime < 2000));

  Serial.println("\n[DrinkGuard Coaster :: Tamper Detection System v2]");
  pinMode(LED_PIN, OUTPUT);
#if defined(ESP8266) && (LED_PIN == 2 || LED_PIN == 0)
  digitalWrite(LED_PIN, HIGH); // Turn off (active LOW)
#else
  digitalWrite(LED_PIN, LOW);  // Turn off (active HIGH)
#endif

  initializeSensor();
  initializeNetworkServices();

  Serial.println("[SYSTEM] Initial Sensitivity Settings (User Adjustable):");
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
  updateSystemIndicators(currentTime_ms);
  manageLedIndicator();
  delay(1);
}

// --- Sensor and System Functions ---
void initializeSensor() {
  Serial.println("  Initializing MPU6050 Sensor...");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!mpu.begin()) {
    Serial.println(">>> CRITICAL: MPU6050 FAILED. HALTED. <<<");
    while (true) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(100); }
  }
  // Use less sensitive ranges to start, user can fine-tune thresholds
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); // A bit more filtering
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
    server.on("/updatesetting", HTTP_GET, handleUpdateSetting); // Route for settings
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
  currentRoll_deg = atan2(currentAcceleration.acceleration.y, currentAcceleration.acceleration.z) * 180.0 / M_PI;
  currentPitch_deg = atan2(-currentAcceleration.acceleration.x, sqrt(currentAcceleration.acceleration.y * currentAcceleration.acceleration.y + currentAcceleration.acceleration.z * currentAcceleration.acceleration.z)) * 180.0 / M_PI;
}

void detectSustainedMotionEvents() {
  unsigned long currentTime = millis();
  bool tiltDetectedThisCycle = false;
  bool slideDetectedThisCycle = false;

  // --- Tilt Detection ---
  float absDeltaRoll = std::abs(currentRoll_deg - prevTiltRoll_deg);
  float absDeltaPitch = std::abs(currentPitch_deg - prevTiltPitch_deg);

  if (firstTiltCheck) {
    prevTiltRoll_deg = currentRoll_deg;
    prevTiltPitch_deg = currentPitch_deg;
    firstTiltCheck = false;
  } else {
    if (absDeltaRoll > currentTiltThresholdDegrees || absDeltaPitch > currentTiltThresholdDegrees) {
      if (sustainedTiltStartTime_ms == 0) { // Tilt candidate started
        sustainedTiltStartTime_ms = currentTime;
      } else if (currentTime - sustainedTiltStartTime_ms >= currentSustainedTiltDurationMs) {
        if (!isSustainedTilt) { // New sustained tilt event
          isSustainedTilt = true;
          tiltDetectedThisCycle = true;
          animPitch = currentPitch_deg; // Capture for animation
          animRoll = currentRoll_deg;
          lastMotionType = "Tilt";
          Serial.printf("[MOTION] Tilt Detected: R:%.1f P:%.1f\n", currentRoll_deg, currentPitch_deg);
        }
      }
    } else { // Below threshold
      sustainedTiltStartTime_ms = 0; // Reset timer
      // isSustainedTilt will be reset by updateSystemIndicators if motion window passes
    }
  }
  // Update previous values for next iteration's delta calculation
  prevTiltRoll_deg = currentRoll_deg;
  prevTiltPitch_deg = currentPitch_deg;

  // --- Slide Detection ---
  // Using net change in X/Y acceleration from a zero-ish baseline might be better for coasters.
  // For now, using delta from previous sample, ensure thresholds are appropriate.
  float deltaAccelX = std::abs(currentAcceleration.acceleration.x - prevSlideAccelX);
  float deltaAccelY = std::abs(currentAcceleration.acceleration.y - prevSlideAccelY);

  if (firstSlideCheck) {
    prevSlideAccelX = currentAcceleration.acceleration.x;
    prevSlideAccelY = currentAcceleration.acceleration.y;
    firstSlideCheck = false;
  } else {
    // Simplified: check if either X or Y acceleration delta exceeds threshold
    if (deltaAccelX > currentSlideThresholdAccel || deltaAccelY > currentSlideThresholdAccel) {
      if (sustainedSlideStartTime_ms == 0) { // Slide candidate started
        sustainedSlideStartTime_ms = currentTime;
      } else if (currentTime - sustainedSlideStartTime_ms >= currentSustainedSlideDurationMs) {
        if (!isSustainedSlide) { // New sustained slide event
          isSustainedSlide = true;
          slideDetectedThisCycle = true;
          // Determine slide direction for animation (simple)
          if (deltaAccelX > deltaAccelY) animSlideX = (currentAcceleration.acceleration.x > prevSlideAccelX) ? 1.0 : -1.0;
          else animSlideX = 0; // Could add Y-slide animation here
          lastMotionType = "Slide";
           Serial.printf("[MOTION] Slide Detected: X:%.2f Y:%.2f\n", currentAcceleration.acceleration.x, currentAcceleration.acceleration.y);
        }
      }
    } else { // Below threshold
      sustainedSlideStartTime_ms = 0; // Reset timer
    }
  }
  prevSlideAccelX = currentAcceleration.acceleration.x;
  prevSlideAccelY = currentAcceleration.acceleration.y;

  if (tiltDetectedThisCycle || slideDetectedThisCycle) {
    lastMotionTimestamp_ms = currentTime; // Update general motion timestamp
  }
}


void updateSystemIndicators(unsigned long currentTime_ms) {
  if (currentTime_ms - lastMotionTimestamp_ms < WEB_MOTION_INDICATOR_DURATION_MS && lastMotionTimestamp_ms != 0) {
    webShowsMotionActive = true;
    // Keep specific motion types (isSustainedTilt, isSustainedSlide) active
  } else {
    webShowsMotionActive = false;
    isSustainedTilt = false;
    isSustainedSlide = false;
    lastMotionType = "None";
    animRoll = 0.0; animPitch = 0.0; animSlideX = 0.0; // Reset animation cues
  }

  if (currentTime_ms - lastMotionTimestamp_ms < LED_MOTION_INDICATOR_DURATION_MS && lastMotionTimestamp_ms != 0) {
    ledIndicatesMotion = true;
  } else {
    ledIndicatesMotion = false;
  }
}

void manageLedIndicator() {
#if defined(ESP8266) && (LED_PIN == 2 || LED_PIN == 0)
  digitalWrite(LED_PIN, ledIndicatesMotion ? LOW : HIGH);
#else
  digitalWrite(LED_PIN, ledIndicatesMotion ? HIGH : LOW);
#endif
}

// --- Web Server Handlers ---

void handleUpdateSetting() {
  String param = server.arg("param");
  String action = server.arg("action");
  float stepFloat = 0.0;
  long stepLong = 0;

  Serial.printf("Updating setting: %s, Action: %s\n", param.c_str(), action.c_str());

  if (param == "tilt_thresh") {
    stepFloat = 0.1f;
    float val = currentTiltThresholdDegrees;
    if (action == "inc") val += stepFloat; else if (action == "dec") val -= stepFloat;
    currentTiltThresholdDegrees = constrain(val, 0.2f, 5.0f);
    Serial.printf("New Tilt Threshold: %.2f\n", currentTiltThresholdDegrees);
  } else if (param == "tilt_dur") {
    stepLong = 25; //ms
    long val = currentSustainedTiltDurationMs;
    if (action == "inc") val += stepLong; else if (action == "dec") val -= stepLong;
    currentSustainedTiltDurationMs = constrain(val, 50, 1000);
     Serial.printf("New Tilt Duration: %lu\n", currentSustainedTiltDurationMs);
  } else if (param == "slide_thresh") {
    stepFloat = 0.1f; // m/s^2
    float val = currentSlideThresholdAccel;
    if (action == "inc") val += stepFloat; else if (action == "dec") val -= stepFloat;
    currentSlideThresholdAccel = constrain(val, 0.1f, 3.0f);
    Serial.printf("New Slide Threshold: %.2f\n", currentSlideThresholdAccel);
  } else if (param == "slide_dur") {
    stepLong = 25; // ms
    long val = currentSustainedSlideDurationMs;
    if (action == "inc") val += stepLong; else if (action == "dec") val -= stepLong;
    currentSustainedSlideDurationMs = constrain(val, 50, 1000);
    Serial.printf("New Slide Duration: %lu\n", currentSustainedSlideDurationMs);
  }

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Updating...");
}


void serveRootPage() {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='" + String(WEB_PAGE_REFRESH_INTERVAL_MS / 1000.0, 1) + "'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>DrinkGuard Status</title>";
  
  html += "<style>";
  html += "body{font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; margin:0; padding:20px; background-color:#f4f7f6; color:#333; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:calc(100vh - 40px); text-align:center;}";
  html += ".container{background-color:#ffffff; padding:25px; border-radius:12px; box-shadow:0 5px 25px rgba(0,0,0,0.1); width:95%; max-width:480px;}";
  html += "h1{color:#0056b3; margin-top:0; margin-bottom:15px; font-size:2.3em;}"; // Trustworthy blue
  html += "p.status-text{font-size:1.6em; margin:20px 0; height:30px; font-weight:600; transition: color 0.3s ease;}";
  html += ".status-secure{color:#28a745;} .status-alert-tilt{color:#ffc107;} .status-alert-slide{color:#fd7e14;} .status-alert-general{color:#dc3545;}"; // Green, Yellow, Orange, Red
  
  html += ".visualization{position:relative; width:160px; height:220px; margin:25px auto; perspective: 600px;}";
  html += ".drink-base{width:100px; height:20px; background:#adb5bd; border-radius:3px 3px 50% 50%/0 0 10px 10px; position:absolute; bottom:0; left:30px;}"; // Saucer like base
  html += ".drink-glass{width:90px; height:150px; background:rgba(173, 216, 230, 0.25); border:2px solid rgba(135, 206, 235, 0.5); border-bottom:none; border-radius:15px 15px 0 0; /* More rounded top */ position:absolute; bottom:15px; /* Sits on base */ left:35px; display:flex; align-items:flex-end; justify-content:center; box-shadow: inset 0 0 10px rgba(0,0,0,0.05);}";
  html += ".drink-liquid{width:85%; height:65%; background:linear-gradient(to top, #fca311, #ffcc55); /* Amber/Orange drink color */ border-radius:0 0 12px 12px; margin-bottom:4%;}";
  html += ".coaster{width:130px; height:22px; background-color:#6c757d; /* Neutral gray */ border:1px solid #5a6268; border-radius:5px; position:absolute; top:30px; left:15px; transform-style:preserve-3d; transition: transform 0.2s ease-out, background-color 0.3s ease; display:flex; align-items:center; justify-content:center; font-size:10px; color:white; font-weight:bold;}";
  html += ".coaster.alert-coaster{background-color:#c82333; box-shadow: 0 0 15px #ff4757;}"; // Brighter red for alert

  html += ".settings-panel{margin-top:30px; padding-top:20px; border-top:1px solid #e0e0e0;}";
  html += "h2{font-size:1.4em; color:#0056b3; margin-bottom:15px;}";
  html += ".setting{display:flex; justify-content:space-between; align-items:center; margin-bottom:12px; padding:8px; background-color:#e9ecef; border-radius:6px;}";
  html += ".setting span{font-size:0.95em; font-weight:500;}";
  html += ".setting .buttons a{text-decoration:none; color:white; background-color:#007bff; padding: 5px 10px; border-radius:4px; margin-left:5px; font-weight:bold; font-size:1.1em; transition: background-color 0.2s;}";
  html += ".setting .buttons a:hover{background-color:#0056b3;}";
  html += ".setting .buttons a.dec{background-color:#6c757d;} .setting .buttons a.dec:hover{background-color:#5a6268;}";
  html += ".footer-text{font-size:0.8em; color:#6c757d; margin-top:25px;}";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>DrinkGuard</h1>";

  html += "<div class='visualization'>";
  html += "<div class='drink-base'></div>";
  html += "<div class='drink-glass'><div class='drink-liquid'></div></div>";
  html += "<div id='coaster' class='coaster'>GUARD</div>"; // Added text to coaster
  html += "</div>";

  html += "<p id='statusText' class='status-text'></p>";
  
  html += "<div class='settings-panel'><h2>Sensitivity Settings</h2>";
  // Tilt Threshold
  html += "<div class='setting'><span>Tilt Angle (&deg;): " + String(currentTiltThresholdDegrees, 1) + "</span><span class='buttons'>";
  html += "<a href='/updatesetting?param=tilt_thresh&action=dec' class='dec'>-</a><a href='/updatesetting?param=tilt_thresh&action=inc'>+</a></span></div>";
  // Tilt Duration
  html += "<div class='setting'><span>Tilt Duration (ms): " + String(currentSustainedTiltDurationMs) + "</span><span class='buttons'>";
  html += "<a href='/updatesetting?param=tilt_dur&action=dec' class='dec'>-</a><a href='/updatesetting?param=tilt_dur&action=inc'>+</a></span></div>";
  // Slide Threshold
  html += "<div class='setting'><span>Slide Force (m/s&sup2;): " + String(currentSlideThresholdAccel, 2) + "</span><span class='buttons'>";
  html += "<a href='/updatesetting?param=slide_thresh&action=dec' class='dec'>-</a><a href='/updatesetting?param=slide_thresh&action=inc'>+</a></span></div>";
  // Slide Duration
  html += "<div class='setting'><span>Slide Duration (ms): " + String(currentSustainedSlideDurationMs) + "</span><span class='buttons'>";
  html += "<a href='/updatesetting?param=slide_dur&action=dec' class='dec'>-</a><a href='/updatesetting?param=slide_dur&action=inc'>+</a></span></div>";
  html += "</div>"; // end settings-panel

  html += "<p class='footer-text'>DrinkGuard Active</p>"; // Simple footer
  html += "</div>"; // end container

  // JavaScript for animations and status update
  html += "<script>";
  html += "const motionActive = " + String(webShowsMotionActive ? "true" : "false") + ";\n";
  html += "const isTilt = " + String(isSustainedTilt ? "true" : "false") + ";\n";
  html += "const isSlide = " + String(isSustainedSlide ? "true" : "false") + ";\n";
  html += "const motionType = '" + String(lastMotionType) + "';\n";
  html += "const animR = " + String(animRoll, 1) + ";\n";
  html += "const animP = " + String(animPitch, 1) + ";\n";
  html += "const animSX = " + String(animSlideX, 1) + ";\n";

  html += "const statusTextEl = document.getElementById('statusText');\n";
  html += "const coasterEl = document.getElementById('coaster');\n";
  
  html += "let currentStatusClass = 'status-secure';\n";
  html += "let currentStatusText = 'SYSTEM ARMED & SECURE';\n";
  html += "let transformStyle = 'rotateX(0deg) rotateY(0deg) translateX(0px)';\n"; // Default resting state
  html += "coasterEl.classList.remove('alert-coaster');\n";


  html += "if(motionActive){\n";
  html += "  coasterEl.classList.add('alert-coaster');\n";
  html += "  if(motionType === 'Tilt'){\n";
  html += "    currentStatusText = 'ALERT: TILT DETECTED!';\n";
  html += "    currentStatusClass = 'status-alert-tilt';\n";
  // Clamp visual animation to avoid extreme flips, make intuitive
  html += "    let visualPitch = Math.max(-25, Math.min(25, animP));\n";
  html += "    let visualRoll = Math.max(-25, Math.min(25, animR));\n";
  html += "    transformStyle = 'rotateX(' + (visualPitch * 1) + 'deg) rotateY(' + visualRoll + 'deg) translateX(0px)';\n"; // Inverted pitch for better visual
  html += "  } else if (motionType === 'Slide'){\n";
  html += "    currentStatusText = 'ALERT: SLIDE DETECTED!';\n";
  html += "    currentStatusClass = 'status-alert-slide';\n";
  html += "    transformStyle = 'translateX(' + (animSX * 20) + 'px) rotateX(0deg) rotateY(0deg)';\n"; // Slide 20px based on animSX (-1, 0, or 1)
  html += "  } else { \n"; // Generic motion if type isn't tilt or slide specifically
  html += "    currentStatusText = 'ALERT: MOTION DETECTED!';\n";
  html += "    currentStatusClass = 'status-alert-general';\n";
  // Could add a subtle shake animation here
  html += "  }\n";
  html += "} else {\n";
    // Already set to defaults above
  html += "}\n";

  html += "statusTextEl.textContent = currentStatusText;\n";
  html += "statusTextEl.className = 'status-text ' + currentStatusClass;\n"; // Ensure base class is kept
  html += "coasterEl.style.transform = transformStyle;\n";

  html += "</script>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void serveNotFound() {
  String message = "<!DOCTYPE html><html><head><title>Not Found</title>";
  message += "<style>body{font-family:'Segoe UI',sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;background-color:#f4f7f6;color:#333;margin:0;}";
  message += ".message-box{text-align:center;padding:40px;background-color:white;border-radius:10px;box-shadow:0 4px 15px rgba(0,0,0,0.1);}</style></head>";
  message += "<body><div class='message-box'><h1>404 - Not Found</h1><p>Sorry, the page you're looking for doesn't exist.</p>";
  message += "<p><a href='/' style='color:#007bff;text-decoration:none;font-weight:bold;'>Return to DrinkGuard</a></p></div></body></html>";
  server.send(404, "text/html", message);
}

String formatDuration(unsigned long totalMillis) { // Currently unused in UI but kept
  unsigned long secs = totalMillis / 1000;
  unsigned long mins = secs / 60;
  unsigned long hrs = mins / 60;
  secs %= 60; mins %= 60; hrs %= 24;
  String T = "";
  if (hrs > 0) T += String(hrs) + "h ";
  T += String(mins) + "m "; T += String(secs) + "s";
  return T;
}
