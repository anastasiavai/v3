// DIY Centrifuge Controller
// Controls motor + monitors accelerometer for safety
// (RIP encoder :,,,,((((( you were missed ))))))

#include <Wire.h>
#include <MPU6050_light.h>

// Hardware setup
#define PIN_PWM         5
#define PIN_BUTTON      8

// Motor limits to prevent stalling or overheating 
#define MOTOR_MIN       40
#define MOTOR_MAX       230

// Safety thresholds
#define MAX_VIB_X       0.5   // g-force limit
#define MAX_VIB_Y       0.5

// Timing
#define DEBOUNCE_TIME   50
#define UPDATE_RATE     500  // Send data twice every second

// Sensor stuff
MPU6050 mpu(Wire);
bool sensorWorking = false;
float peakX = 0, peakY = 0, peakZ = 0;
float currentX = 0, currentY = 0, currentZ = 0;

// States
enum State { IDLE, RUNNING, DONE };
State state = IDLE;

// Run parameters
uint8_t pwmValue = 0;
uint32_t duration = 0;
uint32_t startTime = 0;
uint32_t lastUpdate = 0;

// Button variables
uint32_t buttonTimer = 0;
bool lastButton = HIGH;
bool buttonActive = false;
bool buttonReady = true;

void setup() {
    Serial.begin(9600);
    delay(1000);
    
    pinMode(PIN_PWM, OUTPUT);
    pinMode(PIN_BUTTON, INPUT);
    analogWrite(PIN_PWM, 0);
    
    Serial.println(F("CENTRIFUGE v2.2"));
    
    // Try to start sensor
    Wire.begin();
    if (mpu.begin() == 0) {
        Serial.println(F(" MPU6050 found, calibrating..."));
        mpu.calcOffsets();
        sensorWorking = true;
        Serial.print(F("Safety limits: ±"));
        Serial.print(MAX_VIB_X);
        Serial.println(F("g\n"));
    } else {
        Serial.println(F("No sensor - continuing anyway"));
    }
    
    Serial.println(F("IDLE"));
}

void loop() {
    checkButton();
    
    if (state == IDLE) {
        if (buttonActive) startRun();
    }
    
    else if (state == RUNNING) {
        if (buttonActive) {
            Serial.println(F("Emergency stop!"));
            stopRun(true);
            return;
        }
        
        // Check timer
        if (millis() - startTime >= duration) {
            stopRun(false);
            return;
        }
        
        // Update sensor & send data every second
        if (millis() - lastUpdate >= UPDATE_RATE) {
            lastUpdate = millis();
            
            if (sensorWorking) {
                mpu.update();
                currentX = mpu.getAccX();
                currentY = mpu.getAccY();
                currentZ = mpu.getAccZ();
                
                // Safety check
                if (abs(currentX) > MAX_VIB_X || abs(currentY) > MAX_VIB_Y) {
                    Serial.println(F("VIBRATION TOO HIGH!"));
                    stopRun(true);
                    return;
                }
                
                // Track peaks
                if (abs(currentX) > abs(peakX)) peakX = currentX;
                if (abs(currentY) > abs(peakY)) peakY = currentY;
                if (abs(currentZ) > abs(peakZ)) peakZ = currentZ;
            }
            
            // Send status to GUI
            uint32_t remaining = (duration - (millis() - startTime)) / 1000;
            Serial.print(F("[RUN] "));
            Serial.print(remaining);
            Serial.print(F("s | "));
            
            if (sensorWorking) {
                Serial.print(F("gX:"));
                Serial.print(currentX, 2);
                Serial.print(F("g gY:"));
                Serial.print(currentY, 2);
                Serial.print(F("g gZ:"));
                Serial.print(currentZ, 2);
                Serial.print(F("g"));
            } else {
                Serial.print(F("gX:--- gY:--- gZ:---"));
            }
            
            Serial.print(F(" | PWM:"));
            Serial.print(pwmValue);
            Serial.println(F("/255"));
            
            // Peak report every 5 seconds
            static uint8_t counter = 0;
            if (++counter >= 5) {
                Serial.print(F("Peaks: X="));
                Serial.print(peakX, 2);
                Serial.print(F("g Y="));
                Serial.print(peakY, 2);
                Serial.print(F("g Z="));
                Serial.print(peakZ, 2);
                Serial.println(F("g"));
                counter = 0;
            }
        }
    }
}

void checkButton() {
    buttonActive = false;
    bool pressed = (digitalRead(PIN_BUTTON) == LOW);
    
    if (pressed && !lastButton) {
        buttonTimer = millis();
    }
    
    if (pressed && buttonReady && (millis() - buttonTimer >= DEBOUNCE_TIME)) {
        buttonActive = true;
        buttonReady = false;
    }
    
    if (!pressed) buttonReady = true;
    lastButton = pressed;
}

void startRun() {
    // Get parameters from GUI
    Serial.println(F("PWM duty cycle (0-100%):"));
    Serial.flush();
    
    unsigned long timeout = millis() + 30000;
    while (Serial.available() == 0 && millis() < timeout) delay(10);
    
    if (millis() >= timeout) {
        pwmValue = 127;
        duration = 10000;
        Serial.println(F("Timeout - using defaults"));
    } else {
        float duty = Serial.parseFloat();
        while (Serial.available()) Serial.read();
        
        Serial.println(F("Duration (seconds):"));
        Serial.flush();
        
        timeout = millis() + 30000;
        while (Serial.available() == 0 && millis() < timeout) delay(10);
        
        int secs = Serial.parseInt();
        while (Serial.available()) Serial.read();
        
        // Validate
        duty = constrain(duty, 0, 100);
        secs = constrain(secs, 1, 600);
        
        pwmValue = map(duty * 10, 0, 1000, 0, 255);
        pwmValue = constrain(pwmValue, MOTOR_MIN, MOTOR_MAX);
        duration = secs * 1000UL;
        
        Serial.print(F("Set: PWM="));
        Serial.print(pwmValue);
        Serial.print(F("/255, Time="));
        Serial.print(secs);
        Serial.println(F("s"));
    }
    
    // Start motor
    analogWrite(PIN_PWM, pwmValue);
    startTime = millis();
    lastUpdate = millis() - UPDATE_RATE;
    peakX = peakY = peakZ = 0;
    state = RUNNING;
    
    Serial.println(F("RUNNING"));
}

void stopRun(bool emergency) {
    analogWrite(PIN_PWM, 0);
    state = DONE;
    
    Serial.println(emergency ? F("EMERGENCY STOP") : F("COMPLETE"));
    
    Serial.print(F("Peak forces: X="));
    Serial.print(peakX, 2);
    Serial.print(F("g Y="));
    Serial.print(peakY, 2);
    Serial.print(F("g Z="));
    Serial.print(peakZ, 2);
    Serial.println(F("g"));
    
    delay(3000);
    state = IDLE;
    Serial.println(F("IDLE"));
}