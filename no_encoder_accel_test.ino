// =============================================================================
//  CENTRIFUGE CONTROL - WITH MPU6050 ACCELEROMETER
//  Works with Python GUI - receives PWM/Duration via serial
// =============================================================================

#include <Wire.h>
#include <MPU6050_light.h>

// PINS
#define PIN_PWM         5
#define PIN_BUTTON      8

// PARAMETERS
const uint8_t  MOTOR_MIN_DUTY       = 40;
const uint8_t  MOTOR_MAX_DUTY       = 230;
const uint32_t DEBOUNCE_MS          = 50UL;
const uint32_t TELEMETRY_MS         = 1000UL;

// VIBRATION SAFETY LIMITS
const float MAX_VIBRATION_X = 1.5;  // Max g-force on X axis before emergency stop
const float MAX_VIBRATION_Y = 1.5;  // Max g-force on Y axis before emergency stop

// ACCELEROMETER OFFSETS (from calibration - adjust these!)
const float ACCEL_OFFSET_X = 0.0f;  // Replace with your calibrated value
const float ACCEL_OFFSET_Y = 0.0f;  // Replace with your calibrated value
const float ACCEL_OFFSET_Z = 0.0f;  // Replace with your calibrated value

// ACCELEROMETER
MPU6050 mpu(Wire);
bool accelAvailable = false;
float peakGX = 0.0, peakGY = 0.0, peakGZ = 0.0;
float lastGX = 0.0, lastGY = 0.0, lastGZ = 0.0;

// STATE MACHINE
enum SystemState { IDLE, RUNNING, COMPLETE };
SystemState systemState = IDLE;

// VARIABLES
uint8_t  targetDuty = 0;
uint32_t runDurationMs = 0, runStartMs = 0, buttonDownMs = 0;
uint32_t lastTelemetryMs = 0;
bool lastButtonRaw = HIGH, buttonPressed = false, buttonArmed = true;

// SERIAL INPUT
void collectRunParameters() {
    // Clear buffer
    while (Serial.available() > 0) Serial.read();
    
    Serial.println(F("PWM duty cycle (0-100%):"));
    Serial.flush();
    
    unsigned long timeout = millis() + 30000;
    while (Serial.available() == 0) {
        if (millis() > timeout) {
            Serial.println(F("Timeout! Using defaults"));
            targetDuty = 127;
            runDurationMs = 10000;
            return;
        }
        delay(10);
    }
    
    float duty = Serial.parseFloat();
    while (Serial.available() > 0) Serial.read();
    
    Serial.println(F("Duration (seconds):"));
    Serial.flush();
    
    timeout = millis() + 30000;
    while (Serial.available() == 0) {
        if (millis() > timeout) {
            Serial.println(F("Timeout! Using defaults"));
            runDurationMs = 10000;
            return;
        }
        delay(10);
    }
    
    long secs = Serial.parseInt();
    while (Serial.available() > 0) Serial.read();
    
    // Validation
    if (duty < 0) duty = 0;
    if (duty > 100) duty = 100;
    if (secs < 1) secs = 1;
    if (secs > 600) secs = 600;
    
    float d = (duty / 100.0f) * 255.0f;
    if (d < MOTOR_MIN_DUTY) d = MOTOR_MIN_DUTY;
    if (d > MOTOR_MAX_DUTY) d = MOTOR_MAX_DUTY;
    targetDuty = (uint8_t)d;
    runDurationMs = secs * 1000UL;
    
    Serial.print(F("Configured: PWM=")); 
    Serial.print(targetDuty); 
    Serial.print(F("/255, Duration="));
    Serial.print(secs); 
    Serial.println(F("s"));
    Serial.flush();
}

// MOTOR
void stopMotor() { 
    analogWrite(PIN_PWM, 0); 
}

void startMotor() { 
    analogWrite(PIN_PWM, targetDuty); 
}

// BUTTON
void updateButton() {
    buttonPressed = false;
    bool raw = (digitalRead(PIN_BUTTON) == LOW);
    
    if (raw && !lastButtonRaw) {
        buttonDownMs = millis();
    }
    
    if (raw && buttonArmed && (millis() - buttonDownMs) >= DEBOUNCE_MS) {
        buttonPressed = true;
        buttonArmed = false;
    }
    
    if (!raw) {
        buttonArmed = true;
    }
    
    lastButtonRaw = raw;
}

// STATES
void enterIdle() {
    stopMotor();
    systemState = IDLE;
    peakGX = 0.0;
    peakGY = 0.0;
    peakGZ = 0.0;
    
    Serial.println(F("IDLE"));
    Serial.flush();
}

void enterRunning() {
    collectRunParameters();
    startMotor();
    
    runStartMs = millis();
    lastTelemetryMs = millis() - TELEMETRY_MS;
    systemState = RUNNING;
    
    Serial.println(F("RUNNING"));
    Serial.flush();
}

void enterComplete(bool emergency) {
    stopMotor();
    systemState = COMPLETE;
    
    if (emergency) {
        Serial.println(F("EMERGENCY STOP"));
    } else {
        Serial.println(F("COMPLETE"));
    }
    
    // Print final peak values
    Serial.print(F("Peak G-forces: X="));
    Serial.print(peakGX, 2);
    Serial.print(F("g, Y="));
    Serial.print(peakGY, 2);
    Serial.print(F("g, Z="));
    Serial.print(peakGZ, 2);
    Serial.println(F("g"));
    
    Serial.flush();
    
    delay(3000);
    enterIdle();
}

// SETUP
void setup() {
    Serial.begin(9600);
    delay(1000);
    
    pinMode(PIN_PWM, OUTPUT);
    pinMode(PIN_BUTTON, INPUT);
    analogWrite(PIN_PWM, 0);
    
    Serial.println(F("\n================================="));
    Serial.println(F("  CENTRIFUGE v2.2 - WITH MPU6050"));
    Serial.println(F("=================================\n"));
    
    // Initialize accelerometer
    Wire.begin();
    byte status = mpu.begin();
    
    if (status != 0) {
        Serial.println(F("MPU6050 not found! Continuing without..."));
        accelAvailable = false;
    } else {
        Serial.println(F("MPU6050 found! Calibrating..."));
        mpu.calcOffsets();  // Calibrate - keep sensor still!
        Serial.println(F("MPU6050 initialized and calibrated"));
        accelAvailable = true;
    }
    
    Serial.print(F("Vibration limits: X=±"));
    Serial.print(MAX_VIBRATION_X);
    Serial.print(F("g, Y=±"));
    Serial.print(MAX_VIBRATION_Y);
    Serial.println(F("g\n"));
    
    enterIdle();
}

// MAIN LOOP
void loop() {
    uint32_t now = millis();
    
    // Always update button
    updateButton();

    if (systemState == IDLE) {
        if (buttonPressed) {
            enterRunning();
        }
    }

    else if (systemState == RUNNING) {
        
        // Emergency stop via button
        if (buttonPressed) {
            Serial.println(F("Emergency stop button pressed!"));
            enterComplete(true);
            return;
        }
        
        // Check if duration complete FIRST
        uint32_t elapsed = now - runStartMs;
        if (elapsed >= runDurationMs) {
            enterComplete(false);
            return;
        }
        
        // Update accelerometer readings (only if telemetry is due to avoid I2C overload)
        if (accelAvailable && (now - lastTelemetryMs) >= TELEMETRY_MS) {
            mpu.update();
            
            // Get acceleration in g's with offsets applied
            lastGX = mpu.getAccX() - ACCEL_OFFSET_X;
            lastGY = mpu.getAccY() - ACCEL_OFFSET_Y;
            lastGZ = mpu.getAccZ() - ACCEL_OFFSET_Z;
            
            // Check for excessive vibration on X or Y axis
            if (abs(lastGX) > MAX_VIBRATION_X) {
                Serial.print(F("\n⚠ EXCESSIVE X-AXIS VIBRATION: "));
                Serial.print(lastGX, 2);
                Serial.println(F("g"));
                enterComplete(true);
                return;
            }
            
            if (abs(lastGY) > MAX_VIBRATION_Y) {
                Serial.print(F("\n⚠ EXCESSIVE Y-AXIS VIBRATION: "));
                Serial.print(lastGY, 2);
                Serial.println(F("g"));
                enterComplete(true);
                return;
            }
            
            // Track peaks
            if (abs(lastGX) > abs(peakGX)) peakGX = lastGX;
            if (abs(lastGY) > abs(peakGY)) peakGY = lastGY;
            if (abs(lastGZ) > abs(peakGZ)) peakGZ = lastGZ;
        }
        
        // Send telemetry to GUI
        if ((now - lastTelemetryMs) >= TELEMETRY_MS) {
            lastTelemetryMs = now;
            uint32_t remaining = (runDurationMs - elapsed) / 1000UL;
            
            Serial.print(F("[RUN] ")); 
            Serial.print(remaining); 
            Serial.print(F("s | "));
            
            // Display current accelerometer readings
            if (accelAvailable) {
                Serial.print(F("gX:"));
                Serial.print(lastGX, 2);
                Serial.print(F("g gY:"));
                Serial.print(lastGY, 2);
                Serial.print(F("g gZ:"));
                Serial.print(lastGZ, 2);
                Serial.print(F("g"));
            } else {
                Serial.print(F("gX:--- gY:--- gZ:---"));
            }
            
            Serial.print(F(" | PWM:"));
            Serial.print(targetDuty); 
            Serial.println(F("/255"));
            
            // Send peak values every 5 seconds
            static uint8_t counter = 0;
            counter++;
            if (counter >= 5) {
                Serial.print(F("Peak G-forces: X="));
                Serial.print(peakGX, 2);
                Serial.print(F("g, Y="));
                Serial.print(peakGY, 2);
                Serial.print(F("g, Z="));
                Serial.print(peakGZ, 2);
                Serial.println(F("g"));
                counter = 0;
            }
        }
    }
    
    else if (systemState == COMPLETE) {
        // Auto-return to IDLE handled in enterComplete()
    }
}