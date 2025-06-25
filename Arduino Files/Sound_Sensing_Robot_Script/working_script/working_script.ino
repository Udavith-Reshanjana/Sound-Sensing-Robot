// Sound detection pin
int analogPin = 35;

// Motor pins
const int enableRightMotor = 23;
const int rightMotorPin1 = 21;
const int rightMotorPin2 = 22;

const int enableLeftMotor = 5;
const int leftMotorPin1 = 18;
const int leftMotorPin2 = 19;

// Ultrasonic sensor
#define echoPin 32
#define trigPin 33

#define MAX_MOTOR_SPEED 255
const int PWMFreq = 1000;
const int PWMResolution = 8;
const int rightMotorPWMSpeedChannel = 4;
const int leftMotorPWMSpeedChannel = 5;

// —— Adaptive Noise-Floor Tracking parameters ——
float ambient = 0.0;
const float AMB_ALPHA      = 0.995;   // how fast ambient adapts (closer to 1 → slower)
const float THRESHOLD_DELTA = 150.0;  // minimum margin above ambient to count as "external"

// Update running ambient noise floor
void updateAmbient(float level) {
  ambient = AMB_ALPHA * ambient + (1.0 - AMB_ALPHA) * level;
}

// Return true if current level is a real external event
bool isExternalSound(float level) {
  return (level - ambient) > THRESHOLD_DELTA;
}

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

  // Optional: do a quick calibration of ambient with motors off
  delay(500);
  const int calSamples = 200;
  long sumCal = 0;
  for (int i = 0; i < calSamples; i++) {
    int val = analogRead(analogPin);
    sumCal += val;
    delay(2);
  }
  ambient = (float)sumCal / calSamples;
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
  if (avg <= 850)  return 1;
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

void loop() {
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

  float avg = (float)sum / sampleCount;

  // —— Adaptive noise-floor logic ——
  bool external = isExternalSound(avg);
  if (!external) {
    updateAmbient(avg);
  }

  int level = external ? getSoundLevel(avg) : 1;
  int speed  = getSpeedByLevel(level);
  int radius = getObstacleRadiusByLevel(level);
  int distance = readDistance();

  Serial.print("Avg: ");     Serial.print(avg);
  Serial.print(" | Ambient: "); Serial.print(ambient, 1);
  Serial.print(" | Ext? ");   Serial.print(external);
  Serial.print(" | Level: "); Serial.print(level-1);
  Serial.print(" | Speed: "); Serial.print(speed);
  Serial.print(" | Dist: ");  Serial.print(distance);
  Serial.print(" | Radius: ");Serial.println(radius);

  if (level >= 2) {
    if (distance > 0 && distance <= radius) {
      Serial.println("Obstacle detected: trying to avoid...");
      int attempts = 0;
      const int maxAttempts = 5;
      const int turnDelay = 300;

      while (attempts < maxAttempts) {
        turnLeft(speed);
        delay(turnDelay);
        distance = readDistance();
        if (distance > radius || distance <= 0) break;
        attempts++;
      }

      if (attempts >= maxAttempts) {
        Serial.println("Left blocked. Reversing and turning right.");
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