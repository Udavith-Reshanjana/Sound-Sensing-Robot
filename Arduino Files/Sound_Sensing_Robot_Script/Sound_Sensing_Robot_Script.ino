#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE
#include
const int enableRightMotor = 23;
const int rightMotorPin1 = 22;
const int rightMotorPin2 = 21;

// Left motor pins
const int enableLeftMotor = 5;
const int leftMotorPin1 = 19;
const int leftMotorPin2 = 18;

#define MAX_MOTOR_SPEED 255
const int PWMFreq = 1000; /* 1 KHz */
const int PWMResolution = 8;
const int rightMotorPWMSpeedChannel = 4;
const int leftMotorPWMSpeedChannel = 5;

void setup() {
// Initialize Bluetooth connection
Dabble.begin("BluetoothName"); // Change to your Bluetooth device name

// Set motor control pins as OUTPUT
pinMode(rightMotorPin1, OUTPUT);
pinMode(rightMotorPin2, OUTPUT);
pinMode(leftMotorPin1, OUTPUT);
pinMode(leftMotorPin2, OUTPUT);

// Initialize PWM channels for motor speed control
ledcSetup(rightMotorPWMSpeedChannel, PWMFreq, PWMResolution);
ledcSetup(leftMotorPWMSpeedChannel, PWMFreq, PWMResolution);

// Attach PWM channels to the motor pins (enabling speed control via PWM)
ledcAttachPin(enableRightMotor, rightMotorPWMSpeedChannel);
ledcAttachPin(enableLeftMotor, leftMotorPWMSpeedChannel);
}

void rotateMotor(int rightMotorSpeed, int leftMotorSpeed) {
  // Control the right motor direction
  if (rightMotorSpeed < 0) {
    digitalWrite(rightMotorPin1, LOW); // Set direction to backward
    digitalWrite(rightMotorPin2, HIGH);
    rightMotorSpeed = -rightMotorSpeed; // Make speed positive for PWM
  } else if (rightMotorSpeed > 0) {
    digitalWrite(rightMotorPin1, HIGH); // Set direction to forward
    digitalWrite(rightMotorPin2, LOW);
  } else {
    digitalWrite(rightMotorPin1, LOW); // Stop motor
    digitalWrite(rightMotorPin2, LOW);
  }
  
  // Control the left motor direction
  if (leftMotorSpeed < 0) {
    digitalWrite(leftMotorPin1, LOW); // Set direction to backward
    digitalWrite(leftMotorPin2, HIGH);
    leftMotorSpeed = -leftMotorSpeed; // Make speed positive for PWM
  } else if (leftMotorSpeed > 0) {
    digitalWrite(leftMotorPin1, HIGH); // Set direction to forward
    digitalWrite(leftMotorPin2, LOW);
  } else {
    digitalWrite(leftMotorPin1, LOW); // Stop motor
    digitalWrite(leftMotorPin2, LOW);
  }
  
  // Set PWM values to control speed
  ledcWrite(rightMotorPWMSpeedChannel, rightMotorSpeed);
  ledcWrite(leftMotorPWMSpeedChannel, leftMotorSpeed);
}

void loop() {
  int rightMotorSpeed = 0;
  int leftMotorSpeed = 0;
  Dabble.processInput();
  
  // Check GamePad inputs to set motor speed and direction
  if (GamePad.isUpPressed()) {
    rightMotorSpeed = MAX_MOTOR_SPEED;
    leftMotorSpeed = MAX_MOTOR_SPEED;
  }
  if (GamePad.isDownPressed()) {
    rightMotorSpeed = -MAX_MOTOR_SPEED;
    leftMotorSpeed = -MAX_MOTOR_SPEED;
  }
  if (GamePad.isLeftPressed()) {
    rightMotorSpeed = MAX_MOTOR_SPEED;
    leftMotorSpeed = -MAX_MOTOR_SPEED;
  }
  if (GamePad.isRightPressed()) {
    rightMotorSpeed = -MAX_MOTOR_SPEED;
    leftMotorSpeed = MAX_MOTOR_SPEED;
  }
  
  // Control motors with the specified speeds
  rotateMotor(rightMotorSpeed, leftMotorSpeed);
}
