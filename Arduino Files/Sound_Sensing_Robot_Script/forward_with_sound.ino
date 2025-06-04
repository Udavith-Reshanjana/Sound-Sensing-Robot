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

#define MAX_MOTOR_SPEED 255
const int PWMFreq = 1000; // 1 KHz
const int PWMResolution = 8;
const int rightMotorPWMSpeedChannel = 4;
const int leftMotorPWMSpeedChannel = 5;

void setup() {
Serial.begin(115200);
pinMode(analogPin, INPUT);

// Set motor control pins as OUTPUT
pinMode(rightMotorPin1, OUTPUT);
pinMode(rightMotorPin2, OUTPUT);
pinMode(leftMotorPin1, OUTPUT);
pinMode(leftMotorPin2, OUTPUT);

// Initialize PWM channels for motor speed control
ledcSetup(rightMotorPWMSpeedChannel, PWMFreq, PWMResolution);
ledcSetup(leftMotorPWMSpeedChannel, PWMFreq, PWMResolution);

// Attach PWM channels to enable pins
ledcAttachPin(enableRightMotor, rightMotorPWMSpeedChannel);
ledcAttachPin(enableLeftMotor, leftMotorPWMSpeedChannel);
}

void rotateMotor(int rightMotorSpeed, int leftMotorSpeed) {
// Control right motor direction
digitalWrite(rightMotorPin1, rightMotorSpeed > 0);
digitalWrite(rightMotorPin2, rightMotorSpeed < 0);

// Control left motor direction
digitalWrite(leftMotorPin1, leftMotorSpeed > 0);
digitalWrite(leftMotorPin2, leftMotorSpeed < 0);

// Apply PWM for speed control
ledcWrite(rightMotorPWMSpeedChannel, abs(rightMotorSpeed));
ledcWrite(leftMotorPWMSpeedChannel, abs(leftMotorSpeed));
}

// Movement functions
void moveForward() {
rotateMotor(MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
}

void stopMotors() {
rotateMotor(0, 0);
}

void loop() {
const int sampleCount = 500; // 20 samples x 50ms = ~1 second
long sum = 0;

for (int i = 0; i < sampleCount; i++) {
int minVal = 4095;
int maxVal = 0;

unsigned long start = millis();
while (millis() - start < 1) {
int val = analogRead(analogPin);
if (val < minVal) minVal = val;
if (val > maxVal) maxVal = val;
}

int peakToPeak = maxVal - minVal;
int amplified = peakToPeak * 100;
sum += amplified;
}

float avg = (float)sum / sampleCount;
Serial.print("Avg1s:\t");
if ((avg - 1500) > 0) {
Serial.println((avg - 1000) * 10);
moveForward(); // Move vehicle forward when sound detected
} else {
Serial.println(0);
stopMotors(); // Stop vehicle when no sound detected
}
}

