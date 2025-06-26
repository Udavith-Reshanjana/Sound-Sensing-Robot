#include <WiFi.h>
#include <WebServer.h>

// ======== SOUND SENSOR & ROBOT CONTROL SETUP ========
int analogPin = 35;
const int enableRightMotor = 23, rightMotorPin1 = 21, rightMotorPin2 = 22;
const int enableLeftMotor = 5, leftMotorPin1 = 18, leftMotorPin2 = 19;
#define echoPin 32
#define trigPin 33

#define MAX_MOTOR_SPEED 255
const int PWMFreq = 1000;
const int PWMResolution = 8;
const int rightMotorPWMSpeedChannel = 4;
const int leftMotorPWMSpeedChannel = 5;

// Noise-floor tracking
float ambient = 0.0;
const float AMB_ALPHA = 0.995;
const float THRESHOLD_DELTA = 150.0;

float avg = 0.0;
int level = 1;

// ======== WIFI AND SERVER SETUP ========
const char* ssid = "SoundBot-AP";
const char* password = "12345678";
WebServer server(80);

// ======== FUNCTION DEFINITIONS ========
void updateAmbient(float level) {
  ambient = AMB_ALPHA * ambient + (1.0 - AMB_ALPHA) * level;
}

bool isExternalSound(float level) {
  return (level - ambient) > THRESHOLD_DELTA;
}

void rotateMotor(int rightMotorSpeed, int leftMotorSpeed) {
  digitalWrite(rightMotorPin1, rightMotorSpeed > 0);
  digitalWrite(rightMotorPin2, rightMotorSpeed < 0);
  digitalWrite(leftMotorPin1, leftMotorSpeed > 0);
  digitalWrite(leftMotorPin2, leftMotorSpeed < 0);

  ledcWrite(rightMotorPWMSpeedChannel, abs(rightMotorSpeed));
  ledcWrite(leftMotorPWMSpeedChannel, abs(leftMotorSpeed));
}

void stopMotors() {
  rotateMotor(0, 0);
}

void turnLeft(int speed) {
  rotateMotor(50, -50);
}

void turnRight(int speed) {
  rotateMotor(-50, 50);
}

void moveBackward(int speed) {
  rotateMotor(-speed, -speed);
}

int getSoundLevel(float avg) {
  if (avg <= 850) return 1;
  else if (avg <= 1400) return 2;
  else if (avg <= 2100) return 3;
  else return 4;
}

int getSpeedByLevel(int level) {
  switch (level) {
    case 2: return 100;
    case 3: return 180;
    case 4: return 255;
    default: return 0;
  }
}

int getObstacleRadiusByLevel(int level) {
  switch (level) {
    case 2: return 30;
    case 3: return 60;
    case 4: return 90;
    default: return 0;
  }
}

int readDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  return duration / 58.2;
}

// ======== SETUP ========
void setup() {
  Serial.begin(115200);
  pinMode(analogPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(rightMotorPin1, OUTPUT);
  pinMode(rightMotorPin2, OUTPUT);
  pinMode(leftMotorPin1, OUTPUT);
  pinMode(leftMotorPin2, OUTPUT);

  ledcSetup(rightMotorPWMSpeedChannel, PWMFreq, PWMResolution);
  ledcSetup(leftMotorPWMSpeedChannel, PWMFreq, PWMResolution);
  ledcAttachPin(enableRightMotor, rightMotorPWMSpeedChannel);
  ledcAttachPin(enableLeftMotor, leftMotorPWMSpeedChannel);

  // Initial ambient calibration
  delay(500);
  long sumCal = 0;
  for (int i = 0; i < 200; i++) {
    sumCal += analogRead(analogPin);
    delay(2);
  }
  ambient = (float)sumCal / 200;

  // Wi-Fi AP mode (non-blocking)
  WiFi.softAP(ssid, password);
  Serial.print("WiFi started. IP: ");
  Serial.println(WiFi.softAPIP());

  // API endpoint
  server.on("/status", []() {
    String json = "{";
    json += "\"avg\":" + String(avg, 2) + ",";
    json += "\"level\":" + String(level);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();
}

// ======== MAIN LOOP ========
void loop() {
  server.handleClient();  // Handle incoming HTTP requests

  const int sampleCount = 500;
  long sum = 0;

  for (int i = 0; i < sampleCount; i++) {
    int minVal = 4095, maxVal = 0;
    unsigned long start = millis();
    while (millis() - start < 1) {
      int val = analogRead(analogPin);
      if (val < minVal) minVal = val;
      if (val > maxVal) maxVal = val;
    }
    int peakToPeak = maxVal - minVal;
    sum += (peakToPeak * 100);
  }

  avg = (float)sum / sampleCount;

  bool external = isExternalSound(avg);
  if (!external) updateAmbient(avg);

  level = external ? getSoundLevel(avg) : 1;
  int speed = getSpeedByLevel(level);
  int radius = getObstacleRadiusByLevel(level);
  int distance = readDistance();

  Serial.print("Avg: "); Serial.print(avg);
  Serial.print(" | Ambient: "); Serial.print(ambient, 1);
  Serial.print(" | Level: "); Serial.print(level);
  Serial.print(" | Speed: "); Serial.print(speed);
  Serial.print(" | Distance: "); Serial.println(distance);

  if (level >= 2) {
    if (distance > 0 && distance <= radius) {
      int attempts = 0;
      while (attempts < 5) {
        turnLeft(speed);
        delay(300);
        distance = readDistance();
        if (distance > radius || distance <= 0) break;
        attempts++;
      }
      if (attempts >= 5) {
        moveBackward(speed);
        delay(400);
        turnRight(speed);
        delay(500);
      }
      stopMotors();
    } else {
      rotateMotor(speed, speed);
    }
  } else {
    stopMotors();
  }

  delay(100);
}