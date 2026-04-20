/**
 * @file desk_pet.ino
 * @author Grace Liu, Jocelyn He
 * @date 3/15/2026
 * @brief This file programs a Desk Pet that helps students stay motivated during studying. The user can pet the Desk Pet, causing it to change from a calm face to a happy loving
 * face with hearts. Additionally, the Desk Pet helps regulate the study room temperature by turning on a fan (servo motor) if the temperature of the room 
 * is above a certain defined threshold.

 * This file has 4 tasks, 2 input tasks and 2 output tasks. The input tasks take readings from a temperature sensor and an ultrasonic sensor, where the 
 * output tasks control a fan (servo motor) and OLED Display depending on readings from the input tasks. The tasks communicate using queues (petQueue and fanQueue).
 * Further, because both devices use the I2C bus, a binary semaphore is used. The rate at which the sensors are read are controlled using hardware timers,
 * with the tmperature sensor taking readings at 32Hz and the ultrasonic sensor takes readings at 50Hz.
*/

// INCLUDES
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <Wire.h>
#include <Adafruit_AM2320.h>
#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <ESP32Servo.h>
#include "driver/gpio.h"
#include "esp_timer.h"

// MACROS
#define TOGGLE_INTERVAL_US_PET 20000 // 50 Hz freq
#define TOGGLE_INTERVAL_US_FAN 31250 // 32 Hz freq
#define SDA_PIN 18
#define SCL_PIN 17
#define LED_PIN 42
#define TRIG_PIN 1
#define ECHO_PIN 2
#define SERVO_PIN 4
#define TEMP_THRESHOLD 24
#define PET_DISTANCE 10

#define PET_TASK_HZ 128
#define TEMP_TASK_HZ 0.5f
#define TEMP_DELAY_MS 10
#define PET_DELAY_MS (1000 / PET_TASK_HZ)

// GLOBAL VARIABLES
esp_timer_handle_t periodic_timer_pet;
esp_timer_handle_t periodic_timer_fan;
volatile bool readPetting = false;
bool readTemp = false;
long duration;
int distance;
QueueHandle_t petQueue; // To send data between motionTask() and faceDisplayTask()
QueueHandle_t tempQueue; // To send data between temperatureTask() and fanTask()
SemaphoreHandle_t xBinarySemaphore; // Semaphore for I2C bus
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* SCL=*/ SCL_PIN, /* SDA=*/ SDA_PIN); // OLED display
Adafruit_AM2320 am2320; // Temperature sensor
Servo servo; // Servo motor

// FUNCTION PROTOTYPES
void IRAM_ATTR onTimerPet(void* pvParameters);
void IRAM_ATTR onTimerFan(void* pvParameters);
void faceDisplayTask(void *pvParameters);
void temperatureTask (void *pvParameters);
void motionTask(void *pvParameters);
void fanTask (void *pvParameters);

/**
 * @brief Sets readPetting to be true. Triggers every 20ms (50Hz)
 *
 * @param pvParameters Pointer to task paramteters
*/
void IRAM_ATTR onTimerPet(void* pvParameters) {
  readPetting = true;
}

/**
 * @brief Sets readTemp to be true. Triggers every 31.25ms (32 Hz)
 *
 * @param pvParameters Pointer to task paramteters
*/
void IRAM_ATTR onTimerFan(void* pvParameters) {
  readTemp = true;
}

/**
 * @brief Reads temperature from the AM2320 sensor periodically.
 *
 * This task polls the 'readTemp' flag, which is set by a 32Hz hardware 
 * timer ISR. When this flag is true (32Hz frequency), this task reads
 * the current temperature in Celsius via I2C using xBinarySemaphore
 * to safely share the bus. Valid readings are forwarded to tempQueue
 * for fanTask to consume.
 * @param pvParameters Pointer to task paramteters
*/
void temperatureTask (void *pvParameters) {
  while (1) {
    // Poll readTemp flag set by onTimerFan() ISR
    if (readTemp) {
      float temp;
      // Blocks until xBinarySemaphore is free
      if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) == pdTRUE) {
        // Read temp of the room (in Celsius)
        temp = am2320.readTemperature();
        // Give back the semaphore
        xSemaphoreGive(xBinarySemaphore);
      }

      // Prints a warning to serial output if temp reading failed, otherwise print the temperature reading
      if (isnan(temp)) {
        Serial.println("[Core1] am2320 read failed, retrying...");
      } else {
        Serial.printf("[Core1] Temp: %.1f°C\n", temp);
        // Send temperature to fan control queue (ovrwrite so it stores only the latest temp)
        xQueueOverwrite(tempQueue, &temp);
      }
      // Set flag to be false (ensuring that the temp only gets read at 32Hz)
      readTemp = false;
    }
    vTaskDelay(pdMS_TO_TICKS(TEMP_DELAY_MS));
  }
}

/**
 * @brief Detects petting gestures using an HC-SR04 ultrasonic sensor.
 *
 * This task polls the 'readPetting' flag, which is set by a 50Hz hardware 
 * timer ISR. When this flag is true (50Hz frequency), this task pulses the
 * trigger pin and measures the echo duration to calculate distance.
 * If an object is detected within PET_DISTANCE (10cm), a value is sent to
 * petQueue for the display task to consume.
 * @param pvParameters Pointer to task paramteters
*/
void motionTask(void *pvParameters) {
  bool petDetected = false;

  while (1) {
    // Poll readPetting flag set by onTimerPet() ISR
    if (readPetting) {
      // Ensure trigger is low for a clean high pulse
      digitalWrite(TRIG_PIN, LOW);
      delayMicroseconds(2);
      // Send a 10 microsecond pulse to initiate the sonic burst
      digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIG_PIN, LOW);
      
      // Read the echoPin, returns the sound wave travel time in microseconds
      duration = pulseIn(ECHO_PIN, HIGH);
      // Calculating the distance (speed of sound is 340 m/s or 0.034 cm/us)
      distance = duration * 0.034 / 2;
      // Define petDetected to be if motion less than 10cm is detected
      petDetected = (distance <= PET_DISTANCE && distance > 0);
    
      // Only send petDetected to the queue if petting was detected
      if (petDetected) {
        xQueueSend(petQueue, &petDetected, 0);
      }

      // Set flag to be false (ensuring that the petting motion only gets polled at 50Hz)
      readPetting = false;
    }
    vTaskDelay(pdMS_TO_TICKS(PET_DELAY_MS));
  }
}

/**
 * @brief Controls a servo motor (that is connected to a plastic fan) based on temperature readings.
 *
 * Receives temperature values from tempQueue and sweeps the servo
 * from 0 to 180 degrees and back when temperature meets or exceeds
 * TEMP_THRESHOLD (27°C), simulating a fan cooling motion.
 * @param pvParameters Pointer to task paramteters
*/
void fanTask (void *pvParameters) {
  float currTemp;

  while (1) {
    if (xQueueReceive(tempQueue, &currTemp, 0) == pdTRUE) {
      
      // Only start the fan if the temperature reading is above or equal to TEMP_THRESHOLD
      if (currTemp >= TEMP_THRESHOLD) {
        // Sweep motor (fan) from 0 to 180 degrees
        for(int posDegrees = 0; posDegrees <= 180; posDegrees++) {
          servo.write(posDegrees);
          vTaskDelay(pdMS_TO_TICKS(20));
        }
        // Sweep motor (fan) from 180 to 0 degrees
        for(int posDegrees = 180; posDegrees >= 0; posDegrees--) {
          servo.write(posDegrees);
          vTaskDelay(pdMS_TO_TICKS(20));
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/**
 * @brief Renders facial expressions on the SH1106 128x64 OLED display.
 *
 * Listens on petQueue for petting events. When a pet is detected,
 * draws a happy face with hearts. Otherwise displays a calm resting
 * face. Uses xBinarySemaphore to safely share the I2C bus with
 * other tasks.
 * @param pvParameters Pointer to task paramteters
*/
void faceDisplayTask(void *pvParameters) {
  bool pettedFace = false;

  while (1) {
    // If something is recieved in petQueue, display a happy loving face on the OLED display
    if (xQueueReceive(petQueue, &pettedFace, 0) == pdTRUE) {
      // Blocks forever until xBinarySemaphore is free (protects I2C bus)
      if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) == pdTRUE) {
        u8g2.clearBuffer(); // Clear OLED to prepare the display for the happy face 
        u8g2.setFont(u8g2_font_unifont_t_symbols);
        u8g2.drawGlyph(40, 16, 0x25e0); // Draw happy left eye
        u8g2.drawGlyph(88, 16, 0x25e0); // Draw happy right eye
        u8g2.drawGlyph(64, 42, 0x25e1); // Draw smile
        u8g2.drawGlyph(13, 30, 0x2665); // Draw left heart
        u8g2.drawGlyph(110, 30, 0x2665); // Draw right heart
        u8g2.sendBuffer(); // Send drawings to OLED for display

        xSemaphoreGive(xBinarySemaphore);
      }
    } else {
      // If nothing is recieved from the queue, petting has not been detected, so display a calm resting face
      // Blocks forever until xBinarySemaphore is free (protects I2C bus)
      if (xSemaphoreTake(xBinarySemaphore, portMAX_DELAY) == pdTRUE) {
        u8g2.clearBuffer(); // Clear OLED to prepare the display for the happy face 
        u8g2.drawFilledEllipse(40, 16, 5, 7, U8G2_DRAW_ALL); // Draw calm left eye
        u8g2.drawFilledEllipse(88, 16, 5, 7, U8G2_DRAW_ALL); // Draw calm right eye
        u8g2.setFont(u8g2_font_unifont_t_symbols);
        u8g2.drawGlyph(64, 42, 0x25e1); // Draw smile
        u8g2.sendBuffer(); // Send drawings to OLED for display

        xSemaphoreGive(xBinarySemaphore);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(PET_DELAY_MS));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Setup OLED display
  u8g2.begin();

  delay(100);

  // Setup servo motor (connected to fan)
  servo.attach(SERVO_PIN, 500, 2400);
  Wire.begin(SDA_PIN, SCL_PIN);

  // Setup HC-SR04 ultrasonic sensor
  pinMode(TRIG_PIN, OUTPUT); // Sets the trigPin as an Output
  pinMode(ECHO_PIN, INPUT);  // Sets the echoPin as an Input
  pinMode(LED_PIN, OUTPUT);  // Sets the ledPin as an Output

  // Create and start periodic timer for the petting that fires every TOGGLE_INTERVAL_US_PET (50 Hz Freq)
  const esp_timer_create_args_t timer_args_pet = {
        .callback = &onTimerPet,
        .name = "periodic_timer pet"
  };
  esp_timer_create(&timer_args_pet, &periodic_timer_pet);
  esp_timer_start_periodic(periodic_timer_pet, TOGGLE_INTERVAL_US_PET);

  // Create and start periodic timer for the petting that fires every TOGGLE_INTERVAL_US_FAN (32 Hz Freq)
  const esp_timer_create_args_t timer_args_fan = {
        .callback = &onTimerFan,
        .name = "periodic_timer fan"
  };
  esp_timer_create(&timer_args_fan, &periodic_timer_fan);
  esp_timer_start_periodic(periodic_timer_fan, TOGGLE_INTERVAL_US_FAN);

  // Initialize AM2320 temperature sensor
  // Prints a message to serial output telling the user if temperature sensor initialization was successful or not
  if (!am2320.begin()) {
    Serial.println("WARNING: AM2320 not found, continuing anyway...");
  } else {
    Serial.println("AM2320 ready");
  }

  // Create petQueue, which holds 1 bool value
  petQueue  = xQueueCreate(1, sizeof(bool));
  // Create tempQueue, which holds 1 float value
  tempQueue = xQueueCreate(1,  sizeof(float));

  // Prints a warning to serial output if either queue initialization failed
  if (!petQueue || !tempQueue) {
    Serial.println("[Core1] ERROR: Queue creation failed!");
    while (true);  // Halt program if either failed
  }

  // Create binary semaphore for I2C bus
  xBinarySemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(xBinarySemaphore);

  // Prints a warning to serial output if binary semaphore initialization failed
  if (xBinarySemaphore == NULL) {
    Serial.println("Binary Semaphore did not successfully initialize");
  }

  // Create two input tasks and pin to core 1
  xTaskCreatePinnedToCore(motionTask, "MotionTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(temperatureTask, "TempTask", 4096, NULL, 1, NULL, 1);
  // Create two output tasks and pin to core 0
  xTaskCreatePinnedToCore(faceDisplayTask, "Face Display Task", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(fanTask, "FanTask", 4096, NULL, 1, NULL, 0);

  Serial.println("[Core1] Setup complete.");
}

void loop() {
}
