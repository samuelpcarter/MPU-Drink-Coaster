#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <vector>
#include <math.h>  // For atan2, sqrt, M_PI.
#include <stdio.h> // For sprintf

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h> // For ESP32 WebServer
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  // ESP8266WebServer server(80); // If using ESP8266, declare server object like this globally
#else
  #error "This sketch is designed for ESP32 or ESP8266. Please select an appropriate board."
#endif

// --- I2C Pins for MPU6050 ---
// Corrected to match your wiring: SCL to D21 (GPIO21), SDA to D22 (GPIO22)
const int SDA_PIN = 22; // SDA is on GPIO22
const int SCL_PIN = 21; // SCL is on GPIO21


// --- Wi-Fi Credentials ---
const char* ssid = "DevilDogVIP";     // Your Wi-Fi Network Name
const char* password = "yvetteway";   // Your Wi-Fi Network Password

// --- Web Server Credentials ---
const char* webServerUsername = "DevilDogVIP"; // Username for webpage access
const char* webServerPassword = "yvetteway"; // Password for webpage access

// --- LED Configuration ---
#if defined(ESP32)
  const int LED_PIN = 13; // Common built-in LED for ESP32 Feather (often red)
#elif defined(ESP8266)
  const int LED_PIN = 0;  // Or 2 for other ESP8266 Feather variants, often active LOW
#else
  const int LED_PIN = 2;  // Generic fallback, update if needed
#endif


// --- MPU6050 & Telemetry ---
Adafruit_MPU6050 mpu;
sensors_event_t current_a, current_g, current_temp;
float current_roll = 0.0, current_pitch = 0.0;
unsigned long lastTelemetryReadMillis = 0;
const unsigned long telemetryReadInterval = 50; // ms

// --- Web Server ---
#if defined(ESP32)
  WebServer server(80);
#elif defined(ESP8266)
  ESP8266WebServer server(80);
#endif


// --- Motion State Management ---
unsigned long lastMotionDetectedTime = 0;
const unsigned long WEB_MOTION_VISIBLE_DURATION = 900; 
const unsigned long LED_ON_DURATION_AFTER_MOTION = 500;  

bool webPageShowsMotion = false;
bool ledShouldBeOn = false;

// --- Sustained Tilt Detection Logic ---
float prev_roll_tilt = 0.0, prev_pitch_tilt = 0.0;
bool firstTiltReading = true;
unsigned long continuousTiltChangeStartTime = 0;
bool deviceIsTiltingSustained = false;

const float TILT_CHANGE_DEGREE_THRESHOLD = 0.5; 
const unsigned long SUSTAINED_TILT_DURATION = 250;

// --- Sustained Slide Detection Logic ---
float prev_ax_slide = 0.0, prev_ay_slide = 0.0;
bool firstAccelReadingForSlide = true;
unsigned long continuousSlideStartTime = 0;
bool deviceIsSlidingSustained = false;

const float SLIDE_ACCEL_CHANGE_THRESHOLD = 0.5; 
const unsigned long SUSTAINED_SLIDE_DURATION = 150; 


// --- Motion History Log ---
struct LogEntry {
  unsigned long timestamp;
  String motionType; 
  sensors_event_t accelData;
  sensors_event_t gyroData;
  sensors_event_t tempData;
  float rollAtDetection;
  float pitchAtDetection;
};
std::vector<LogEntry> motionLog;
const int MAX_MOTION_LOG_ENTRIES = 20; 
unsigned long lastTimeEntryAddedToLog = 0;
const unsigned long MIN_INTERVAL_BETWEEN_LOG_ENTRIES = 300;

// --- Function Prototypes ---
String formatMillisTimestamp(unsigned long ms);
void handleRoot(); void handleNotFound(); void handleDownloadLog();
void setupWiFi(); void setupMPU6050(); void setupWebServer();
void controlLED(); void readTelemetryDataAndAngles(); void updateMotionDisplayStates();
void checkSustainedTilt(); void checkSustainedSlide(); 
void addMotionToLog(unsigned long timestamp, const String& type, const sensors_event_t& a, const sensors_event_t& g, const sensors_event_t& t, float r, float p);

void setup(void) {
  Serial.begin(115200);
  delay(2000); 

  Serial.println();
  Serial.println("DEBUG: --- SETUP FUNCTION STARTED ---");
  Serial.println("MPU6050 Web Server - Tilt & Slide Detection with Debug (Corrected I2C Pins)");

  Serial.println("DEBUG: Initializing LED pin...");
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); 
  Serial.println("DEBUG: LED pin initialized.");

  Serial.println("DEBUG: Initializing motion state flags...");
  webPageShowsMotion = false;
  ledShouldBeOn = false;
  Serial.println("DEBUG: Motion state flags initialized.");

  setupMPU6050(); 
  Serial.println("DEBUG: setupMPU6050() CALL COMPLETED.");

  setupWiFi();
  Serial.println("DEBUG: setupWiFi() CALL COMPLETED.");
  
  setupWebServer();
  Serial.println("DEBUG: setupWebServer() CALL COMPLETED.");

  Serial.println("DEBUG: --- SETUP FUNCTION SUCCESSFULLY COMPLETED ---");
  Serial.println("Server running. Access via URL shown above (if WiFi connected).");
}

void loop() {
  server.handleClient(); 
  unsigned long currentMillisInLoop = millis();

  if (currentMillisInLoop - lastTelemetryReadMillis >= telemetryReadInterval) {
    lastTelemetryReadMillis = currentMillisInLoop;
    readTelemetryDataAndAngles(); 
    checkSustainedTilt();       
    checkSustainedSlide();      
  }

  static bool prevOverallMotionState = false;
  bool overallMotionDetected = deviceIsTiltingSustained || deviceIsSlidingSustained; 

  if (overallMotionDetected && !prevOverallMotionState) {
    String motionTypeStr = "";
    if (deviceIsTiltingSustained && deviceIsSlidingSustained) motionTypeStr = "Tilt & Slide";
    else if (deviceIsTiltingSustained) motionTypeStr = "Tilt";
    else if (deviceIsSlidingSustained) motionTypeStr = "Slide";
    else motionTypeStr = "Motion"; 

    Serial.print(formatMillisTimestamp(currentMillisInLoop)); 
    Serial.print(": Motion STARTED (Type: "); Serial.print(motionTypeStr); Serial.println(")");
    
    lastMotionDetectedTime = currentMillisInLoop; 
    
    if (currentMillisInLoop - lastTimeEntryAddedToLog >= MIN_INTERVAL_BETWEEN_LOG_ENTRIES) { 
        addMotionToLog(currentMillisInLoop, motionTypeStr, current_a, current_g, current_temp, current_roll, current_pitch);
        lastTimeEntryAddedToLog = currentMillisInLoop;
    }
  } else if (!overallMotionDetected && prevOverallMotionState) {
    Serial.print(formatMillisTimestamp(currentMillisInLoop)); Serial.println(": Motion STOPPED");
  }
  prevOverallMotionState = overallMotionDetected;

  updateMotionDisplayStates(); 
  controlLED();         
  delay(10); 
}

String formatMillisTimestamp(unsigned long ms) {
    unsigned long s = ms / 1000, m = s / 60, h = m / 60;
    ms %= 1000; s %= 60; m %= 60; h %= 24; 
    char buf[20]; 
    sprintf(buf, "%02lu:%02lu:%02lu.%03lu", h, m, s, ms);
    return String(buf);
}

void readTelemetryDataAndAngles() {
  mpu.getEvent(&current_a, &current_g, &current_temp);
  float ax = current_a.acceleration.x, ay = current_a.acceleration.y, az = current_a.acceleration.z;
  if (abs(az) < 1e-4f && abs(ay) < 1e-4f) current_roll = firstTiltReading ? 0.0f : prev_roll_tilt; 
  else current_roll  = atan2(ay, az) * 180.0 / M_PI;
  float yz_sqrt_sum = sqrt(pow(ay,2) + pow(az,2));
  if (yz_sqrt_sum < 1e-4f) current_pitch = firstTiltReading ? 0.0f : prev_pitch_tilt; 
  else current_pitch = atan2(-ax, yz_sqrt_sum) * 180.0 / M_PI;
}

void checkSustainedTilt() {
    unsigned long ct = millis(); 
    if (firstTiltReading) {
        prev_roll_tilt = current_roll; prev_pitch_tilt = current_pitch;
        firstTiltReading = false; continuousTiltChangeStartTime = 0; deviceIsTiltingSustained = false; return;
    }
    float dr = abs(current_roll - prev_roll_tilt), dp = abs(current_pitch - prev_pitch_tilt); 
    if (dr > TILT_CHANGE_DEGREE_THRESHOLD || dp > TILT_CHANGE_DEGREE_THRESHOLD) {
        if (continuousTiltChangeStartTime == 0) continuousTiltChangeStartTime = ct; 
        if (ct - continuousTiltChangeStartTime >= SUSTAINED_TILT_DURATION) deviceIsTiltingSustained = true;
    } else { deviceIsTiltingSustained = false; continuousTiltChangeStartTime = 0; }
    prev_roll_tilt = current_roll; prev_pitch_tilt = current_pitch;
}

void checkSustainedSlide() { 
    unsigned long ct = millis();
    if (firstAccelReadingForSlide) {
        prev_ax_slide = current_a.acceleration.x; prev_ay_slide = current_a.acceleration.y;
        firstAccelReadingForSlide = false; continuousSlideStartTime = 0; deviceIsSlidingSustained = false; return;
    }
    float dax = abs(current_a.acceleration.x - prev_ax_slide), day = abs(current_a.acceleration.y - prev_ay_slide); 
    if ((dax + day) > SLIDE_ACCEL_CHANGE_THRESHOLD) { 
        if (continuousSlideStartTime == 0) continuousSlideStartTime = ct;
        if (ct - continuousSlideStartTime >= SUSTAINED_SLIDE_DURATION) deviceIsSlidingSustained = true;
    } else { deviceIsSlidingSustained = false; continuousSlideStartTime = 0; }
    prev_ax_slide = current_a.acceleration.x; prev_ay_slide = current_a.acceleration.y;
}

void addMotionToLog(unsigned long ts, const String& type, const sensors_event_t& a, const sensors_event_t& g, const sensors_event_t& t, float r, float p) { 
  if (motionLog.size() >= MAX_MOTION_LOG_ENTRIES) motionLog.erase(motionLog.begin()); 
  LogEntry nE = {ts, type, a, g, t, r, p}; 
  motionLog.push_back(nE);
  Serial.print(formatMillisTimestamp(ts)); Serial.print(": Logged '"); Serial.print(type); Serial.print("'. Log#: "); Serial.println(motionLog.size());
}

void updateMotionDisplayStates() {
  unsigned long ct = millis();
  if (lastMotionDetectedTime == 0 && !deviceIsTiltingSustained && !deviceIsSlidingSustained) { 
      webPageShowsMotion = false; ledShouldBeOn = false;
  } else {
      webPageShowsMotion = (ct - lastMotionDetectedTime < WEB_MOTION_VISIBLE_DURATION);
      ledShouldBeOn      = (ct - lastMotionDetectedTime < LED_ON_DURATION_AFTER_MOTION);
  }
}

void setupMPU6050() {
  Serial.println("DEBUG: --- setupMPU6050() STARTED ---");
  Serial.print("DEBUG: Attempting Wire.begin with SDA_PIN="); Serial.print(SDA_PIN); Serial.print(" (GPIO"); Serial.print(SDA_PIN);
  Serial.print("), SCL_PIN="); Serial.print(SCL_PIN); Serial.print(" (GPIO"); Serial.print(SCL_PIN); Serial.println(")");
  Wire.begin(SDA_PIN, SCL_PIN); 
  Serial.println("DEBUG: Wire.begin() called.");
  
  Serial.println("DEBUG: Attempting mpu.begin()...");
  if (!mpu.begin()) {
    Serial.println(">>> CRITICAL ERROR in setupMPU6050: Failed to find MPU6050 chip. Check wiring to SDA/SCL pins. Halting. <<<");
    while(1){ digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(50); } 
  }
  Serial.println("DEBUG: MPU6050 mpu.begin() successful!");
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ); 
  Serial.println("DEBUG: MPU6050 ranges and filter set.");
  Serial.print("DEBUG: Tilt Threshold (deg/50ms): "); Serial.println(TILT_CHANGE_DEGREE_THRESHOLD, 2);
  Serial.print("DEBUG: Tilt Duration (ms): "); Serial.println(SUSTAINED_TILT_DURATION);
  Serial.print("DEBUG: Slide Threshold (m/s2 SumXY/50ms): "); Serial.println(SLIDE_ACCEL_CHANGE_THRESHOLD, 2);
  Serial.print("DEBUG: Slide Duration (ms): "); Serial.println(SUSTAINED_SLIDE_DURATION);
  Serial.println("DEBUG: --- setupMPU6050() COMPLETED ---");
}

void setupWiFi() {
  Serial.println("DEBUG: --- setupWiFi() STARTED ---");
  Serial.print("Connecting to WiFi SSID: '"); Serial.print(ssid); Serial.println("'");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) { 
    delay(500); Serial.print("."); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nDEBUG: WiFi connected successfully!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    Serial.print("Web Server URL: http://"); Serial.print(WiFi.localIP()); Serial.println("/");
  } else {
    Serial.println("\n>>> CRITICAL ERROR in setupWiFi: Failed to connect to WiFi. Check credentials. Halting. <<<");
    while(1){ digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(250);}
  }
  Serial.println("DEBUG: --- setupWiFi() COMPLETED ---");
}

void setupWebServer() {
  Serial.println("DEBUG: --- setupWebServer() STARTED ---");
  server.on("/", HTTP_GET, handleRoot);
  server.on("/downloadlog.csv", HTTP_GET, handleDownloadLog); 
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("DEBUG: HTTP web server started.");
  Serial.println("DEBUG: --- setupWebServer() COMPLETED ---");
}

void handleDownloadLog() {
    if (!server.authenticate(webServerUsername, webServerPassword)) {
        return server.requestAuthentication();
    }
    String csvData = "Timestamp (Raw ms),Formatted Time (HH:MM:SS.sss),Motion Type,Roll (deg),Pitch (deg),AccelX (m/s^2),AccelY,AccelZ,GyroX (rad/s),GyroY,GyroZ,Temp (C)\n";
    std::vector<LogEntry> logCopy = motionLog; 
    for (size_t i = 0; i < logCopy.size(); ++i) { 
        const LogEntry& entry = logCopy[i];
        csvData += String(entry.timestamp) + ",";
        csvData += formatMillisTimestamp(entry.timestamp) + ",";
        csvData += "\"" + entry.motionType + "\","; 
        csvData += String(entry.rollAtDetection, 2) + ",";
        csvData += String(entry.pitchAtDetection, 2) + ",";
        csvData += String(entry.accelData.acceleration.x, 4) + ",";
        csvData += String(entry.accelData.acceleration.y, 4) + ",";
        csvData += String(entry.accelData.acceleration.z, 4) + ",";
        csvData += String(entry.gyroData.gyro.x, 4) + ",";
        csvData += String(entry.gyroData.gyro.y, 4) + ",";
        csvData += String(entry.gyroData.gyro.z, 4) + ",";
        csvData += String(entry.tempData.temperature, 2) + "\n";
    }
    server.sendHeader("Content-Disposition", "attachment; filename=motion_log.csv");
    server.send(200, "text/csv", csvData);
    Serial.println("Log data provided for CSV download.");
}

void handleRoot() {
  if (!server.authenticate(webServerUsername, webServerPassword)) { return server.requestAuthentication(); }
  unsigned long millisAtPageLoad = millis();
  String html = "<html><head><title>MPU6050 Motion Detection</title>";
  html += "<meta http-equiv='refresh' content='0.3'>"; 
  html += "<style>body { font-family: Arial, Helvetica, sans-serif; margin: 0; padding: 20px; background-color: #f4f4f4; color: #333; }";
  html += "h1, h2, h3 { text-align: center; color: #0056b3; }";
  html += ".container { max-width: 750px; margin: 20px auto; padding: 25px; background-color: #fff; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.15); }";
  html += ".status { text-align: center; margin-bottom: 25px; padding: 12px; border-radius: 6px; font-size: 1.1em; font-weight: bold; }";
  html += ".status-motion { background-color: #ffdddd; color: #c00000; border: 1px solid #c00000; }";
  html += ".status-no-motion { background-color: #ddffdd; color: #006400; border: 1px solid #006400; }";
  html += "table { width: 100%; border-collapse: collapse; margin-top: 20px; font-size: 0.9em;}";
  html += "th, td { text-align: left; padding: 6px; border-bottom: 1px solid #e0e0e0; }";
  html += "th { background-color: #007bff; color: white; } tr:hover { background-color: #f0f8ff; }";
  html += ".log-container { margin-top: 30px; padding: 15px; background-color: #e9ecef; border-radius: 6px;}";
  html += ".log-container ul { list-style-type: none; padding-left: 0;}";
  html += ".log-container li { background-color: #fff; margin-bottom: 8px; padding: 10px; border-radius: 4px; border: 1px solid #ced4da; font-size: 0.85em; line-height:1.4;}";
  html += ".log-item-detail {margin-left: 15px; font-size: 0.9em; color: #555;}";
  html += ".download-button { display: block; width: 200px; margin: 20px auto; padding: 10px 15px; background-color: #28a745; color: white; text-align: center; text-decoration: none; border-radius: 5px; font-weight: bold;}";
  html += ".download-button:hover { background-color: #218838; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>ESP32 MPU6050 Motion Detection</h1>";
  html += "<div class='status ";
  if (webPageShowsMotion) { html += "status-motion'>DEVICE STATUS: MOTION DETECTED (LED On)"; }
  else { html += "status-no-motion'>DEVICE STATUS: NO MOTION (LED Off)"; }
  html += "</div>";
  html += "<h2>Live Telemetry Data</h2>";
  html += "<table><tr><th>Sensor/Value</th><th>X-axis / Roll (&deg;)</th><th>Y-axis / Pitch (&deg;)</th><th>Z-axis</th><th>Temperature (&deg;C)</th></tr>";
  html += "<tr><td>Accelerometer (m/s&sup2;)</td><td>" + String(current_a.acceleration.x, 2) + "</td><td>" + String(current_a.acceleration.y, 2) + "</td><td>" + String(current_a.acceleration.z, 2) + "</td><td rowspan='3' style='vertical-align:middle; text-align:center;'>" + String(current_temp.temperature, 1) + "</td></tr>";
  html += "<tr><td>Gyroscope (rad/s)</td><td>" + String(current_g.gyro.x, 2) + "</td><td>" + String(current_g.gyro.y, 2) + "</td><td>" + String(current_g.gyro.z, 2) + "</td></tr>";
  html += "<tr><td>Orientation (&deg;)</td><td>" + String(current_roll, 1) + "</td><td>" + String(current_pitch, 1) + "</td><td>N/A</td></tr>";
  html += "</table>";
  html += "<p><a href='/downloadlog.csv' class='download-button' download='motion_log.csv'>Download Log as CSV</a></p>"; 
  html += "<div class='log-container'>";
  html += "<h3>Motion Event History (Last " + String(MAX_MOTION_LOG_ENTRIES) + " Events - Newest First)</h3>";
  
  std::vector<LogEntry> logCopyForDisplay = motionLog;

  if (logCopyForDisplay.empty()) { html += "<p>No motion events logged yet.</p>"; }
  else {
    html += "<ul>";
    for (int i = logCopyForDisplay.size() - 1; i >= 0; i--) { 
      const LogEntry& entry = logCopyForDisplay[i];
      unsigned long ageMillis = millisAtPageLoad - entry.timestamp;
      String ageString;
      if (ageMillis < 1000) { ageString = "Just now"; }
      else if (ageMillis < 60000) { ageString = String(ageMillis / 1000) + "s ago"; }
      else if (ageMillis < 3600000) { ageString = String(ageMillis / 60000) + "m " + String((ageMillis % 60000) / 1000) + "s ago"; }
      else { ageString = String(ageMillis / 3600000) + "h " + String((ageMillis % 3600000) / 60000) + "m ago"; }
      
      html += "<li><strong>Event (" + entry.motionType + "): " + ageString + " (" + formatMillisTimestamp(entry.timestamp) + ")</strong>";
      html += "<div class='log-item-detail'>";
      html += "Roll: " + String(entry.rollAtDetection, 1) + "&deg;, Pitch: " + String(entry.pitchAtDetection, 1) + "&deg;<br>";
      html += "Accel X: " + String(entry.accelData.acceleration.x, 2) + ", Y: " + String(entry.accelData.acceleration.y, 2) + ", Z: " + String(entry.accelData.acceleration.z, 2) + " m/s&sup2;<br>";
      html += "Gyro X: " + String(entry.gyroData.gyro.x, 2) + ", Y: " + String(entry.gyroData.gyro.y, 2) + ", Z: " + String(entry.gyroData.gyro.z, 2) + " rad/s<br>";
      html += "Temp: " + String(entry.tempData.temperature, 1) + " &deg;C";
      html += "</div></li>";
    }
    html += "</ul>";
  }
  html += "</div></div></body></html>";
  server.send(200, "text/html", html);
}

void handleNotFound() {
  if (!server.authenticate(webServerUsername, webServerPassword)) { return server.requestAuthentication(); }
  String message = "File Not Found\n\nURI: " + server.uri() + "\nMethod: " + (server.method() == HTTP_GET ? "GET" : "POST") + "\nArguments: " + String(server.args()) + "\n";
  for (uint8_t i = 0; i < server.args(); i++) { message += " " + server.argName(i) + ": " + server.arg(i) + "\n"; }
  server.send(404, "text/plain", message);
}

void controlLED() {
  if (ledShouldBeOn) { 
      #if defined(ESP8266) && (LED_PIN == 0 || LED_PIN == 2) 
        digitalWrite(LED_PIN, LOW); 
      #elif defined(ESP32) && LED_PIN == 13 
        digitalWrite(LED_PIN, HIGH);
      #else 
        digitalWrite(LED_PIN, HIGH); 
      #endif
  } else { 
      #if defined(ESP8266) && (LED_PIN == 0 || LED_PIN == 2)
        digitalWrite(LED_PIN, HIGH);
      #elif defined(ESP32) && LED_PIN == 13
        digitalWrite(LED_PIN, LOW);
      #else
        digitalWrite(LED_PIN, LOW); 
      #endif
  }
}