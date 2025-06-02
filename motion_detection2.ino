//--------------------------------------------------------------------------------------------------
// Product:       MotionGuard Pro :: CORE FUNCTIONALITY TEST BUILD
// Focus:         Making settings and note saving WORK RELIABLY with minimal UI.
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
const char* WIFI_SSID         = "DevilDogVIP";
const char* WIFI_PASSWORD     = "yvetteway";
const char* WEB_AUTH_USER     = "DevilDogVIP";
const char* WEB_AUTH_PASSWORD = "yvetteway";

const int I2C_SDA_PIN = 22; const int I2C_SCL_PIN = 21;
#if defined(ESP32)
  const int LED_PIN = 13;
#elif defined(ESP8266)
  const int LED_PIN = 2;
#else
  const int LED_PIN = 2;
#endif

// --- Default Sensitivity Settings (Original Style) ---
const float DEFAULT_TILT_THRESHOLD_DEGREES     = 0.5f;
const unsigned long DEFAULT_SUSTAINED_TILT_DURATION_MS = 250;
const float DEFAULT_SLIDE_THRESHOLD_ACCEL      = 0.5f; // Using this for the simplified test
const unsigned long DEFAULT_SUSTAINED_SLIDE_DURATION_MS= 150; // Using this for the simplified test

// --- Runtime Adjustable Sensitivity Settings ---
float currentTiltThresholdDegrees     = DEFAULT_TILT_THRESHOLD_DEGREES;
unsigned long currentSustainedTiltDurationMs = DEFAULT_SUSTAINED_TILT_DURATION_MS;
// For simplicity, we'll only try to make these two adjustable in this test UI
float currentSlideThresholdAccel      = DEFAULT_SLIDE_THRESHOLD_ACCEL;
unsigned long currentSustainedSlideDurationMs= DEFAULT_SUSTAINED_SLIDE_DURATION_MS;

String globalUserNote = "Test note."; // User Notes

// Other System Parameters
const unsigned long TELEMETRY_READ_INTERVAL_MS = 50;
const unsigned long WEB_MOTION_INDICATOR_DURATION_MS = 1000;
const unsigned long LED_MOTION_INDICATOR_DURATION_MS = 500;
const int           MAX_LOG_ENTRIES                  = 5; // Reduced for minimal test
const unsigned long MIN_LOG_INTERVAL_MS              = 300;
// const float         WEB_PAGE_REFRESH_SECONDS         = 1.5; // META REFRESH REMOVED

// Global Objects & State (shortened for brevity, ensure correct WebServer for your board)
Adafruit_MPU6050 mpu;
#if defined(ESP32)
  WebServer server(80);
#elif defined(ESP8266)
  ESP8266WebServer server(80);
#endif
sensors_event_t currentAcceleration, currentGyro, currentTemperature;
float currentRoll_deg = 0.0, currentPitch_deg = 0.0; unsigned long lastMotionTimestamp_ms = 0;
bool webShowsMotionActive = false; bool ledIndicatesMotion = false; bool isSustainedTilt = false; bool isSustainedSlide = false;
// ... (other state variables as before, ensure they are declared)
float prevTiltRoll_deg = 0.0, prevTiltPitch_deg = 0.0; bool firstTiltCheck = true; unsigned long sustainedTiltStartTime_ms = 0;
float prevSlideAccelX = 0.0, prevSlideAccelY = 0.0; bool firstSlideCheck = true; unsigned long sustainedSlideStartTime_ms = 0;
struct MotionLogEntry { unsigned long timestamp_ms; String motionType; sensors_event_t accelData, gyroData, tempData; float rollAtDetection_deg, pitchAtDetection_deg; };
std::vector<MotionLogEntry> motionEventLog; unsigned long lastLogEntryTime_ms = 0;


// Function Prototypes
void initializeSensor(); void initializeNetworkServices(); void processSensorData(); void detectSustainedMotionEvents();
void updateSystemIndicatorsAndLogging(unsigned long); void manageLedIndicator();
void serveRootPage(); void serveLogDownload(); void handleUpdateSettings(); void handleResetSettings(); void handleUpdateNote(); void handleClearLog();
void serveNotFound(); String formatTime(unsigned long); String formatDuration(unsigned long);
void addEventToMotionLog(unsigned long, const String&, const sensors_event_t&, const sensors_event_t&, const sensors_event_t&, float, float);

void setup(void) {
  Serial.begin(115200); unsigned long t_setup = millis(); while (!Serial && (millis() - t_setup < 2000));
  Serial.println("\n[MotionGuard Pro :: CORE FUNCTIONALITY TEST BUILD]");
  pinMode(LED_PIN, OUTPUT);
  #if defined(ESP8266) && (LED_PIN == 2 || LED_PIN == 0)
    digitalWrite(LED_PIN, HIGH);
  #else
    digitalWrite(LED_PIN, LOW);
  #endif
  initializeSensor(); initializeNetworkServices();
  Serial.println("[SYSTEM] Initial Default Sensitivity:");
  Serial.printf("  Tilt Thresh: %.1f deg, Tilt Duration: %lu ms\n", DEFAULT_TILT_THRESHOLD_DEGREES, DEFAULT_SUSTAINED_TILT_DURATION_MS);
  Serial.printf("  Slide Thresh: %.2f m/s^2, Slide Duration: %lu ms\n", DEFAULT_SLIDE_THRESHOLD_ACCEL, DEFAULT_SUSTAINED_SLIDE_DURATION_MS);
  Serial.println("[SYSTEM] Initialization Complete.");
  if (WiFi.status() == WL_CONNECTED) { Serial.print("Dashboard: http://"); Serial.println(WiFi.localIP()); }
}

void loop() {
  server.handleClient(); unsigned long cM_loop = millis(); static unsigned long lTRT_loop = 0;
  if (cM_loop - lTRT_loop >= TELEMETRY_READ_INTERVAL_MS) { lTRT_loop = cM_loop; processSensorData(); detectSustainedMotionEvents(); }
  updateSystemIndicatorsAndLogging(cM_loop); manageLedIndicator(); delay(5);
}

// --- Initialization, Sensor, Motion Detection, Indicators, Logging, Time Formatting ---
// (Use the full robust versions of these functions from the previous "Feature Enhanced" or "Precision & Clarity" editions)
// For brevity here, I'm not repeating all of them, but ensure you use the complete, correct functions.
void initializeSensor() { /* ... full code ... */ }
void initializeNetworkServices() {
  Serial.println("  Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); int att_wifi = 0;
  while (WiFi.status() != WL_CONNECTED && att_wifi < 30) { delay(500); Serial.print("."); att_wifi++; }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n  WiFi Connected. IP: " + WiFi.localIP().toString());
    server.on("/", HTTP_GET, serveRootPage);
    // server.on("/downloadlog.csv", HTTP_GET, serveLogDownload); // Temporarily disable less critical routes
    server.on("/updatesettings", HTTP_POST, handleUpdateSettings);
    server.on("/resetsettings", HTTP_POST, handleResetSettings);
    server.on("/updatenote", HTTP_POST, handleUpdateNote);
    // server.on("/clearlog", HTTP_POST, handleClearLog); // Temporarily disable
    server.onNotFound(serveNotFound);
    server.begin(); Serial.println("  Web Server Started.");
  } else { Serial.println("\n>>> CRITICAL: WiFi FAILED. HALTED. <<<"); while (true) { /* ... */ }}
}
void processSensorData() { /* ... full code ... */ }
void detectSustainedMotionEvents() { /* ... full code using current... variables ... */ }
void updateSystemIndicatorsAndLogging(unsigned long cM_log) { /* ... full code ... */ }
void manageLedIndicator() { /* ... full code ... */ }
void addEventToMotionLog(unsigned long ts, const String& type, const sensors_event_t& a, const sensors_event_t& g, const sensors_event_t& t, float r, float p) { /* ... full code ... */ }
String formatTime(unsigned long ms_time) { /* ... full code ... */ }
String formatDuration(unsigned long totalMillis) { /* ... full code ... */ }


// --- Web Server Handlers ---
void handleUpdateSettings() {
  if (!server.authenticate(WEB_AUTH_USER, WEB_AUTH_PASSWORD)) return server.requestAuthentication();
  Serial.println("\n[SETTINGS DEBUG] POST /updatesettings received.");
  bool changed = false; String log = "[Settings Applied] ";
  float pSTh=currentSlideThresholdAccel; unsigned long pSDur=currentSustainedSlideDurationMs; // Only checking these two for the test

  for (uint8_t i=0; i<server.args(); i++) {
    String name=server.argName(i); String valStr=server.arg(i);
    Serial.printf("  [DEBUG] Raw Form Arg: %s = '%s'\n", name.c_str(), valStr.c_str());
    // ONLY TESTING SLIDE SETTINGS FOR SIMPLICITY
    if(name=="slide_thresh_accel"){float v=valStr.toFloat();if(v>=0.05f&&v<=5.0f)currentSlideThresholdAccel=v;else Serial.printf("    [WARN] %s '%.2f' out of range(0.05-5.0)\n",name.c_str(),v);}
    else if(name=="slide_duration_ms"){unsigned long v=strtoul(valStr.c_str(),NULL,10);if(v>=50&&v<=3000)currentSustainedSlideDurationMs=v;else Serial.printf("    [WARN] %s '%lu' out of range(50-3000)\n",name.c_str(),v);}
  }
  if(std::abs(currentSlideThresholdAccel-pSTh)>0.001f){changed=true;log+="SlideThresh="+String(currentSlideThresholdAccel,2)+" ";}
  if(currentSustainedSlideDurationMs!=pSDur){changed=true;log+="SlideDur="+String(currentSustainedSlideDurationMs)+" ";}

  if(changed)Serial.println(log);else Serial.println("[Settings Update] No effective changes made for tested params.");
  Serial.printf("[SETTINGS AFTER UPDATE] SlideT:%.2f, SlideD:%lu (Tilt settings unchanged in this test)\n",currentSlideThresholdAccel,currentSustainedSlideDurationMs);
  server.sendHeader("Location","/",true); server.send(302,"text/plain","Updating..."); 
}

void handleResetSettings() {
  if (!server.authenticate(WEB_AUTH_USER, WEB_AUTH_PASSWORD)) return server.requestAuthentication();
  // Reset ALL settings even if UI only shows a few for test
  currentTiltThresholdDegrees=DEFAULT_TILT_THRESHOLD_DEGREES; currentSustainedTiltDurationMs=DEFAULT_SUSTAINED_TILT_DURATION_MS;
  currentSlideThresholdAccel=DEFAULT_SLIDE_THRESHOLD_ACCEL; currentSustainedSlideDurationMs=DEFAULT_SUSTAINED_SLIDE_DURATION_MS;
  Serial.println("[SETTINGS] RESET TO ALL DEFAULTS.");
  Serial.printf("[SETTINGS AFTER RESET] TiltT:%.1f,TiltD:%lu, SlideT:%.2f,SlideD:%lu\n",currentTiltThresholdDegrees,currentSustainedTiltDurationMs,currentSlideThresholdAccel,currentSustainedSlideDurationMs);
  server.sendHeader("Location","/",true); server.send(302,"text/plain","Resetting..."); 
}

void handleUpdateNote() {
  if (!server.authenticate(WEB_AUTH_USER, WEB_AUTH_PASSWORD)) return server.requestAuthentication();
  Serial.println("\n[NOTES DEBUG] POST /updatenote received.");
  if (server.hasArg("usernote")) {
    globalUserNote = server.arg("usernote");
    if (globalUserNote.length() > 100) {globalUserNote = globalUserNote.substring(0, 100); Serial.println("[NOTES] User note truncated to 100 chars for test.");}
    Serial.println("[NOTES] User note updated. New note (first 50 chars): '" + globalUserNote.substring(0,50) + "'");
  } else {
    Serial.println("[NOTES DEBUG] 'usernote' argument not found in POST request.");
  }
  server.sendHeader("Location","/",true); server.send(302,"text/plain","Note Updated.");
}

// Temporarily disable clear log and download log to reduce routes & HTML string
void handleClearLog() { server.send(404, "text/plain", "Temporarily disabled"); }
void serveLogDownload() { server.send(404, "text/plain", "Temporarily disabled"); }


void serveRootPage() {
  if (!server.authenticate(WEB_AUTH_USER, WEB_AUTH_PASSWORD)) return server.requestAuthentication();
  Serial.printf("\n[PAGE LOAD DEBUG] Serving root. SlideT:%.2f, SlideD:%lu. Note: '%.20s...'\n",
    currentSlideThresholdAccel, currentSustainedSlideDurationMs, globalUserNote.c_str());

  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
  html += "<title>" + String(webShowsMotionActive ? "❗️MOTION!" : "✅ Standby") + " - MPU Test</title>";
  // NO META REFRESH html += "<meta http-equiv='refresh' content='" + String(WEB_PAGE_REFRESH_SECONDS, 1) + "'>"; 
  html += "<style>";
  html += "body{font-family:sans-serif; margin:20px; background-color:#f4f4f4; color:#333;}";
  html += "h1,h2{color:#0056b3;} .section{background-color:#fff; padding:15px; border-radius:8px; margin-bottom:15px; box-shadow: 0 2px 4px rgba(0,0,0,0.1);}";
  html += "label{display:block; margin-top:10px; font-weight:bold;} input[type='number'], textarea{width:200px; padding:8px; margin-top:5px; border:1px solid #ccc; border-radius:4px;}";
  html += "button{background-color:#007bff; color:white; padding:10px 15px; border:none; border-radius:5px; cursor:pointer; margin-top:10px;} button:hover{background-color:#0056b3;}";
  html += ".status-motion{color:red; font-weight:bold;} .status-no-motion{color:green;}";
  html += "ul{list-style-type:none; padding-left:0;} li{background-color:#eee; margin-bottom:5px; padding:5px;}";
  html += "</style></head><body>";
  
  html += "<div class='section'><h1>MotionGuard Pro - Core Test</h1>";
  html += "<p class='status " + String(webShowsMotionActive ? "status-motion" : "status-no-motion") + "'>";
  html += webShowsMotionActive ? "MOTION DETECTED!" : "All Quiet";
  html += "</p></div>";

  html += "<div class='section'><h2>Live Telemetry (Minimal)</h2>";
  html += "<p>Roll: " + String(currentRoll_deg, 1) + "&deg;, Pitch: " + String(currentPitch_deg, 1) + "&deg;</p>";
  html += "<p>Accel X: " + String(currentAcceleration.acceleration.x, 2) + ", Y: " + String(currentAcceleration.acceleration.y, 2) + ", Z: " + String(currentAcceleration.acceleration.z, 2) + " m/s&sup2;</p>";
  html += "<p>Temp: " + String(currentTemperature.temperature, 1) + "&deg;C</p>";
  html += "</div>";

  // --- MINIMAL SETTINGS FORM ---
  html += "<div class='section'><h2>Test Settings (Slide Only)</h2>";
  html += "<form method='POST' action='/updatesettings'>";
  html += "<label for='slide_thresh_accel_in'>Slide Threshold (0.05-5.0 m/s&sup2;):</label>";
  html += "<input type='number' id='slide_thresh_accel_in' name='slide_thresh_accel' step='0.05' min='0.05' max='5.0' value='" + String(currentSlideThresholdAccel, 2) + "'>";
  html += "<span style='font-size:0.8em; color:#555;'> (Default: " + String(DEFAULT_SLIDE_THRESHOLD_ACCEL, 2) + ")</span><br>";
  
  html += "<label for='slide_duration_ms_in'>Slide Duration (50-3000 ms):</label>";
  html += "<input type='number' id='slide_duration_ms_in' name='slide_duration_ms' step='50' min='50' max='3000' value='" + String(currentSustainedSlideDurationMs) + "'>";
  html += "<span style='font-size:0.8em; color:#555;'> (Default: " + String(DEFAULT_SUSTAINED_SLIDE_DURATION_MS) + ")</span><br>";
  html += "<button type='submit'>Save Test Settings</button>";
  html += "</form>";
  // Reset Button Form
  html += "<form method='POST' action='/resetsettings' style='margin-top:10px;'><button type='submit' style='background-color:#ffc107; color:black;'>Reset All Settings to Defaults</button></form>";
  html += "</div>";

  // --- MINIMAL NOTES FORM ---
  html += "<div class='section'><h2>Test User Note</h2>";
  html += "<form method='POST' action='/updatenote'>";
  html += "<label for='usernote_area'>Your Note (max 100 chars):</label>";
  html += "<textarea id='usernote_area' name='usernote' rows='3' maxlength='100'>" + globalUserNote + "</textarea><br>";
  html += "<button type='submit'>Save Note</button>";
  html += "</form></div>";
  
  html += "<div class='section'><h2>Motion Log (Max " + String(MAX_LOG_ENTRIES) + ")</h2>";
  if (motionEventLog.empty()) { html += "<p>No motion events recorded yet.</p>"; }
  else {
    html += "<ul>";
    for (int i = motionEventLog.size() - 1; i >= 0; i--) { 
      const MotionLogEntry& entry = motionEventLog[i];
      html += "<li>" + formatTime(entry.timestamp_ms) + " - " + entry.motionType + " (R:" + String(entry.rollAtDetection_deg,1) + ", P:" + String(entry.pitchAtDetection_deg,1) + ")</li>";
    } html += "</ul>";
  } html += "</div>"; 
  
  html += "<p>System Uptime: " + formatDuration(millis()) + "</p>";
  html += "</body></html>"; 
  server.send(200, "text/html", html);
}

void serveNotFound() {
  server.send(404, "text/plain", "404: Not Found. This is a barebones test server.");
}