int motor1Pin = 9;  // First motor control
int motor2Pin = 10; // Second motor control

void setup() { 
    Serial.begin(115200);
    pinMode(motor1Pin, OUTPUT);
    pinMode(motor2Pin, OUTPUT);
}

// Move both motors forward
void moveForward() {
    digitalWrite(motor1Pin, HIGH);
    digitalWrite(motor2Pin, HIGH);
}

// Move both motors backward
void moveBackward() {
    digitalWrite(motor1Pin, LOW);
    digitalWrite(motor2Pin, LOW);
}

// Turn left (one motor moves)
void turnLeft() {
    digitalWrite(motor1Pin, LOW);
    digitalWrite(motor2Pin, HIGH);
}

// Turn right (other motor moves)
void turnRight() {
    digitalWrite(motor1Pin, HIGH);
    digitalWrite(motor2Pin, LOW);
}

// Stop both motors
void stopMotors() {
    digitalWrite(motor1Pin, LOW);
    digitalWrite(motor2Pin, LOW);
}

