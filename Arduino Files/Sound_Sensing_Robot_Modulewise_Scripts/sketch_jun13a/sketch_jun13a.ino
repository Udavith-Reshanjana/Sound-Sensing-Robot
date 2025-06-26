// Sound detection pin
int analogPin = 35;

// Right motor pins
const int enableRightMotor = 5;
const int rightMotorPin1 = 19;
const int rightMotorPin2 = 18;

// Left motor pins
const int enableLeftMotor = 23;
const int leftMotorPin1 = 22;
const int leftMotorPin2 = 21;

// Ultrasonic pins
#define echoPin 26
#define trigPin 25

#define MAX_MOTOR_SPEED 255

void setup() {
  Serial.begin(115200);
  pinMode(analogPin, INPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  pinMode(rightMotorPin1, OUTPUT);
  pinMode(rightMotorPin2, OUTPUT);
  pinMode(leftMotorPin1, OUTPUT);
  pinMode(leftMotorPin2, OUTPUT);
  pinMode(enableRightMotor, OUTPUT);
  pinMode(enableLeftMotor, OUTPUT);

  Serial.println("Robot Ready...");
}

void rotateMotor(int rightMotorSpeed, int leftMotorSpeed) {
  digitalWrite(rightMotorPin1, rightMotorSpeed > 0);
  digitalWrite(rightMotorPin2, rightMotorSpeed < 0);
  digitalWrite(leftMotorPin1, leftMotorSpeed > 0);
  digitalWrite(leftMotorPin2, leftMotorSpeed < 0);

  analogWrite(enableRightMotor, abs(rightMotorSpeed));
  analogWrite(enableLeftMotor, abs(leftMotorSpeed));
}

void stopMotors() {
  rotateMotor(0, 0);
}

void turnLeft(int speed) {
  rotateMotor(-speed, speed);
}

void turnRight(int speed) {
  rotateMotor(speed, -speed);
}

void moveBackward(int speed) {
  rotateMotor(-speed, -speed);
}

int getSoundLevel(float avg) {
  if (avg <= 700) return 1;
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
    case 2: return 15;
    case 3: return 35;
    case 4: return 50;
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
  int level = getSoundLevel(avg);
  int speed = getSpeedByLevel(level);
  int radius = getObstacleRadiusByLevel(level);
  int distance = readDistance();

  Serial.print("Level: "); Serial.print(level);
  Serial.print(" | Speed: "); Serial.print(speed);
  Serial.print(" | Distance: "); Serial.print(distance);
  Serial.print(" | Radius: "); Serial.println(radius);

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
