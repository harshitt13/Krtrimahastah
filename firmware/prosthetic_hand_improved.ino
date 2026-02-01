#include <WiFi.h>
#include <SinricPro.h>
#include <SinricProDevice.h>
#include <SinricProSwitch.h> 
#include <ESP32Servo.h>

/* ================= USER CONFIG ================= */

#define WIFI_SSID         "YourWIFISSID"
#define WIFI_PASS         "YourWIFIPass"

#define APP_KEY           "YourAppKey"
#define APP_SECRET        "YourAppSecret"
#define DEVICE_ID         "YourDeviceID"


// ---------- PINS ----------
#define PIN_EMG           34
#define PIN_FSR_THUMB     36
#define PIN_FSR_INDEX     39

#define SERVO_THUMB       13
#define SERVO_INDEX       12
#define SERVO_MIDDLE      14
#define SERVO_RING        27
#define SERVO_PINKY       26

// ---------- TUNING ----------
#define EMG_OPEN_THR      60
#define EMG_CLOSE_THR     120
#define FSR_LIMIT         2000

#define SERVO_BASE_SPEED  15

#define MAX_TP            120
#define MAX_OTHERS        175

// ---------- SAFETY & STABILITY ----------
#define EMG_SAMPLE_COUNT       5     // Number of samples to average
#define FSR_SAMPLE_COUNT       3     // Number of samples to average
#define WIFI_RECONNECT_INTERVAL 30000 // 30 seconds
#define SERVO_UPDATE_INTERVAL   50   // Minimum time between servo updates (ms)
#define SAFETY_CHECK_INTERVAL   100  // How often to check safety sensors (ms)
#define EMG_DEBOUNCE_TIME      200   // Debounce time for EMG state changes (ms)
#define WATCHDOG_TIMEOUT       10000 // Reset if stuck for 10 seconds

/* ================= ENUMS & STRUCTS ================= */

enum HandState {
  STATE_IDLE,
  STATE_EMG_CONTROL,
  STATE_GESTURE,
  STATE_ANIMATION_ILY,
  STATE_ANIMATION_RPS,
  STATE_SAFETY_STOP
};

enum EmgState {
  EMG_NEUTRAL = 0,
  EMG_OPEN = -1,
  EMG_CLOSE = 1
};

struct ServoPosition {
  int thumb;
  int index;
  int middle;
  int ring;
  int pinky;
};

/* ================= GLOBAL STATE ================= */

Servo thumb, indexFinger, middle, ring, pinky;

HandState currentState = STATE_EMG_CONTROL;
EmgState emgState = EMG_NEUTRAL;
ServoPosition currentPos = {0, 0, 0, 0, 0};
ServoPosition targetPos = {0, 0, 0, 0, 0};

bool emgControlActive = true;
bool wifiConnected = false;
bool safetyOverride = false;

// ----- Timing variables -----
unsigned long lastServoUpdate = 0;
unsigned long lastSafetyCheck = 0;
unsigned long lastEmgChange = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastWatchdog = 0;

// ----- I LOVE YOU animation -----
bool ilyActive = false;
int ilyStage = 0;
unsigned long ilyTimer = 0;

// ----- ROCK PAPER SCISSORS animation -----
bool rpsActive = false;
int rpsStage = 0;
int rpsChoice = 0;
unsigned long rpsTimer = 0;

// ----- Sensor buffers for averaging -----
int emgBuffer[EMG_SAMPLE_COUNT] = {0};
int fsrThumbBuffer[FSR_SAMPLE_COUNT] = {0};
int fsrIndexBuffer[FSR_SAMPLE_COUNT] = {0};
int bufferIndex = 0;

/* ================= UTILITY FUNCTIONS ================= */

// Moving average filter for EMG
int getFilteredEMG() {
  emgBuffer[bufferIndex % EMG_SAMPLE_COUNT] = analogRead(PIN_EMG);
  
  int sum = 0;
  for (int i = 0; i < EMG_SAMPLE_COUNT; i++) {
    sum += emgBuffer[i];
  }
  
  return sum / EMG_SAMPLE_COUNT;
}

// Moving average filter for FSR sensors
int getFilteredFSR(int pin, int* buffer) {
  buffer[bufferIndex % FSR_SAMPLE_COUNT] = analogRead(pin);
  
  int sum = 0;
  for (int i = 0; i < FSR_SAMPLE_COUNT; i++) {
    sum += buffer[i];
  }
  
  return sum / FSR_SAMPLE_COUNT;
}

// Check if safety sensors detect excessive force
bool checkSafety() {
  int fsrThumb = getFilteredFSR(PIN_FSR_THUMB, fsrThumbBuffer);
  int fsrIndex = getFilteredFSR(PIN_FSR_INDEX, fsrIndexBuffer);
  
  if (fsrThumb > FSR_LIMIT || fsrIndex > FSR_LIMIT) {
    Serial.println("SAFETY: Force limit exceeded!");
    return false;
  }
  
  return true;
}

// Update EMG state with debouncing
void updateEmgState() {
  int emgVal = getFilteredEMG();
  EmgState newState = EMG_NEUTRAL;
  
  if (emgVal > EMG_CLOSE_THR) {
    newState = EMG_CLOSE;
  } else if (emgVal < EMG_OPEN_THR) {
    newState = EMG_OPEN;
  }
  
  // Only change state if debounce time has passed
  if (newState != emgState && (millis() - lastEmgChange > EMG_DEBOUNCE_TIME)) {
    emgState = newState;
    lastEmgChange = millis();
    
    Serial.print("EMG State: ");
    Serial.println(emgState == EMG_CLOSE ? "CLOSE" : (emgState == EMG_OPEN ? "OPEN" : "NEUTRAL"));
  }
}

// Constrain servo values to safe ranges
ServoPosition constrainPosition(ServoPosition pos) {
  pos.thumb = constrain(pos.thumb, 0, MAX_TP);
  pos.pinky = constrain(pos.pinky, 0, MAX_TP);
  pos.index = constrain(pos.index, 0, MAX_OTHERS);
  pos.middle = constrain(pos.middle, 0, MAX_OTHERS);
  pos.ring = constrain(pos.ring, 0, MAX_OTHERS);
  
  return pos;
}

// Check if target position is reached
bool positionReached() {
  return (abs(currentPos.thumb - targetPos.thumb) < 5 &&
          abs(currentPos.index - targetPos.index) < 5 &&
          abs(currentPos.middle - targetPos.middle) < 5 &&
          abs(currentPos.ring - targetPos.ring) < 5 &&
          abs(currentPos.pinky - targetPos.pinky) < 5);
}

/* ================= SERVO CONTROL ================= */

void setTargetPosition(int t, int i, int m, int r, int p) {
  targetPos = {t, i, m, r, p};
  targetPos = constrainPosition(targetPos);
}

void updateServos() {
  unsigned long now = millis();
  
  // Rate limit servo updates
  if (now - lastServoUpdate < SERVO_UPDATE_INTERVAL) {
    return;
  }
  
  lastServoUpdate = now;
  
  // Calculate speed multiplier based on EMG state
  int stepSize = SERVO_BASE_SPEED;
  if (emgState == EMG_CLOSE) stepSize *= 2;  // Faster closing
  if (emgState == EMG_OPEN) stepSize /= 2;   // Slower opening
  
  // Smoothly move towards target
  bool moved = false;
  
  if (currentPos.thumb != targetPos.thumb) {
    currentPos.thumb += (currentPos.thumb < targetPos.thumb) ? min(stepSize, targetPos.thumb - currentPos.thumb) 
                                                               : max(-stepSize, targetPos.thumb - currentPos.thumb);
    thumb.write(currentPos.thumb);
    moved = true;
  }
  
  if (currentPos.index != targetPos.index) {
    currentPos.index += (currentPos.index < targetPos.index) ? min(stepSize, targetPos.index - currentPos.index)
                                                               : max(-stepSize, targetPos.index - currentPos.index);
    indexFinger.write(currentPos.index);
    moved = true;
  }
  
  if (currentPos.middle != targetPos.middle) {
    currentPos.middle += (currentPos.middle < targetPos.middle) ? min(stepSize, targetPos.middle - currentPos.middle)
                                                                  : max(-stepSize, targetPos.middle - currentPos.middle);
    middle.write(currentPos.middle);
    moved = true;
  }
  
  if (currentPos.ring != targetPos.ring) {
    currentPos.ring += (currentPos.ring < targetPos.ring) ? min(stepSize, targetPos.ring - currentPos.ring)
                                                            : max(-stepSize, targetPos.ring - currentPos.ring);
    ring.write(currentPos.ring);
    moved = true;
  }
  
  if (currentPos.pinky != targetPos.pinky) {
    currentPos.pinky += (currentPos.pinky < targetPos.pinky) ? min(stepSize, targetPos.pinky - currentPos.pinky)
                                                               : max(-stepSize, targetPos.pinky - currentPos.pinky);
    pinky.write(currentPos.pinky);
    moved = true;
  }
  
  if (moved) {
    lastWatchdog = millis();  // Reset watchdog on successful movement
  }
}

/* ================= BASIC POSES ================= */

void poseOpen()      { setTargetPosition(0, 0, 0, 0, 0); }
void poseRelax()     { setTargetPosition(20, 20, 20, 20, 20); }
void poseRock()      { setTargetPosition(MAX_TP, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, MAX_TP); }
void posePaper()     { setTargetPosition(0, 0, 0, 0, 0); }
void poseScissors()  { setTargetPosition(MAX_TP, 0, 0, MAX_OTHERS, MAX_TP); }

// Functional gestures
void poseHook()      { setTargetPosition(0, 150, 150, 150, 150); }
void posePinch()     { setTargetPosition(MAX_TP, 140, MAX_OTHERS, MAX_OTHERS, MAX_TP); }
void poseTripod()    { setTargetPosition(100, 120, 120, MAX_OTHERS, MAX_TP); }

// Social gestures
void posePoint()     { setTargetPosition(MAX_TP, 0, MAX_OTHERS, MAX_OTHERS, MAX_TP); }
void poseMiddle()    { setTargetPosition(MAX_TP, MAX_OTHERS, 0, MAX_OTHERS, MAX_TP); }
void posePeace()     { setTargetPosition(MAX_TP, 0, 0, MAX_OTHERS, MAX_TP); }
void poseGun()       { setTargetPosition(0, 0, MAX_OTHERS, MAX_OTHERS, MAX_TP); }
void poseRockNRoll() { setTargetPosition(0, 0, MAX_OTHERS, MAX_OTHERS, 0); }
void poseCall()      { setTargetPosition(0, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, 0); }
void poseThumbsUp()  { setTargetPosition(0, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, MAX_TP); }
void poseOK()        { setTargetPosition(120, 140, 0, 0, 0); }
void poseLove()      { setTargetPosition(0, 0, MAX_OTHERS, MAX_OTHERS, 0); }
void poseThree()     { setTargetPosition(MAX_TP, 0, 0, 0, MAX_TP); }
void poseFour()      { setTargetPosition(MAX_TP, 0, 0, 0, 0); }
void posePinky()     { setTargetPosition(MAX_TP, MAX_OTHERS, MAX_OTHERS, MAX_OTHERS, 0); }

// ILY animation poses
void pose_I() { setTargetPosition(120, 175, 175, 175, 0); }
void pose_L() { setTargetPosition(0, 0, 175, 175, 120); }
void pose_Y() { setTargetPosition(0, 175, 175, 175, 0); }

/* ================= STATE MACHINE ================= */

void enterSafetyStop() {
  Serial.println("STATE: SAFETY_STOP");
  currentState = STATE_SAFETY_STOP;
  safetyOverride = true;
  poseOpen();  // Open hand immediately
}

void enterIdleState() {
  Serial.println("STATE: IDLE");
  currentState = STATE_IDLE;
  poseRelax();
}

void enterEmgControl() {
  Serial.println("STATE: EMG_CONTROL");
  currentState = STATE_EMG_CONTROL;
}

void enterGesture() {
  Serial.println("STATE: GESTURE");
  currentState = STATE_GESTURE;
}

/* ================= I LOVE YOU ENGINE ================= */

void startILoveYou() {
  ilyActive = true;
  ilyStage = 0;
  ilyTimer = millis();
  currentState = STATE_ANIMATION_ILY;
  pose_I();
  Serial.println("Starting I Love You animation");
}

void handleILoveYouSequence() {
  if (!ilyActive) return;
  
  // Cancel on safety trigger or EMG open signal
  if (!checkSafety() || emgState == EMG_OPEN) {
    Serial.println("ILY animation cancelled");
    ilyActive = false;
    currentState = STATE_EMG_CONTROL;
    poseOpen();
    return;
  }
  
  unsigned long now = millis();
  if (now - ilyTimer < 1000) return;
  
  // Wait for position to be reached before advancing
  if (!positionReached()) return;
  
  ilyTimer = now;
  ilyStage++;
  
  if (ilyStage == 1) {
    pose_L();
  } else if (ilyStage == 2) {
    pose_Y();
  } else {
    Serial.println("ILY animation complete");
    ilyActive = false;
    currentState = STATE_EMG_CONTROL;
  }
}

/* ================= RPS ENGINE ================= */

void startRPS() {
  rpsActive = true;
  rpsStage = 0;
  rpsChoice = random(0, 3);
  rpsTimer = millis();
  currentState = STATE_ANIMATION_RPS;
  Serial.print("Starting RPS animation - choice: ");
  Serial.println(rpsChoice == 0 ? "ROCK" : (rpsChoice == 1 ? "PAPER" : "SCISSORS"));
}

void handleRPSSequence() {
  if (!rpsActive) return;
  
  // Cancel on safety trigger or EMG open signal
  if (!checkSafety() || emgState == EMG_OPEN) {
    Serial.println("RPS animation cancelled");
    rpsActive = false;
    currentState = STATE_EMG_CONTROL;
    poseOpen();
    return;
  }
  
  unsigned long now = millis();
  if (now - rpsTimer < 300) return;
  
  // Wait for position to be reached before advancing
  if (!positionReached()) return;
  
  rpsTimer = now;
  rpsStage++;
  
  // Bounce animation (3 times)
  if (rpsStage <= 6) {
    if (rpsStage % 2 == 0) {
      poseRock();
    } else {
      setTargetPosition(80, 140, 140, 140, 80);
    }
    return;
  }
  
  // Final reveal
  if (rpsStage == 7) {
    if (rpsChoice == 0) poseRock();
    else if (rpsChoice == 1) posePaper();
    else poseScissors();
    return;
  }
  
  Serial.println("RPS animation complete");
  rpsActive = false;
  currentState = STATE_EMG_CONTROL;
}

/* ================= WIFI MANAGEMENT ================= */

void checkWifiConnection() {
  unsigned long now = millis();
  
  if (now - lastWifiCheck < WIFI_RECONNECT_INTERVAL) return;
  lastWifiCheck = now;
  
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    Serial.println("WiFi disconnected, attempting reconnect...");
    wifiConnected = false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    delay(100);
  } else if (!wifiConnected) {
    Serial.println("WiFi reconnected!");
    wifiConnected = true;
  }
}

/* ================= SINRIC CALLBACKS ================= */

bool onSetMode(const String&, const String&, String& mode) {
  mode.toLowerCase();
  Serial.print("Gesture command received: ");
  Serial.println(mode);
  
  // Animation commands
  if (mode == "play rock paper scissors" || mode == "rock paper scissors") {
    startRPS();
    return true;
  }
  
  if (mode == "i love you") {
    startILoveYou();
    return true;
  }
  
  // Stop animations and enter gesture mode
  ilyActive = false;
  rpsActive = false;
  emgControlActive = false;
  enterGesture();
  
  // Functional gestures
  if (mode == "grab" || mode == "fist" || mode == "close") {
    poseRock();
  }
  else if (mode == "hook") {
    poseHook();
  }
  else if (mode == "pinch") {
    posePinch();
  }
  else if (mode == "tripod") {
    poseTripod();
  }
  // Social gestures
  else if (mode == "open" || mode == "five" || mode == "paper") {
    poseOpen();
  }
  else if (mode == "relax") {
    poseRelax();
  }
  else if (mode == "point" || mode == "index" || mode == "one") {
    posePoint();
  }
  else if (mode == "middle" || mode == "fuck" || mode == "fuck you" || mode == "fuck off") {
    poseMiddle();
  }
  else if (mode == "peace" || mode == "victory" || mode == "two" || mode == "scissor" || mode == "scissors") {
    posePeace();
  }
  else if (mode == "gun" || mode == "l" || mode == "l sign" || mode == "loser") {
    poseGun();
  }
  else if (mode == "rock" || mode == "rock n roll" || mode == "rock and roll") {
    poseRockNRoll();
  }
  else if (mode == "call" || mode == "y" || mode == "y sign") {
    poseCall();
  }
  else if (mode == "thumbs up" || mode == "like") {
    poseThumbsUp();
  }
  else if (mode == "ok" || mode == "okay") {
    poseOK();
  }
  else if (mode == "love") {
    poseLove();
  }
  else if (mode == "pinky") {
    posePinky();
  }
    else if (mode == "four") {
    poseFour();
  }
  else if (mode == "three") {
    poseThree();
  }
  else {
    Serial.println("Unknown gesture command");
    return false;
  }
  
  return true;
}

bool onPowerState(const String&, bool &state) {
  emgControlActive = state;
  Serial.print("EMG Control: ");
  Serial.println(state ? "ENABLED" : "DISABLED");
  
  if (state) {
    enterEmgControl();
  } else {
    enterIdleState();
  }
  
  return true;
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Prosthetic Hand Control Starting ===");
  
  randomSeed(analogRead(0));
  
  // Initialize PWM timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  // Attach servos
  Serial.println("Attaching servos...");
  thumb.attach(SERVO_THUMB);
  indexFinger.attach(SERVO_INDEX);
  middle.attach(SERVO_MIDDLE);
  ring.attach(SERVO_RING);
  pinky.attach(SERVO_PINKY);
  
  // Initialize to open position
  poseOpen();
  delay(100);
  updateServos();  // Force immediate update
  
  Serial.println("Servos initialized");
  
  // Initialize sensor buffers
  for (int i = 0; i < EMG_SAMPLE_COUNT; i++) {
    emgBuffer[i] = analogRead(PIN_EMG);
  }
  for (int i = 0; i < FSR_SAMPLE_COUNT; i++) {
    fsrThumbBuffer[i] = analogRead(PIN_FSR_THUMB);
    fsrIndexBuffer[i] = analogRead(PIN_FSR_INDEX);
  }
  
  // WiFi connection
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Initialize SinricPro
    SinricProDevice &hand = SinricPro[DEVICE_ID];

    hand.onPowerState(onPowerState);
    hand.onSetMode("handGesture", onSetMode);

    SinricPro.begin(APP_KEY, APP_SECRET);
    Serial.println("SinricPro initialized");
  } else {
    wifiConnected = false;
    emgControlActive = true;
    Serial.println("WiFi connection failed - running in offline mode");
  }
  
  lastWatchdog = millis();
  Serial.println("=== Setup Complete ===\n");
}

/* ================= MAIN LOOP ================= */

void loop() {
  unsigned long now = millis();
  
  // Watchdog check
  if (now - lastWatchdog > WATCHDOG_TIMEOUT) {
    Serial.println("WATCHDOG: System appears stuck, resetting to safe state");
    enterSafetyStop();
    lastWatchdog = now;
  }
  
  // Update sensor buffer index
  bufferIndex++;
  
  // Handle WiFi
  if (wifiConnected) {
    SinricPro.handle();
    checkWifiConnection();
  }
  
  // Update EMG state
  updateEmgState();
  
  // Safety check
  if (now - lastSafetyCheck > SAFETY_CHECK_INTERVAL) {
    lastSafetyCheck = now;
    
    if (!checkSafety() && currentState != STATE_SAFETY_STOP) {
      enterSafetyStop();
    } else if (safetyOverride && checkSafety()) {
      // Clear safety override after force is removed
      safetyOverride = false;
      enterEmgControl();
    }
  }
  
  // State machine
  switch (currentState) {
    case STATE_ANIMATION_RPS:
      handleRPSSequence();
      break;
      
    case STATE_ANIMATION_ILY:
      handleILoveYouSequence();
      break;
      
    case STATE_EMG_CONTROL:
      if (emgControlActive && !safetyOverride) {
        if (emgState == EMG_CLOSE) {
          poseRock();
        } else if (emgState == EMG_OPEN) {
          poseOpen();
        }
      }
      break;
      
    case STATE_SAFETY_STOP:
      // Remain stopped until safety clears
      break;
      
    case STATE_IDLE:
    case STATE_GESTURE:
      // Do nothing, waiting for commands
      break;
  }
  
  // Update servos smoothly
  updateServos();
  
  // Small delay to prevent excessive loop speed
  delay(10);
}
