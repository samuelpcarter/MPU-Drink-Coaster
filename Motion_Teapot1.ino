//--------------------------------------------------------------------------------------------------
// Product:        DrinkGuard Coaster :: Teapot Demo Visualization v4
// Focus:          3D cuboid visualization of sensor orientation (roll, pitch, yaw),
//                 Apple-like/peaches aesthetic, relative orientation, less sensitive motion detection.
// Revision Notes: Added yaw calculation (gyro-based). Implemented 3D cuboid in HTML/CSS/JS.
//                 New UI with reset button. Adjusted sensitivity to be lower.
//                 Background cup element. System UI font stack.
//--------------------------------------------------------------------------------------------------

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <cmath>
#include <cstdio>

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

const int I2C_SDA_PIN = 22; // ESP32 default
const int I2C_SCL_PIN = 21; // ESP32 default
#if defined(ESP32)
  const int LED_PIN = 13;
#elif defined(ESP8266)
  const int LED_PIN = 2; // D4 on NodeMCU
#else
  const int LED_PIN = LED_BUILTIN; // Fallback
#endif

// --- Fixed Sensitivity Settings (LESS SENSITIVE) ---
const float DEFAULT_TILT_THRESHOLD_DEGREES        = 4.0f;  // Increased for less sensitivity
const unsigned long DEFAULT_SUSTAINED_TILT_DURATION_MS = 500;  // Increased
const float DEFAULT_SLIDE_MAGNITUDE_THRESHOLD_ACCEL = 0.7f; // m/s^2 - Increased
const unsigned long DEFAULT_SUSTAINED_SLIDE_DURATION_MS= 1000; // Increased

// System Parameters
const unsigned long TELEMETRY_READ_INTERVAL_MS = 50;    // Sensor read frequency (20Hz)
const unsigned long WEB_PAGE_REFRESH_INTERVAL_MS = 100; // UI refresh (10Hz - faster for smoother 3D)
const unsigned long WEB_MOTION_INDICATOR_DURATION_MS = 2500;
const unsigned long LED_MOTION_INDICATOR_DURATION_MS = 1500;

// Global Objects & State
Adafruit_MPU6050 mpu;
#if defined(ESP32)
  WebServer server(80);
#elif defined(ESP8266)
  ESP8266WebServer server(80);
#endif

sensors_event_t currentAcceleration, currentGyro, currentTemperature;
float currentRoll_deg = 0.0, currentPitch_deg = 0.0, currentYaw_deg = 0.0;
float baseRoll_deg = 0.0, basePitch_deg = 0.0; // For "start flat" relative orientation
bool baseOrientationSet = false;

unsigned long lastMotionTimestamp_ms = 0;
bool webShowsMotionActive = false;
bool ledIndicatesMotion = false;
bool isSustainedTilt = false;
bool isSustainedSlide = false;
String lastMotionType = "None";

float prevTiltRoll_deg = 0.0, prevTiltPitch_deg = 0.0;
bool firstTiltCheck = true;
unsigned long sustainedTiltStartTime_ms = 0;
unsigned long sustainedSlideStartTime_ms = 0;
float lastGyroZ = 0.0;


// Function Prototypes
void initializeSensor();
void initializeNetworkServices();
void processSensorData();
void detectSustainedMotionEvents();
void updateSystemIndicators(unsigned long currentTime);
void manageLedIndicator();
void serveRootPage();
void handleResetOrientation();
void serveNotFound();

void setup(void) {
  Serial.begin(115200);
  unsigned long setupStartTime = millis();
  while (!Serial && (millis() - setupStartTime < 2000));

  Serial.println("\n[DrinkGuard Coaster :: Teapot Demo v4]");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  initializeSensor();
  // Initial sensor read to establish baseline if possible
  if (mpu.getMotionInterruptStatus()) { // Clear any pending interrupts
      // Could also do a few dummy reads
  }
  processSensorData(); // Get initial readings
  
  // Base orientation will be set on first data send or by reset
  // baseRoll_deg = currentRoll_deg;
  // basePitch_deg = currentPitch_deg;
  // currentYaw_deg = 0.0; // Yaw always starts at 0 relative
  // baseOrientationSet = true;
  // Serial.printf("[SYSTEM] Initial base orientation: R:%.1f P:%.1f\n", baseRoll_deg, basePitch_deg);


  initializeNetworkServices();

  Serial.println("[SYSTEM] Fixed Sensitivity Settings (Low Sensitivity):");
  Serial.printf("  Tilt Thresh: %.2f deg, Tilt Duration: %lu ms\n", DEFAULT_TILT_THRESHOLD_DEGREES, DEFAULT_SUSTAINED_TILT_DURATION_MS);
  Serial.printf("  Slide Mag Thresh: %.2f m/s^2, Slide Duration: %lu ms\n", DEFAULT_SLIDE_MAGNITUDE_THRESHOLD_ACCEL, DEFAULT_SUSTAINED_SLIDE_DURATION_MS);
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
     if (!baseOrientationSet) { // Set base orientation on the first valid data after setup
        baseRoll_deg = currentRoll_deg;
        basePitch_deg = currentPitch_deg;
        currentYaw_deg = 0.0; // Yaw always starts at 0 relative
        baseOrientationSet = true;
        Serial.printf("[SYSTEM] Initial base orientation set: R:%.1f P:%.1f\n", baseRoll_deg, basePitch_deg);
    }
  }
  updateSystemIndicators(currentTime_ms);
  manageLedIndicator();
  delay(1);
}

void initializeSensor() {
  Serial.println("  Initializing MPU6050 Sensor...");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!mpu.begin()) {
    Serial.println(">>> CRITICAL: MPU6050 FAILED. HALTED. <<<");
    while (true) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(100); }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG); // Standard range
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); // Moderate filter
  //mpu.setSampleRateDivisor(TELEMETRY_READ_INTERVAL_MS) // This is not how it works, sample rate is internal
  Serial.println("  MPU6050 Initialized.");
  delay(100); // Allow sensor to settle
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
    server.on("/resetorientation", HTTP_POST, handleResetOrientation); // POST to signify action
    server.onNotFound(serveNotFound);
    server.begin();
    Serial.println("  Web Server Started.");
  } else {
    Serial.println("\n>>> CRITICAL: WiFi FAILED. HALTED. <<<");
    while (true) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(200); }
  }
}

void processSensorData() {
  mpu.getEvent(&currentAcceleration, &currentGyro, &currentTemperature);

  // Roll and Pitch from accelerometer (absolute reference to gravity)
  currentRoll_deg = atan2(currentAcceleration.acceleration.y, currentAcceleration.acceleration.z) * 180.0 / M_PI;
  currentPitch_deg = atan2(-currentAcceleration.acceleration.x, sqrt(currentAcceleration.acceleration.y * currentAcceleration.acceleration.y + currentAcceleration.acceleration.z * currentAcceleration.acceleration.z)) * 180.0 / M_PI;

  // Yaw from gyroscope integration
  // Gyro data is in rad/s. Convert to deg/s: currentGyro.gyro.z * (180.0 / M_PI)
  // Then multiply by time interval (TELEMETRY_READ_INTERVAL_MS / 1000.0f)
  float gyroZ_rad_s = currentGyro.gyro.z;
  // Simple complementary filter could be added here if roll/pitch from gyro were also used.
  // For now, just gyro Z for yaw.
  currentYaw_deg += gyroZ_rad_s * (TELEMETRY_READ_INTERVAL_MS / 1000.0f) * (180.0 / M_PI);
  // Optional: Keep yaw within a specific range, e.g., -180 to 180 or 0 to 360
  // currentYaw_deg = fmod(currentYaw_deg, 360.0f);
  // if (currentYaw_deg < 0) currentYaw_deg += 360.0f;
  lastGyroZ = gyroZ_rad_s; // Store for debugging or advanced filtering
}

void handleResetOrientation() {
  Serial.println("[ACTION] Reset Orientation Requested by Client.");
  baseRoll_deg = currentRoll_deg;
  basePitch_deg = currentPitch_deg;
  currentYaw_deg = 0.0; // Reset accumulated yaw
  baseOrientationSet = true; // Ensure it's marked as set
  Serial.printf("New base orientation: R:%.1f P:%.1f Y:%.1f\n", baseRoll_deg, basePitch_deg, currentYaw_deg);
  server.send(200, "text/plain", "Orientation Reset");
}

void detectSustainedMotionEvents() {
  unsigned long currentTime = millis();
  bool tiltDetectedThisCycle = false;
  bool slideDetectedThisCycle = false;

  // --- Tilt Detection (using absolute roll/pitch relative to initial base) ---
  float effectiveRoll = currentRoll_deg - baseRoll_deg;
  float effectivePitch = currentPitch_deg - basePitch_deg;
  
  // Check against absolute thresholds from the calibrated "flat"
  if (std::abs(effectiveRoll) > DEFAULT_TILT_THRESHOLD_DEGREES || std::abs(effectivePitch) > DEFAULT_TILT_THRESHOLD_DEGREES) {
    if (sustainedTiltStartTime_ms == 0) {
      sustainedTiltStartTime_ms = currentTime;
    } else if (currentTime - sustainedTiltStartTime_ms >= DEFAULT_SUSTAINED_TILT_DURATION_MS) {
      if (!isSustainedTilt) {
        isSustainedTilt = true;
        tiltDetectedThisCycle = true;
        lastMotionType = "Tilt";
        Serial.printf("[MOTION] Tilt Detected: Eff_R:%.1f Eff_P:%.1f\n", effectiveRoll, effectivePitch);
      }
    }
  } else {
    sustainedTiltStartTime_ms = 0;
  }

  // --- Slide Detection (horizontal acceleration magnitude) ---
  float accelX = currentAcceleration.acceleration.x;
  float accelY = currentAcceleration.acceleration.y;
  // Assuming Z is mainly gravity. For slide, we are interested in X and Y plane.
  // If device is tilted, gravity components will appear in X and Y.
  // For simplicity, still using raw X, Y here. A more advanced approach would remove gravity vector.
  float horizontalAccelMag = sqrt(accelX * accelX + accelY * accelY);

  if (horizontalAccelMag > DEFAULT_SLIDE_MAGNITUDE_THRESHOLD_ACCEL) {
    if (sustainedSlideStartTime_ms == 0) {
      sustainedSlideStartTime_ms = currentTime;
    } else if (currentTime - sustainedSlideStartTime_ms >= DEFAULT_SUSTAINED_SLIDE_DURATION_MS) {
      if (!isSustainedSlide) {
        isSustainedSlide = true;
        slideDetectedThisCycle = true;
        lastMotionType = "Slide";
        Serial.printf("[MOTION] Slide Detected: Mag:%.2f (X:%.2f Y:%.2f)\n", horizontalAccelMag, accelX, accelY);
      }
    }
  } else {
    sustainedSlideStartTime_ms = 0;
  }

  if (tiltDetectedThisCycle || slideDetectedThisCycle) {
    lastMotionTimestamp_ms = currentTime;
  }
}

void updateSystemIndicators(unsigned long currentTime_ms) {
  if (currentTime_ms - lastMotionTimestamp_ms < WEB_MOTION_INDICATOR_DURATION_MS && lastMotionTimestamp_ms != 0) {
    webShowsMotionActive = true;
  } else {
    webShowsMotionActive = false;
    if (isSustainedTilt || isSustainedSlide) {
        // Serial.println("[SYSTEM] Motion indicators reset."); // Less verbose
    }
    isSustainedTilt = false;
    isSustainedSlide = false;
    if (!webShowsMotionActive && lastMotionType != "None") {
        lastMotionType = "None";
    }
  }

  if (currentTime_ms - lastMotionTimestamp_ms < LED_MOTION_INDICATOR_DURATION_MS && lastMotionTimestamp_ms != 0) {
    ledIndicatesMotion = true;
  } else {
    ledIndicatesMotion = false;
  }
}

void manageLedIndicator() {
#if defined(ESP8266) && (LED_PIN == 2)
  digitalWrite(LED_PIN, ledIndicatesMotion ? LOW : HIGH); // NodeMCU D4 LED is active LOW
#else
  digitalWrite(LED_PIN, ledIndicatesMotion ? HIGH : LOW);
#endif
}

void serveRootPage() {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='" + String(WEB_PAGE_REFRESH_INTERVAL_MS / 1000.0, 2) + "'>"; // Faster refresh
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0, user-scalable=no'>";
  html += "<title>DrinkGuard 3D Viewer</title>";
  
  html += "<style>";
  html += "body{font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; margin:0; padding:0; background-color:#f4f6f8; color:#333; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:100vh; text-align:center; overflow:hidden; position: relative;}";
  html += ".background-cup { position: absolute; bottom: 20px; left: 50%; transform: translateX(-50%); width: 150px; height: 100px; background-color: #e0e5e9; border-radius: 0 0 60px 60px; opacity: 0.5; z-index: 0; }";
  html += ".background-cup::before { content: ''; position: absolute; top: -10px; left: 50%; transform: translateX(-50%); width: 60px; height: 20px; border: 10px solid #e0e5e9; border-bottom: none; border-radius: 30px 30px 0 0; box-sizing: border-box; }";

  html += ".container{background-color:rgba(255,255,255,0.85); backdrop-filter: blur(10px); -webkit-backdrop-filter: blur(10px); padding:25px; border-radius:20px; box-shadow:0 8px 32px rgba(0,0,0,0.1); width:90%; max-width:380px; z-index: 1; position:relative;}";
  html += "h1{color:#2c3e50; margin-top:0; margin-bottom:15px; font-size:1.8em; font-weight:500;}";
  
  // Scene for 3D object
  html += ".scene{width: 200px; height: 200px; margin: 20px auto; perspective: 600px; border: 0px solid #ddd; border-radius:10px; background-color: rgba(230,230,240,0.1); }";
  html += ".cuboid{width:100%; height:100%; position:relative; transform-style:preserve-3d; transform: rotateX(0deg) rotateY(0deg) rotateZ(0deg); transition: transform 0.08s linear;}"; // Faster transition for smoothness
  html += ".cuboid__face{position:absolute; border: 1px solid rgba(0,0,0,0.2); font-size:12px; color:white; text-align:center; display:flex; align-items:center; justify-content:center; font-weight:bold; opacity:0.9;}";
  
  // Cuboid dimensions (example: 120px width, 80px height, 20px depth)
  html += ".cuboid__face--front  { width:120px; height:80px; background:#ff8a65; transform: rotateY(  0deg) translateZ(10px); }"; // Peach
  html += ".cuboid__face--back   { width:120px; height:80px; background:#ffab91; transform: rotateY(180deg) translateZ(10px); }"; // Lighter Peach
  html += ".cuboid__face--right  { width:20px;  height:80px; background:#ff7043; transform: rotateY( 90deg) translateZ(60px); }"; // Darker Peach side
  html += ".cuboid__face--left   { width:20px;  height:80px; background:#ff7043; transform: rotateY(-90deg) translateZ(60px); }"; // Darker Peach side
  html += ".cuboid__face--top    { width:120px; height:20px; background:#ffa726; transform: rotateX( 90deg) translateZ(40px); }"; // Orange-Peach top
  html += ".cuboid__face--bottom { width:120px; height:20px; background:#fb8c00; transform: rotateX(-90deg) translateZ(40px); }"; // Darker Orange-Peach bottom

  html += "p.status-text{font-size:1.1em; margin:15px 0 10px; min-height:24px; font-weight:400; transition: color 0.3s ease;}";
  html += ".status-secure{color:#27AE60;} .status-alert{color:#E74C3C;}";
  html += ".button-container{margin-top:15px;}";
  html += "button{background-color:#007aff; color:white; border:none; padding:10px 18px; border-radius:8px; font-size:0.9em; font-weight:500; cursor:pointer; transition: background-color 0.2s ease;}";
  html += "button:hover{background-color:#0056b3;}";
  html += ".footer-text{font-size:0.7em; color:#7f8c8d; margin-top:20px;}";
  html += "</style></head><body>";
  
  html += "<div class='background-cup'></div>";
  html += "<div class='container'>";
  html += "<h1>DrinkGuard 3D</h1>";

  html += "<div class='scene'><div id='cuboid' class='cuboid'>";
  html += "<div class='cuboid__face cuboid__face--front'>Front</div>";
  html += "<div class='cuboid__face cuboid__face--back'>Back</div>";
  html += "<div class='cuboid__face cuboid__face--right'>R</div>";
  html += "<div class='cuboid__face cuboid__face--left'>L</div>";
  html += "<div class='cuboid__face cuboid__face--top'>Top</div>";
  html += "<div class='cuboid__face cuboid__face--bottom'>Bot</div>";
  html += "</div></div>";

  html += "<p id='orientationData'>Roll: 0.0&deg;, Pitch: 0.0&deg;, Yaw: 0.0&deg;</p>";
  html += "<p id='statusText' class='status-text status-secure'>SYSTEM ARMED</p>";
  html += "<div class='button-container'><button onclick='resetOrientation()'>Reset View</button></div>";
  html += "<p class='footer-text'>Gyro-based yaw will drift over time.</p>";
  html += "</div>"; 

  html += "<script>";
  html += "const cuboid = document.getElementById('cuboid');\n";
  html += "const statusTextEl = document.getElementById('statusText');\n";
  html += "const orientationDataEl = document.getElementById('orientationData');\n";

  // Data from ESP (will be replaced by actual values)
  html += "const motionActive = " + String(webShowsMotionActive ? "true" : "false") + ";\n";
  html += "const lastMotionType = '" + String(lastMotionType) + "';\n";
  html += "let roll = " + String(currentRoll_deg - baseRoll_deg, 1) + ";\n";
  html += "let pitch = " + String(currentPitch_deg - basePitch_deg, 1) + ";\n";
  html += "let yaw = " + String(currentYaw_deg, 1) + ";\n"; // Yaw is already relative

  html += "if(cuboid) { cuboid.style.transform = 'rotateX(' + (-pitch) + 'deg) rotateY(' + (yaw) + 'deg) rotateZ(' + (-roll) + 'deg)'; }\n";
  // Note on rotation: The axes and signs might need adjustment based on MPU6050 orientation and desired visual mapping.
  // Common mapping: X-axis for pitch, Y-axis for yaw, Z-axis for roll.
  // -pitch for rotateX because positive pitch (nose up) often means positive rotation around sensor's Y-axis, which might map to negative rotateX in CSS view.
  // -roll for rotateZ because positive roll (right wing down) often means positive rotation around sensor's X-axis.

  html += "if(orientationDataEl) { orientationDataEl.textContent = 'Roll: ' + roll.toFixed(1) + '°, Pitch: ' + pitch.toFixed(1) + '°, Yaw: ' + yaw.toFixed(1) + '°';}\n";

  html += "if(motionActive){\n";
  html += "  statusTextEl.textContent = 'ALERT: ' + lastMotionType.toUpperCase() + ' DETECTED!';\n";
  html += "  statusTextEl.className = 'status-text status-alert';\n";
  html += "} else {\n";
  html += "  statusTextEl.textContent = 'SYSTEM ARMED';\n";
  html += "  statusTextEl.className = 'status-text status-secure';\n";
  html += "}\n";

  html += "function resetOrientation() {\n";
  html += "  fetch('/resetorientation', { method: 'POST' })\n";
  html += "    .then(response => response.text())\n";
  html += "    .then(data => { console.log(data); /* Optional: update UI immediately or wait for refresh */ })\n";
  html += "    .catch(error => console.error('Error resetting orientation:', error));\n";
  html += "}\n";
  html += "</script>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void serveNotFound() {
  String message = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
  message += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  message += "<title>404 Not Found</title>";
  message += "<style>body{font-family:-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;display:flex;flex-direction:column;justify-content:center;align-items:center;height:100vh;background-color:#f4f6f8;color:#333;margin:0;text-align:center;}";
  message += ".message-box{padding:30px;background-color:white;border-radius:15px;box-shadow:0 6px 20px rgba(0,0,0,0.08);}";
  message += "h1{color:#2c3e50; font-size:2em; margin-bottom:10px; font-weight:500;}";
  message += "p{font-size:1em; margin-bottom:20px;}";
  message += "a{color:#007aff;text-decoration:none;font-weight:500;} a:hover{text-decoration:underline;}</style></head>";
  message += "<body><div class='message-box'><h1>404 - Not Found</h1><p>The page you are looking for doesn't exist.</p>";
  message += "<p><a href='/'>Return to 3D Viewer</a></p></div></body></html>";
  server.send(404, "text/html", message);
}
