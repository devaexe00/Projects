#include <Arduino.h>

// --- Configuration ---
const int PIN_MOTOR = 26;        // Pin connected to MOSFET Gate
const int PIN_TOUCH = 32;        // Pin connected to Touch Pad
const int VIBRATION_DURATION = 150; // Motor runs for 150ms
const int TOUCH_THRESHOLD = 40;  // Threshold: Value drops when touched (Adjust after testing)
const unsigned long DEBOUNCE_DELAY = 200; // Prevent double triggers

// --- State Variables ---
unsigned long motorStartTime = 0;
unsigned long lastTouchTime = 0;
bool isMotorRunning = false;

void setup() {
  Serial.begin(115200);

  // Initialize Motor Pin
  pinMode(PIN_MOTOR, OUTPUT);
  digitalWrite(PIN_MOTOR, LOW); // Ensure motor is off at startup

  // Allow touch sensor to settle
  delay(100); 
  Serial.println("System Ready. Touch the pad.");
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. READ SENSORS
  // touchRead() returns a lower value when touched (e.g., < 40)
  int touchValue = touchRead(PIN_TOUCH); 

  // 2. CHECK FOR TOUCH TRIGGER
  if (touchValue < TOUCH_THRESHOLD) {
    // Check debounce timer to avoid rapid re-triggering
    if (currentMillis - lastTouchTime > DEBOUNCE_DELAY) {
      Serial.printf("Touch Detected! Value: %d\n", touchValue);
      
      // Turn Motor ON
      digitalWrite(PIN_MOTOR, HIGH);
      isMotorRunning = true;
      motorStartTime = currentMillis;
      lastTouchTime = currentMillis;
    }
  }

  // 3. HANDLE MOTOR TIMING (Non-blocking)
  if (isMotorRunning) {
    // Check if the vibration duration has passed
    if (currentMillis - motorStartTime >= VIBRATION_DURATION) {
      digitalWrite(PIN_MOTOR, LOW); // Turn Motor OFF
      isMotorRunning = false;
      Serial.println("Motor OFF");
    }
  }

  // You can do other tasks here (e.g., update WiFi, blink LEDs)
  // because we are not using delay()!
}