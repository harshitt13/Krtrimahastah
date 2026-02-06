/*
 * ====================================================================
 * PROSTHETIC HAND - HYBRID CONTROL (Voice + Muscle + Global Safety)
 * ====================================================================
 * Priority Hierarchy:
 * 1. FSR Safety (Global Brake - Stops ALL fingers on contact)
 * 2. Sinric Pro (Voice Command - pauses EMG for 3s)
 * 3. EMG Sensor (Muscle Control - default state)
 */

#include <WiFi.h>
#include <SinricPro.h>
#include <SinricProSpeaker.h>
#include <ESP32Servo.h>

// ======================= CONFIGURATION =======================

#define WIFI_SSID         "WIFI_SSID"
#define WIFI_PASS         "WIFI_PASS"
#define APP_KEY           "APP_KEY"
#define APP_SECRET        "APP_SECRET"
#define DEVICE_ID         "DEVICE_ID"
#define MODE_INSTANCE     "handGesture" 

// --- SERVO PINS ---
#define PIN_THUMB   13
#define PIN_INDEX   12
#define PIN_MIDDLE  14
#define PIN_RING    27
#define PIN_PINKY   26

// --- SENSOR PINS ---
#define PIN_EMG       34  // Muscle Sensor
#define PIN_FSR_THUMB 36  // Pressure Sensor Thumb (VP)
#define PIN_FSR_INDEX 39  // Pressure Sensor Index (VN)

// --- THRESHOLDS ---
#define EMG_THRESHOLD     700  
#define FSR_LIMIT         4000  // Adjust this: Higher = harder squeeze allowed
#define EMG_PAUSE_TIME    3000  

// --- SERVO LIMITS ---
#define SERVO_MIN   0
#define MAX_TP      120 // Thumb/Pinky max
#define MAX_OTHERS  175 // Remaining Fingers max

// ======================= GLOBAL VARS =======================

Servo thumb, indexFinger, middle, ring, pinky;

struct HandPosition {
  int thumb;
  int index;
  int middle;
  int ring;
  int pinky;
};

HandPosition currentPos = {0, 0, 0, 0, 0};
HandPosition targetPos  = {0, 0, 0, 0, 0};

bool powerState = true;
unsigned long lastSinricActionTime = 0; 
const int stepSize = 15;

// ======================= SERVO LOGIC =======================

void setTargetPosition(int t, int i, int m, int r, int p) {
  targetPos.thumb = t;
  targetPos.index = i;
  targetPos.middle = m;
  targetPos.ring = r;
  targetPos.pinky = p;
}

void updateServos() {
  // Simple function to move servos incrementally toward target
  if (currentPos.thumb != targetPos.thumb) {
    currentPos.thumb += constrain(targetPos.thumb - currentPos.thumb, -stepSize, stepSize);
    thumb.write(currentPos.thumb);
  }
  if (currentPos.index != targetPos.index) {
    currentPos.index += constrain(targetPos.index - currentPos.index, -stepSize, stepSize);
    indexFinger.write(currentPos.index);
  }
  if (currentPos.middle != targetPos.middle) {
    currentPos.middle += constrain(targetPos.middle - currentPos.middle, -stepSize, stepSize);
    middle.write(currentPos.middle);
  }
  if (currentPos.ring != targetPos.ring) {
    currentPos.ring += constrain(targetPos.ring - currentPos.ring, -stepSize, stepSize);
    ring.write(currentPos.ring);
  }
  if (currentPos.pinky != targetPos.pinky) {
    currentPos.pinky += constrain(targetPos.pinky - currentPos.pinky, -stepSize, stepSize);
    pinky.write(currentPos.pinky);
  }
}

// ======================= POSE DEFINITIONS =======================

void poseOpen()      { setTargetPosition(0, 0, 0, 0, 0); }
void poseRelax()     { setTargetPosition(20, 20, 20, 20, 20); }
void poseClose()     { setTargetPosition(MAX_TP, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, MAX_TP); } 
void poseOK()        { setTargetPosition(120, 140, 0, 0, 0); }
void poseHook()      { setTargetPosition(0, 150, 150, 150, 150); }
void posePoint()     { setTargetPosition(MAX_TP, 0, MAX_OTHERS, MAX_OTHERS, MAX_TP); }
void poseTwo()       { setTargetPosition(MAX_TP, 0, 0, MAX_OTHERS, MAX_TP); }
void poseThree()     { setTargetPosition(MAX_TP, 0, 0, 0, MAX_TP); }
void poseFour()      { setTargetPosition(MAX_TP, 0, 0, 0, 0); }
void poseCall()      { setTargetPosition(0, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, 0); }
void posePinky()     { setTargetPosition(MAX_TP, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, 0); }
void poseThumbsUp()  { setTargetPosition(0, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, MAX_TP); }
void poseFuckYou()   { setTargetPosition(MAX_TP, MAX_OTHERS, 0, MAX_OTHERS, MAX_TP); }
void poseGun()       { setTargetPosition(0, 0, MAX_OTHERS, MAX_OTHERS, MAX_TP); }

// ======================= SENSOR & PLOTTING LOGIC =======================

// Function to plot data on Serial Plotter
void plotSensorData() {
  int emg = analogRead(PIN_EMG);
  int fsrThumb = analogRead(PIN_FSR_THUMB);
  int fsrIndex = analogRead(PIN_FSR_INDEX);

  Serial.print("EMG:");
  Serial.print(emg);
  Serial.print(",");
  
  Serial.print("EMG_Threshold:");
  Serial.print(EMG_THRESHOLD);
  Serial.print(",");

  Serial.print("FSR_Thumb:");
  Serial.print(fsrThumb);
  Serial.print(",");

  Serial.print("FSR_Index:");
  Serial.print(fsrIndex);
  Serial.print(",");

  Serial.print("FSR_Limit:");
  Serial.println(FSR_LIMIT);
}

void checkSensors() {
  // DETERMINE INTENDED MOVEMENT (EMG or Voice)
  // Check if Voice Command is active (pausing EMG)
  bool voiceActive = (millis() - lastSinricActionTime < EMG_PAUSE_TIME);

  if (!voiceActive) {
     int emgVal = analogRead(PIN_EMG) - 3300;
     if (emgVal > EMG_THRESHOLD) {
        poseClose(); // User wants to close
     } else {
        poseOpen();  // User wants to open
     }
  }

  // FSR GLOBAL SAFETY BRAKE
  int fsrThumbVal = analogRead(PIN_FSR_THUMB);
  int fsrIndexVal = analogRead(PIN_FSR_INDEX);
  bool pressureDetected = (fsrThumbVal > FSR_LIMIT) || (fsrIndexVal > FSR_LIMIT);

  if (pressureDetected) {
      if (targetPos.thumb > currentPos.thumb)   targetPos.thumb = currentPos.thumb;
      if (targetPos.index > currentPos.index)   targetPos.index = currentPos.index;
      if (targetPos.middle > currentPos.middle) targetPos.middle = currentPos.middle;
      if (targetPos.ring > currentPos.ring)     targetPos.ring = currentPos.ring;
      if (targetPos.pinky > currentPos.pinky)   targetPos.pinky = currentPos.pinky;
  }
}

// ======================= SINRICPRO CALLBACKS =======================

bool onPowerState(const String &deviceId, bool &state) {
  Serial.printf("Power: %s \r\n", state ? "ON" : "OFF");
  powerState = state;
  if (state) poseRelax(); else poseOpen();
  lastSinricActionTime = millis(); 
  return true;
}

bool onSetMode(const String& deviceId, const String& instance, String &mode) {
  Serial.printf("Command: %s\r\n", mode.c_str());
  if (!powerState) return false;

  lastSinricActionTime = millis(); // Block EMG for 3 seconds

  String cleanMode = mode;
  cleanMode.toLowerCase();
  cleanMode.replace(" ", "");

  if      (cleanMode == "open")       poseOpen();
  else if (cleanMode == "close")      poseClose();
  else if (cleanMode == "grab")       poseClose();
  else if (cleanMode == "relax")      poseRelax();
  else if (cleanMode == "ok")         poseOK();
  else if (cleanMode == "hook")       poseHook();
  else if (cleanMode == "point")      posePoint();
  else if (cleanMode == "two")        poseTwo();
  else if (cleanMode == "three")      poseThree();
  else if (cleanMode == "four")       poseFour();
  else if (cleanMode == "call")       poseCall();
  else if (cleanMode == "pinky")      posePinky();
  else if (cleanMode == "thumbsup")   poseThumbsUp();
  else if (cleanMode == "fuckyou")    poseFuckYou();
  else if (cleanMode == "gun")        poseGun();
  else return false; 

  return true;
}

// ======================= SETUP & LOOP =======================

void setup() {
  Serial.begin(115200);

  // Servos
  thumb.attach(PIN_THUMB);
  indexFinger.attach(PIN_INDEX);
  middle.attach(PIN_MIDDLE);
  ring.attach(PIN_RING);
  pinky.attach(PIN_PINKY);
  
  // Sensors
  pinMode(PIN_EMG, INPUT);
  pinMode(PIN_FSR_THUMB, INPUT);
  pinMode(PIN_FSR_INDEX, INPUT);
  
  poseOpen(); 

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" OK!");

  // SinricPro
  SinricProSpeaker &myHand = SinricPro[DEVICE_ID];
  myHand.onPowerState(onPowerState);
  myHand.onSetMode(MODE_INSTANCE, onSetMode);
  SinricPro.begin(APP_KEY, APP_SECRET);
}

void loop() {
  SinricPro.handle();
  
  if (powerState) {
    checkSensors(); // Control logic
    updateServos(); // Motor movement
    plotSensorData(); // Sends data to Serial Plotter
  }
  
  delay(10); 
}