# Krtrimahastah: AI-Powered Low-Cost Prosthetic Hand

![Project Status](https://img.shields.io/badge/Status-Prototype_Complete-success)
![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![License](https://img.shields.io/badge/License-MIT-green)

**Krtrimahastah** (Sanskrit for *Artificial Hand*) is an open-source, affordable, and intelligent prosthetic arm designed to bridge the gap between expensive bionic limbs and passive cosmetic devices. 

By leveraging 3D printing, the ESP32 microcontroller, and Cloud AI, this project delivers a functional, multi-modal assistive device for under **$100 USD**.

---

## 🌟 Key Features

* **Multi-Modal Control:** Hybrid input system using **Voice Commands** (Primary) and **EMG Muscle Signals** (Secondary/Backup).
* **AI-Powered Intelligence:** Integrates **Google Cloud Speech-to-Text API** to understand complex commands like "Point," "Peace Sign," or "Pinch."
* **Haptic Feedback:** Force Sensitive Resistors (FSRs) in the fingertips enable a closed-loop grip, preventing the crushing of delicate objects.
* **Tendon-Driven Actuation:** Bio-inspired mechanical design using 5 independent MG90s servos for anthropomorphic movement.
* **Portable Power Management:** Solves the "low-current shutdown" issue common in consumer power banks, allowing for reliable all-day portability using a Xiaomi 4i.
* **Frugal Innovation:** Built entirely with off-the-shelf hobbyist components and FDM 3D printing.

---

## 🛠️ Hardware Architecture

### Bill of Materials (BOM)
| Component | Function |
| :--- | :--- |
| **ESP32 DevKit V1** | Main Controller (32-bit, Wi-Fi/BT) |
| **MG90s Servos (x5)** | Actuators for individual fingers |
| **INMP441 Microphone** | I2S Digital Mic for Voice Commands |
| **EMG Sensor (V3.0)** | Analog Muscle Signal Acquisition |
| **FSR402 Sensors (x2)** | Pressure/Tactile Feedback |
| **Xiaomi Power Bank 4i** | 20000mAh Portable Power Source |
| **Tactile Pushbutton** | Push-to-Talk Trigger |
| **3D Printed Chassis** | PLA parts (Palm, Phalanges, Forearm) |

### 🔌 Master Pinout (Left-Side Layout)
The circuit is designed to optimize cable routing within the forearm chassis.

| Component | Pin Name | ESP32 GPIO | Note |
| :--- | :--- | :--- | :--- |
| **Servo: Thumb** | Signal | `GPIO 13` | PWM Output |
| **Servo: Index** | Signal | `GPIO 12` | PWM Output |
| **Servo: Middle** | Signal | `GPIO 14` | PWM Output |
| **Servo: Ring** | Signal | `GPIO 27` | PWM Output |
| **Servo: Pinky** | Signal | `GPIO 26` | PWM Output |
| **Voice Trigger** | Button | `GPIO 25` | Input (Pull-down) |
| **Mic (INMP441)** | WS (Word) | `GPIO 33` | I2S Clock |
| **Mic (INMP441)** | SCK (Clock)| `GPIO 32` | I2S Clock |
| **Mic (INMP441)** | SD (Data) | `GPIO 35` | I2S Data In |
| **EMG Sensor** | SIG | `GPIO 34` | Analog Input (ADC1) |
| **FSR (Thumb)** | Signal | `GPIO 36` | Analog Input (ADC1) |
| **FSR (Finger)** | Signal | `GPIO 39` | Analog Input (ADC1) |

---

## ⚙️ How It Works

### 1. The Control Logic
The hand operates in two distinct modes to ensure reliability in all environments:

* **Mode A: Smart Voice Control (Online)**
    * User holds the **Push-to-Talk** button.
    * Audio is streamed via Wi-Fi to the **Google Cloud Speech-to-Text API**.
    * The returned text (e.g., "Close Fist") is parsed by the ESP32.
    * Servos execute the corresponding pre-programmed pattern.

* **Mode B: EMG Toggle (Offline/Silent)**
    * Used for rapid, frequent actions without speaking.
    * A single muscle flex (detected by the EMG sensor) acts as a **binary toggle**.
    * If Open -> Close. If Closed -> Open.

### 2. Power Management (The "Xiaomi Hack")
Standard power banks often shut down when powering microcontrollers because the current draw is too low during idle states.
* **Solution:** We utilize the **"Low Current Mode"** of the Xiaomi Power Bank 4i (activated by double-pressing the power button).
* **Wiring:** A split-rail breadboard design separates the high-noise servo power rail (5V) from the sensitive logic power rail (3.3V via ESP32 regulator) to prevent brownouts.

---

## 💻 Software Setup

### Prerequisites
* [Arduino IDE](https://www.arduino.cc/en/software)
* **Google Cloud Console Account** (for API Key)

### Required Libraries
Install these via the Arduino Library Manager:
* `ESP32Servo` (by Kevin Harrington)
* `ArduinoJson` (by Benoit Blanchon)
* `WiFiClientSecure` (Built-in)

### Installation
1.  **Clone the Repo:**
    ```bash
    git clone [https://github.com/yourusername/krtrimahastah-prosthetic.git](https://github.com/yourusername/krtrimahastah-prosthetic.git)
    ```
2.  **Configure Credentials:**
    * Rename `config_template.h` to `config.h`.
    * Enter your Wi-Fi SSID, Password, and Google API Key.
3.  **Calibrate Servos:**
    * Upload `calibration_zero.ino` first.
    * Attach tendons while servos are electrically locked at 0 degrees.
4.  **Upload Main Firmware:**
    * Upload `main_controller.ino` to the ESP32.

---

## ⚠️ Disclaimer
*This project is a research prototype intended for educational and developmental purposes. It is not a medically certified device.*