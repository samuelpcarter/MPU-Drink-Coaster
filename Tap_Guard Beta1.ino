/**
 * @file AuraGuard_Coaster_v21.ino
 * @brief Firmware for "AuraGuard Coaster" - A Wi-Fi enabled Smart Anti-Tampering Coaster.
 * @version 21.1 (Fix for F() macro syntax error)
 * @date 2025-06-03
 *
 * @description
 * Project AuraGuard Coaster:
 * This project transforms a simple coaster into a smart device designed to detect
 * tampering or movement of an object (e.g., a drink glass) placed upon it.
 * It's envisioned as a portable security accessory, fitting easily into a purse or pocket.
 *
 * Core Functionality:
 * 1. Motion Detection:
 * - Hardware-based: Utilizes the MPU6050's built-in motion detection for quick jolts/bumps.
 * - Software-based: Implements an algorithm to detect slow, sustained movements (slides, tilts)
 * by analyzing raw accelerometer data over time.
 * 2. Arm/Disarm System: The coaster's monitoring can be armed or disarmed via a web interface.
 * 3. Event Logging with Timestamps:
 * - When armed, if any motion (hardware or software) is detected, the event is logged.
 * - Timestamps are based on real-time fetched via NTP (Network Time Protocol).
 * 4. Web Interface:
 * - Served directly from the ESP32 over Wi-Fi (STA mode - connects to an existing network).
 * - Allows users to arm/disarm, view the live event log, and clear the log.
 * - Styled with an elegant, feminine aesthetic.
 * 5. LED Indication: An onboard LED provides immediate, brief visual feedback upon any raw motion detection,
 * regardless of the armed state.
 *
 * Hardware Assumptions:
 * - ESP32 Module: An ESP32-WROOM-32 series module (or similar).
 * - MPU6050 Accelerometer/Gyroscope: Connected via I2C.
 * - SDA (Data): Connected to ESP32 GPIO 21.
 * - SCL (Clock): Connected to ESP32 GPIO 22.
 * (These are common default I2C pins for ESP32. If different, update Wire.begin() call).
 * - Power System:
 * - Lithium Polymer (LiPo) Battery: e.g., 3.7V, ~500mAh.
 * - Charging Circuit: TP4056 or similar for USB charging of the LiPo battery.
 * - Step-Up (Boost) Converter: To raise the LiPo's 3.7V to a stable 3.3V or 5V required by the ESP32.
 * - Status LED: A single LED connected to a GPIO pin (defined by STATUS_LED_PIN).
 *
 * Software & Connectivity:
 * - Wi-Fi: ESP32 connects as a station (STA) to an existing Wi-Fi network.
 * - NTP Client: Fetches current time from an NTP server for accurate event timestamps.
 * - Web Server: ESP32 hosts a web page for user interaction.
 * - JSON: Used for data exchange between ESP32 (server) and web browser (client).
 *
 * Future Development Considerations (as per user's vision):
 * - Bluetooth Low Energy (BLE): For direct smartphone connectivity, potentially for configuration,
 * arming/disarming, and log retrieval without needing a shared Wi-Fi network. This would be
 * more aligned with a portable, personal device.
 * - Wi-Fi Access Point (AP) Mode: ESP32 could create its own Wi-Fi network if no external
 * network is available, allowing a device to connect directly to the coaster.
 * - Non-Volatile Storage: Using SPIFFS, LittleFS on ESP32 flash, or an external EEPROM/SD card
 * to store event logs persistently, so they survive power cycles or disconnections.
 * - Power Management: Implementing deep sleep modes and optimizing sensor polling to maximize
 * battery life.
 * - Calibration Routine: For software-based motion detection to adapt to different surfaces
 * or initial orientations.
 *
 * This version (v21.1) fixes a syntax error in an F() macro call.
 */

// --- Core ESP32 and Sensor Libraries ---
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Arduino_JSON.h>
#include <time.h> // For NTP and time functions

// --- User Configuration: WiFi Credentials ---
#define WIFI_SSID "DevilDogVIP"
#define WIFI_PASSWORD "yvetteway"

// --- System Behavior and Pin Configuration ---
#define STATUS_LED_PIN 2
#define RAW_MOTION_LED_DURATION_MS 750 // LED on duration for raw motion (milliseconds)

// MPU6050 Hardware Motion Detection Sensitivity (for jolts/bumps):
// THRESHOLD (1-255): Lower is more sensitive.
// DURATION_MS (1-255, actual_ms = val * 1.024): How long motion must persist.
// v21: Made more sensitive than v20 for better detection of subtle quick movements.
#define MPU_HW_MOTION_DETECT_THRESHOLD 2 
#define MPU_HW_MOTION_DETECT_DURATION_MS 3 

// Software-Based Slow Movement Detection Parameters:
#define ACCEL_READ_INTERVAL_MS 100     // How often to read accelerometer for slow move detection.
#define SLOW_MOVE_SAMPLE_COUNT 5       // Number of samples to average for baseline.
#define SLOW_MOVE_THRESHOLD_G 0.035    // Threshold of change in G's to detect slow move (e.g., 0.03G).
#define SLOW_MOVE_MIN_DURATION_MS 750  // How long the change must persist to be a slow move event.

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;      // GMT offset in seconds (e.g., for PST: -8 * 3600) - Set to your timezone
const int daylightOffset_sec = 0; // Daylight saving offset in seconds (e.g., 3600 for US DST)

// Other Configurations
#define SERIAL_MONITOR_BAUD_RATE 115200
#define WIFI_CONNECT_TIMEOUT_SECONDS 20
#define WIFI_CONNECT_RETRY_DELAY_MS 500
#define HTTP_SERVER_PORT 80
#define MAX_MOTION_LOG_ENTRIES 30

// --- Global System State Variables ---
Adafruit_MPU6050 mpu;
WebServer webServer(HTTP_SERVER_PORT);

bool g_isSystemArmed = false;
bool g_newRawMotionDetectedByMPU = false;   
bool g_newSlowMoveDetectedBySW = false;   
bool g_motionEventForWebUI = false; 

String g_motionLog[MAX_MOTION_LOG_ENTRIES]; 
int g_motionLogCount = 0;
uint32_t g_armingTimeMillis = 0; 
bool g_ntpTimeSyncd = false;

uint32_t g_rawMotionLedActiveUntilMs = 0;

// Variables for Software-based Slow Motion Detection
float g_accelBaselineMagnitude = 0.0;
float g_accelMagnitudeSamples[SLOW_MOVE_SAMPLE_COUNT];
int g_accelSampleIndex = 0;
bool g_baselineEstablished = false;
uint32_t g_lastAccelReadTime = 0;
uint32_t g_slowMoveStartTime = 0;
bool g_potentialSlowMove = false;


// --- PROGMEM HTML, CSS, JavaScript Frontend ---
const char HTML_DOCUMENT_PROGMEM[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no, user-scalable=no">
    <title>AuraGuard Coaster</title>
    <link href="https://fonts.googleapis.com/css2?family=Playfair+Display:wght@700&family=Lato:wght@400;700&display=swap" rel="stylesheet">
    <style>
        html, body {
            height: 100%; width: 100%; margin: 0; padding: 0;
            font-family: 'Lato', sans-serif; /* Default body font */
            background-color: #1C1C1E; /* Slightly off-black */
            color: #F2F2F7; /* Apple's light text color */
            display: flex;
            flex-direction: column; /* Allow header and content area */
            align-items: center;
            justify-content: flex-start; /* Align content to top */
            text-align: center;
            overflow-x: hidden;
            -webkit-font-smoothing: antialiased;
            -moz-osx-font-smoothing: grayscale;
            padding-top: 30px; /* Space for header */
        }
        .header-title {
            font-family: 'Playfair Display', serif; /* Elegant title font */
            font-size: clamp(2.5rem, 8vw, 3.5rem); /* Responsive title size */
            color: #E91E63; /* Vibrant Pink/Rose accent */
            margin-bottom: 30px;
            letter-spacing: 1px;
        }
        .app-container {
            width: 100%;
            max-width: 480px; /* Increased max-width */
            padding: 20px;
            box-sizing: border-box;
            display: flex;
            flex-direction: column;
            align-items: center;
            transition: box-shadow 0.3s ease-out; /* For motion flash */
        }
        .app-container.motion-flash {
            box-shadow: 0 0 25px rgba(233, 30, 99, 0.7); /* Pink glow */
            border-radius: 16px;
        }
        #status-message {
            font-family: 'Lato', sans-serif;
            font-size: clamp(1.5rem, 5vw, 2rem); /* Larger status */
            font-weight: 600;
            margin-bottom: 35px;
            min-height: 2.8em; 
            transition: color 0.4s ease, transform 0.3s ease;
            line-height: 1.4;
        }
        .status-disarmed { color: #FF9F0A; } /* Apple Orange */
        .status-armed-monitoring { color: #30D158; } /* Apple Green */
        .status-armed-logged { color: #FF453A; } /* Apple Red */
        .status-motion-detected { 
            color: #E91E63; /* Vibrant Pink */
            animation: pulseStatusText 1.2s infinite ease-in-out;
        }
        @keyframes pulseStatusText {
            0%, 100% { opacity: 1; transform: scale(1.03); }
            50% { opacity: 0.7; transform: scale(1); }
        }

        .button-container {
            display: flex;
            gap: 15px; /* Space between buttons */
            margin-bottom: 40px;
            flex-wrap: wrap; /* Allow buttons to wrap on smaller screens */
            justify-content: center;
        }
        .action-button { /* Common style for all buttons */
            font-family: 'Lato', sans-serif;
            font-size: clamp(1.1rem, 4vw, 1.4rem); /* Larger button text */
            font-weight: 700;
            color: white;
            border: none;
            padding: 18px 30px; /* Increased padding */
            border-radius: 16px; /* More rounded */
            min-width: 180px; /* Min width for buttons */
            cursor: pointer;
            transition: all 0.25s ease;
            box-shadow: 0 6px 12px rgba(0,0,0,0.2), 0 0 0 0 rgba(233, 30, 99, 0.5); /* Base shadow + potential glow */
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .action-button:hover {
            transform: translateY(-3px);
            box-shadow: 0 10px 20px rgba(0,0,0,0.25), 0 0 15px 0 rgba(233, 30, 99, 0.4);
        }
        .action-button:active {
            transform: translateY(1px);
            box-shadow: 0 3px 7px rgba(0,0,0,0.22);
        }
        #arm-disarm-button.button-arm {
            background: linear-gradient(135deg, #E91E63, #FF6090); /* Pink/Rose gradient */
        }
        #arm-disarm-button.button-disarm {
            background: linear-gradient(135deg, #757575, #9E9E9E); /* Grey gradient for disarm */
        }
        #clear-log-button {
            background: linear-gradient(135deg, #4A5568, #718096); /* Cool grey/blue */
            display: none; /* Initially hidden */
        }
        .action-button:disabled {
            background-image: none;
            background-color: #525252;
            cursor: not-allowed;
            opacity: 0.5;
            transform: none;
            box-shadow: 0 4px 8px rgba(0,0,0,0.15);
        }

        #event-log-container { /* Renamed from motion-log-container */
            width: 100%;
            background-color: #2C2C2E; /* Darker grey for log area */
            border-radius: 12px; /* Match button radius */
            padding: 20px 25px;
            box-sizing: border-box;
            max-height: 300px; 
            overflow-y: auto;
            border: 1px solid #4A4A4A;
            text-align: left;
            box-shadow: inset 0 2px 8px rgba(0,0,0,0.3);
        }
        #event-log-container h3 {
            font-family: 'Playfair Display', serif;
            margin-top: 0;
            margin-bottom: 15px;
            color: #E91E63; /* Pink accent for log title */
            font-size: clamp(1.2rem, 4.5vw, 1.6rem);
            font-weight: 700;
            border-bottom: 1px solid #4A4A4A;
            padding-bottom: 10px;
        }
        #event-log-list {
            list-style-type: none;
            padding-left: 0;
            margin: 0;
            font-size: clamp(0.95rem, 3.5vw, 1.1rem); /* Larger log text */
            color: #EAEAFB; /* Lighter text for log entries */
        }
        #event-log-list li {
            padding: 10px 5px;
            border-bottom: 1px solid #3A3A3C; /* Subtle separator */
            font-weight: 700; /* Make timestamp bold */
        }
        #event-log-list li:last-child { border-bottom: none; }
        #event-log-list .log-placeholder {
            color: #8E8E93; /* Dimmer placeholder */
            font-style: italic;
            font-weight: 400;
        }

        #event-log-container::-webkit-scrollbar { width: 10px; }
        #event-log-container::-webkit-scrollbar-track { background: #2C2C2E; border-radius: 10px; }
        #event-log-container::-webkit-scrollbar-thumb { background-color: #E91E63; border-radius: 10px; border: 2px solid #2C2C2E; }
        #event-log-container::-webkit-scrollbar-thumb:hover { background-color: #FF6090; }
    </style>
</head>
<body>
    <div class="header-title">AuraGuard Coaster</div>
    <div class="app-container" id="app-main-container">
        <div id="status-message" class="status-disarmed">SYSTEM DISARMED</div>
        <div class="button-container">
            <button id="arm-disarm-button" class="action-button button-arm" type="button">ARM SYSTEM</button>
            <button id="clear-log-button" class="action-button" type="button">CLEAR LOG</button>
        </div>
        <div id="event-log-container" style="display: none;">
            <h3>Event Log</h3>
            <ul id="event-log-list"></ul>
        </div>
    </div>

    <script>
        'use strict';
        const SCRIPT_VERSION_V21 = "v21.1.0_compile_fix"; // Updated version
        console.log(`AuraGuard Coaster UI Script ${SCRIPT_VERSION_V21} Initializing...`);

        const DOM = {
            appContainer: document.getElementById('app-main-container'),
            statusMessage: document.getElementById('status-message'),
            armDisarmButton: document.getElementById('arm-disarm-button'),
            clearLogButton: document.getElementById('clear-log-button'),
            eventLogContainer: document.getElementById('event-log-container'),
            eventLogList: document.getElementById('event-log-list')
        };

        let AppState = {
            isArmed: false,
            eventLog: [], 
            totalEventsThisSession: 0,
            serverFetchIntervalId: null,
            motionDetectedDisplayEndTime: 0,
            motionFlashTimeoutId: null,
            ntpStatusKnown: false,
            isNtpSyncd: false
        };

        const UI_CONSTANTS = {
            SERVER_FETCH_INTERVAL_MS: 750,
            MOTION_DETECTED_MSG_DURATION_MS: 4500,
            MOTION_FLASH_DURATION_MS: 600
        };

        function initializeUI() {
            console.log("V21 JS: Initializing UI...");
            DOM.armDisarmButton.addEventListener('click', handleArmDisarmButtonClick);
            DOM.clearLogButton.addEventListener('click', handleClearLogClick);
            updateButtonDisplay(AppState.isArmed, false); 
            updateStatusMessage(AppState.isArmed, false, 0, AppState.isNtpSyncd);
            
            AppState.serverFetchIntervalId = setInterval(fetchStateFromServer, UI_CONSTANTS.SERVER_FETCH_INTERVAL_MS);
            fetchStateFromServer();
            console.log(`AuraGuard UI ${SCRIPT_VERSION_V21} init complete.`);
        }

        async function handleArmDisarmButtonClick() {
            const newArmState = !AppState.isArmed;
            DOM.armDisarmButton.disabled = true;
            DOM.clearLogButton.disabled = true; 

            try {
                await fetch(`/arm?state=${newArmState ? 1 : 0}`);
            } catch (error) {
                console.error("V21 JS: Network error during arm/disarm:", error);
            } finally {
                fetchStateFromServer();
            }
        }
        
        async function handleClearLogClick() {
            if (!AppState.isArmed || AppState.eventLog.length === 0) return; 
            console.log("V21 JS: Clear Log button clicked.");
            DOM.clearLogButton.disabled = true;
            DOM.armDisarmButton.disabled = true;

            try {
                const response = await fetch('/resetlog');
                if (response.ok) {
                    console.log("V21 JS: Log cleared successfully on server.");
                } else {
                    console.error("V21 JS: Error clearing log on server.");
                }
            } catch (error) {
                console.error("V21 JS: Network error clearing log:", error);
            } finally {
                fetchStateFromServer();
            }
        }


        function updateButtonDisplay(isArmed, hasLoggedEvents) {
            DOM.armDisarmButton.disabled = false; 
            DOM.clearLogButton.disabled = false;

            if (isArmed) {
                DOM.armDisarmButton.textContent = 'DISARM SYSTEM';
                DOM.armDisarmButton.classList.remove('button-arm');
                DOM.armDisarmButton.classList.add('button-disarm');
                if (hasLoggedEvents) {
                    DOM.clearLogButton.style.display = 'inline-block';
                } else {
                    DOM.clearLogButton.style.display = 'none';
                }
            } else {
                DOM.armDisarmButton.textContent = 'ARM SYSTEM';
                DOM.armDisarmButton.classList.remove('button-disarm');
                DOM.armDisarmButton.classList.add('button-arm');
                DOM.clearLogButton.style.display = 'none';
            }
        }

        function updateStatusMessage(isArmed, newMotionEvent, currentLogCount, ntpSyncd) {
            DOM.statusMessage.classList.remove('status-disarmed', 'status-armed-monitoring', 'status-armed-logged', 'status-motion-detected');
            let ntpWarning = (!ntpSyncd && AppState.ntpStatusKnown && isArmed) ? " (Time may be inaccurate)" : "";

            if (isArmed) {
                if (newMotionEvent) {
                    DOM.statusMessage.textContent = `MOTION DETECTED! (Event #${currentLogCount})${ntpWarning}`;
                    DOM.statusMessage.classList.add('status-motion-detected');
                    AppState.motionDetectedDisplayEndTime = Date.now() + UI_CONSTANTS.MOTION_DETECTED_MSG_DURATION_MS;
                    if(DOM.appContainer) {
                        DOM.appContainer.classList.add('motion-flash');
                        if(AppState.motionFlashTimeoutId) clearTimeout(AppState.motionFlashTimeoutId);
                        AppState.motionFlashTimeoutId = setTimeout(() => {
                            DOM.appContainer.classList.remove('motion-flash');
                        }, UI_CONSTANTS.MOTION_FLASH_DURATION_MS);
                    }
                } else if (Date.now() < AppState.motionDetectedDisplayEndTime) {
                    DOM.statusMessage.textContent = `MOTION DETECTED! (Event #${AppState.totalEventsThisSession})${ntpWarning}`;
                    DOM.statusMessage.classList.add('status-motion-detected');
                } else if (currentLogCount > 0) {
                    DOM.statusMessage.textContent = `ARMED - EVENTS LOGGED (${currentLogCount})${ntpWarning}`;
                    DOM.statusMessage.classList.add('status-armed-logged');
                } else {
                    DOM.statusMessage.textContent = `ARMED - MONITORING${ntpWarning}`;
                    DOM.statusMessage.classList.add('status-armed-monitoring');
                }
            } else {
                DOM.statusMessage.textContent = 'SYSTEM DISARMED';
                DOM.statusMessage.classList.add('status-disarmed');
                AppState.motionDetectedDisplayEndTime = 0;
                AppState.totalEventsThisSession = 0;
            }
        }

        function updateEventLog(logEntries) {
            DOM.eventLogList.innerHTML = ''; 
            if (AppState.isArmed) {
                DOM.eventLogContainer.style.display = 'block';
                if (logEntries && logEntries.length > 0) {
                    AppState.totalEventsThisSession = logEntries.length;
                    logEntries.forEach(timestampStr => { 
                        const listItem = document.createElement('li');
                        listItem.textContent = timestampStr;
                        DOM.eventLogList.appendChild(listItem);
                    });
                    DOM.eventLogList.scrollTop = DOM.eventLogList.scrollHeight;
                } else {
                    AppState.totalEventsThisSession = 0;
                    const placeholder = document.createElement('li');
                    placeholder.classList.add('log-placeholder');
                    placeholder.textContent = AppState.isNtpSyncd ? 'No events logged yet.' : 'No events logged. Waiting for time sync...';
                    DOM.eventLogList.appendChild(placeholder);
                }
            } else {
                DOM.eventLogContainer.style.display = 'none';
                AppState.totalEventsThisSession = 0;
            }
        }

        async function fetchStateFromServer() {
            try {
                const response = await fetch('/state');
                if (!response.ok) { return; }
                const serverState = await response.json();

                const previousArmedState = AppState.isArmed;
                AppState.isArmed = serverState.armed;
                AppState.eventLog = serverState.log || [];
                AppState.isNtpSyncd = serverState.ntp_syncd || false;
                AppState.ntpStatusKnown = true; 

                if (!AppState.isArmed) AppState.motionDetectedDisplayEndTime = 0;
                if (!previousArmedState && AppState.isArmed) AppState.totalEventsThisSession = 0;

                updateButtonDisplay(AppState.isArmed, AppState.eventLog.length > 0);
                updateStatusMessage(AppState.isArmed, serverState.motionEventForWebUI, AppState.eventLog.length, AppState.isNtpSyncd);
                updateEventLog(AppState.eventLog);

            } catch (error) {
                console.error("V21 JS: Network error fetching state:", error);
            }
        }

        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', initializeUI);
        } else {
            initializeUI();
        }
    </script>
</body>
</html>
)HTML";

// --- Function Prototypes ---
// Initialization
void initializeSerialPort();
void initializeStatusLed();
void initializeNTP();
bool attemptWiFiConnectionWithRetries();
void initializeMPU6050_Sensor();
void setupWebServerEndpoints();
void logInitialSystemConfig(bool wifiStatus);

// Core Logic
void processMotionDetection();
void handleRawMotionLED();
float calculateAccelMagnitude();
void updateAccelSamples(float currentMagnitude);
bool checkForSlowMove(float currentMagnitude);

// Web Server Handlers
void httpHandleRoot();
void httpHandleArmCommand();
void httpHandleStateRequest();
void httpHandleResetLogCommand(); 
void httpHandleNotFound();

// Time Helper
String getFormattedTime();


/**
 * @brief Standard Arduino setup() function. Runs once at startup.
 */
void setup() {
    initializeSerialPort();
    initializeStatusLed();
    bool wifiConnected = attemptWiFiConnectionWithRetries();
    if (wifiConnected) {
        initializeNTP(); 
    }
    initializeMPU6050_Sensor();
    setupWebServerEndpoints();
    logInitialSystemConfig(wifiConnected);
    Serial.println(F(">>> AuraGuard Coaster v21.1: Setup complete. System running. <<<"));
    Serial.println(F("============================================================="));
}

/**
 * @brief Standard Arduino loop() function. Runs repeatedly.
 */
void loop() {
    webServer.handleClient();
    processMotionDetection(); 
    handleRawMotionLED();     
}

// --- Initialization Routines Implementation ---
void initializeSerialPort() {
    Serial.begin(SERIAL_MONITOR_BAUD_RATE);
    unsigned long serialStartTime = millis();
    while (!Serial && (millis() - serialStartTime < 1500)) { delay(10); }
    Serial.println(F("\n\n--- AuraGuard Coaster v21.1: Initializing System ---"));
    Serial.println(F("SERIAL: Communication established."));
}

void initializeStatusLed() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    Serial.println(F("STATUS LED: Initialized (OFF)."));
}

void initializeNTP() {
    Serial.print(F("NTP: Configuring time from "));
    Serial.println(ntpServer);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) { 
        Serial.println(F("NTP ERROR: Failed to obtain time. Timestamps may be relative or unavailable."));
        g_ntpTimeSyncd = false;
    } else {
        Serial.print(F("NTP: Time synchronized. Current time: "));
        Serial.println(getFormattedTime());
        g_ntpTimeSyncd = true;
    }
}


bool attemptWiFiConnectionWithRetries() {
    Serial.print(F("WIFI: Attempting to connect to SSID: '"));
    Serial.print(WIFI_SSID); Serial.println(F("'"));
    WiFi.disconnect(true); delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print(F("WIFI: Connecting"));
    unsigned long wifiConnectStartTime = millis();
    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if ((millis() - wifiConnectStartTime) > (WIFI_CONNECT_TIMEOUT_SECONDS * 1000UL)) {
            Serial.println(F("\nERROR: Wi-Fi connection timeout."));
            return false;
        }
        delay(WIFI_CONNECT_RETRY_DELAY_MS); Serial.print(F(".")); retryCount++;
        if (retryCount % 20 == 0) { Serial.println(); Serial.print(F("WIFI: (Still attempting)")); }
    }
    Serial.println(F("\nSUCCESS: Wi-Fi connected!"));
    Serial.print(F("  IP Address: http://")); Serial.println(WiFi.localIP());
    return true;
}

void initializeMPU6050_Sensor() {
    Serial.println(F("MPU6050: Initializing sensor..."));
    if (!mpu.begin()) {
        Serial.println(F("ERROR: MPU6050 init FAILED! Check wiring. System Halted."));
        while (true) { digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN)); delay(100); }
    }
    Serial.println(F("MPU6050: Sensor online. Configuring:"));
    mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
    mpu.setMotionDetectionThreshold(MPU_HW_MOTION_DETECT_THRESHOLD);
    mpu.setMotionDetectionDuration(MPU_HW_MOTION_DETECT_DURATION_MS);
    mpu.setInterruptPinLatch(true);
    mpu.setInterruptPinPolarity(true);
    mpu.setMotionInterrupt(true);
    Serial.print(F("  - HW Motion Threshold: ")); Serial.println(MPU_HW_MOTION_DETECT_THRESHOLD);
    Serial.print(F("  - HW Motion Duration (config): ")); Serial.println(MPU_HW_MOTION_DETECT_DURATION_MS);

    for(int i=0; i<SLOW_MOVE_SAMPLE_COUNT; ++i) g_accelMagnitudeSamples[i] = 0.0f;
    g_accelSampleIndex = 0;
    g_baselineEstablished = false;
    g_lastAccelReadTime = millis();
    Serial.println(F("  - Software slow-move detection parameters initialized."));
    Serial.println(F("MPU6050: Configuration complete."));
}

void setupWebServerEndpoints() {
    Serial.println(F("WEBSERVER: Configuring HTTP endpoints..."));
    webServer.on("/", HTTP_GET, httpHandleRoot);                    
    webServer.on("/arm", HTTP_GET, httpHandleArmCommand);           
    webServer.on("/state", HTTP_GET, httpHandleStateRequest);       
    webServer.on("/resetlog", HTTP_GET, httpHandleResetLogCommand); 
    webServer.onNotFound(httpHandleNotFound);                       
    webServer.begin();
    Serial.println(F("WEBSERVER: Started. Endpoints: /, /arm, /state, /resetlog")); // Corrected log
}

void logInitialSystemConfig(bool wifiStatus) {
    Serial.println(F("\n--- AuraGuard Coaster v21.1: System Configuration Summary ---"));
    if (wifiStatus) { Serial.print(F("  Wi-Fi: Connected. Web UI at http://")); Serial.println(WiFi.localIP()); }
    else { Serial.println(F("  Wi-Fi: NOT CONNECTED. Web UI unavailable. NTP time sync failed.")); }
    if(g_ntpTimeSyncd) { Serial.println(F("  NTP Time: Synchronized.")); }
    else { Serial.println(F("  NTP Time: Sync FAILED or not attempted. Timestamps will be relative if armed.")); }
    Serial.print(F("  MPU HW Threshold: ")); Serial.println(MPU_HW_MOTION_DETECT_THRESHOLD);
    Serial.print(F("  MPU HW Duration (config): ")); Serial.println(MPU_HW_MOTION_DETECT_DURATION_MS);
    Serial.print(F("  Software Slow Move Threshold (G): ")); Serial.println(SLOW_MOVE_THRESHOLD_G);
    Serial.println(F("----------------------------------------------------------"));
}


// --- Core Logic Implementation ---
float calculateAccelMagnitude() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp); 
    float ax = a.acceleration.x / 9.81;
    float ay = a.acceleration.y / 9.81;
    float az = a.acceleration.z / 9.81;
    return sqrt(ax * ax + ay * ay + az * az);
}

void updateAccelSamples(float currentMagnitude) {
    g_accelMagnitudeSamples[g_accelSampleIndex] = currentMagnitude;
    g_accelSampleIndex = (g_accelSampleIndex + 1) % SLOW_MOVE_SAMPLE_COUNT;
    if (!g_baselineEstablished && g_accelSampleIndex == 0) { 
        g_baselineEstablished = true; 
    }
}

bool checkForSlowMove(float currentMagnitude) {
    if (!g_baselineEstablished) {
        float sum = 0;
        for(int i=0; i<SLOW_MOVE_SAMPLE_COUNT; ++i) sum += g_accelMagnitudeSamples[i];
        g_accelBaselineMagnitude = sum / SLOW_MOVE_SAMPLE_COUNT;
        return false; 
    }

    float currentSmoothedMagnitude = 0;
    for(int i=0; i<SLOW_MOVE_SAMPLE_COUNT; ++i) currentSmoothedMagnitude += g_accelMagnitudeSamples[i];
    currentSmoothedMagnitude /= SLOW_MOVE_SAMPLE_COUNT;

    float diff = abs(currentSmoothedMagnitude - g_accelBaselineMagnitude);

    if (diff > SLOW_MOVE_THRESHOLD_G) {
        if (!g_potentialSlowMove) {
            g_potentialSlowMove = true;
            g_slowMoveStartTime = millis();
        } else {
            if (millis() - g_slowMoveStartTime >= SLOW_MOVE_MIN_DURATION_MS) {
                g_accelBaselineMagnitude = currentSmoothedMagnitude; 
                g_potentialSlowMove = false;
                return true;
            }
        }
    } else {
        g_potentialSlowMove = false; 
        if (millis() % (ACCEL_READ_INTERVAL_MS * 10) == 0) { 
             g_accelBaselineMagnitude = (g_accelBaselineMagnitude * 0.95) + (currentSmoothedMagnitude * 0.05);
        }
    }
    return false;
}


void processMotionDetection() {
    bool motionDetectedThisCycle = false;
    String detectionSource = "";

    if (mpu.getMotionInterruptStatus()) {
        motionDetectedThisCycle = true;
        g_newRawMotionDetectedByMPU = true; 
        detectionSource = "HW"; 
    }

    if (millis() - g_lastAccelReadTime >= ACCEL_READ_INTERVAL_MS) {
        g_lastAccelReadTime = millis();
        float currentMag = calculateAccelMagnitude();
        updateAccelSamples(currentMag);
        if (g_baselineEstablished && checkForSlowMove(currentMag)) {
            if (!motionDetectedThisCycle) { 
                motionDetectedThisCycle = true;
                detectionSource = "SW"; 
            }
            g_newSlowMoveDetectedBySW = true; 
        }
    }

    if (motionDetectedThisCycle) {
        if (g_isSystemArmed) {
            g_motionEventForWebUI = true; 
            if (g_motionLogCount < MAX_MOTION_LOG_ENTRIES) {
                g_motionLog[g_motionLogCount] = getFormattedTime(); 
                g_motionLogCount++;
                Serial.print(F("EVENT LOGGED (")); Serial.print(detectionSource); Serial.print(F("): ")); // Corrected F() macro
                Serial.println(g_motionLog[g_motionLogCount - 1]);
            } else {
                Serial.println(F("WARNING: Event log full."));
            }
        } else {
             Serial.print(F("INFO: Motion (")); Serial.print(detectionSource); Serial.println(F(") detected while DISARMED. Not logged. LED will flash.")); // Corrected F() macro
        }
    }
}

void handleRawMotionLED() {
    if (g_newRawMotionDetectedByMPU || g_newSlowMoveDetectedBySW) {
        digitalWrite(STATUS_LED_PIN, HIGH); 
        g_rawMotionLedActiveUntilMs = millis() + RAW_MOTION_LED_DURATION_MS;
        g_newRawMotionDetectedByMPU = false; 
        g_newSlowMoveDetectedBySW = false;
    }

    if (g_rawMotionLedActiveUntilMs > 0 && millis() >= g_rawMotionLedActiveUntilMs) {
        digitalWrite(STATUS_LED_PIN, LOW);
        g_rawMotionLedActiveUntilMs = 0; 
    }
}

// --- Web Server Request Handlers ---
void httpHandleRoot() {
    webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    webServer.send_P(200, "text/html", HTML_DOCUMENT_PROGMEM);
}

void httpHandleArmCommand() {
    Serial.print(F("HTTP REQ: /arm - "));
    if (webServer.hasArg("state")) {
        bool reqArmState = (webServer.arg("state") == "1");
        g_isSystemArmed = reqArmState;
        g_motionEventForWebUI = false; 

        if (g_isSystemArmed) {
            Serial.println(F("System ARMED. Event log cleared. Baseline reset."));
            g_motionLogCount = 0;
            g_armingTimeMillis = millis();
            g_baselineEstablished = false; 
            for(int i=0; i<SLOW_MOVE_SAMPLE_COUNT; ++i) g_accelMagnitudeSamples[i] = 0.0f; 
            g_accelSampleIndex = 0;
            g_potentialSlowMove = false;
            webServer.send(200, "text/plain", "ACK: System ARMED. Log cleared.");
        } else {
            Serial.println(F("System DISARMED."));
            webServer.send(200, "text/plain", "ACK: System DISARMED.");
        }
    } else {
        Serial.println(F("ERROR - Missing 'state' parameter."));
        webServer.send(400, "text/plain", "ERROR: Missing 'state' query parameter.");
    }
}

void httpHandleStateRequest() {
    JSONVar resp;
    resp["armed"] = g_isSystemArmed;
    resp["motionEventForWebUI"] = g_motionEventForWebUI;
    resp["ntp_syncd"] = g_ntpTimeSyncd;

    JSONVar logArr;
    if (g_isSystemArmed) {
        for (int i = 0; i < g_motionLogCount; i++) {
            logArr[i] = g_motionLog[i]; 
        }
    }
    resp["log"] = logArr;

    String jsonStr = JSON.stringify(resp);
    webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    webServer.send(200, "application/json", jsonStr);

    if (g_motionEventForWebUI) {
        g_motionEventForWebUI = false; 
    }
}

void httpHandleResetLogCommand() {
    Serial.print(F("HTTP REQ: /resetlog - "));
    if (g_isSystemArmed) {
        g_motionLogCount = 0;
        g_motionEventForWebUI = false; 
        Serial.println(F("Event log cleared while system remains ARMED."));
        webServer.send(200, "text/plain", "ACK: Event log cleared.");
    } else {
        Serial.println(F("Cannot clear log, system is DISARMED."));
        webServer.send(400, "text/plain", "ERROR: System is not armed. Cannot clear log.");
    }
}

void httpHandleNotFound() {
    Serial.print(F("HTTP REQ: 404 Not Found - URI: ")); Serial.println(webServer.uri());
    webServer.send(404, "text/plain", "404: Resource Not Found");
}

// --- Time Helper Function ---
String getFormattedTime() {
    if (g_ntpTimeSyncd) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timeStr[9]; 
            strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
            return String(timeStr);
        } else {
            return "NTP Err";
        }
    } else {
        if (g_isSystemArmed) {
            float secondsSinceArm = (millis() - g_armingTimeMillis) / 1000.0;
            char relTimeStr[10];
            dtostrf(secondsSinceArm, 6, 2, relTimeStr); 
            String s = String(relTimeStr);
            s.trim(); 
            return s + "s (Rel)";
        } else {
            return "No Time"; 
        }
    }
}
