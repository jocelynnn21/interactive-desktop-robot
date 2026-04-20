# Mini Interactive Desktop Robot 

An interactive ESP32-based mini robot designed to sense, react, and express simple behaviors in real time.
Built as a hands-on robotics project to explore microcontroller programming, sensor integration, and actuator control.

---

## Features

- **Proximity detection** via HC-SR04 ultrasonic sensor
- **Animated OLED face** — calm at rest, happy with hearts when petted
- **Automatic fan control** — servo motor sweeps when temperature exceeds threshold
- **ESP32 hardware timers** — ultrasonic runs at 50Hz, temperature at 32Hz
- **FreeRTOS multitasking** — 4 tasks across 2 cores
- **Binary semaphore** — safely shares I2C bus between OLED and AM2320
- **Queue-based communication** between input and output tasks

---

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32-S3 |
| Display | SH1106 128x64 OLED (I2C) |
| Proximity Sensor | HC-SR04 Ultrasonic |
| Temperature/Humidity Sensor | AM2320 (I2C) |
| Fan | Servo motor (ESP32Servo) |

---

## Pin Definitions

| Pin | GPIO | Description |
|-----|------|-------------|
| `TRIG_PIN` | 1 | Ultrasonic trigger |
| `ECHO_PIN` | 2 | Ultrasonic echo |
| `LED_PIN` | 42 | Status LED |
| `SDA_PIN` | 18 | I2C data (OLED + AM2320) |
| `SCL_PIN` | 17 | I2C clock (OLED + AM2320) |
| `SERVO_PIN` | 4 | Servo motor (fan) |

---

## How It Works
 
```
Hardware Timers
├── Timer 1 (50Hz / 20ms)      → sets readPetting flag
└── Timer 2 (32Hz / 31.25ms)   → sets readTemp flag
 
[Core 1] motionTask
    └── Polls readPetting flag
    └── Fires ultrasonic pulse, calculates distance
    └── If distance <= 10cm → sends true to petQueue
 
[Core 1] temperatureTask
    └── Polls readTemp flag
    └── Reads AM2320 via I2C (protected by semaphore)
    └── Sends temperature to tempQueue
 
[Core 0] faceDisplayTask
    └── Reads petQueue (non-blocking)
    └── Happy face with hearts if petted, calm face otherwise
    └── Uses semaphore to safely access I2C (OLED)
 
[Core 0] fanTask
    └── Reads tempQueue (non-blocking)
    └── If temp >= 24°C → sweeps servo 0→180→0 degrees
```
 
---

## Timing
 
| Parameter | Value |
|-----------|-------|
| Ultrasonic polling rate | 50 Hz (every 20ms) |
| Temperature polling rate | 32 Hz (every 31.25ms) |
| Pet detection threshold | ≤ 10 cm |
| Fan trigger threshold | ≥ 24°C |
| Servo sweep range | 0° → 180° → 0° |
 
---

## Dependencies

- [U8g2](https://github.com/olikraus/u8g2) — OLED display library
- [Adafruit AM2320](https://github.com/adafruit/Adafruit_AM2320) — Temperature/humidity sensor
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo) — Servo motor control
- FreeRTOS (bundled with ESP32 Arduino core)

---

## Project Structure

```
interactive-desktop-robot/
├── src/
│   └── desk_pet.ino       # All task and setup logic
└── README.md
```

---

## Future Ideas

- 🧠 TinyML mood classifier — learns your petting patterns over time and personalizes reactions
- 🎤 Voice detection — wake up or react when you say its name using TinyML keyword spotting
- 📱 BLE companion app — see desk pet's mood history and temperature logs on your phone
