#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

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
float ambient = 500.0;
const float AMB_ALPHA = 0.98;
const float THRESHOLD_DELTA = 100.0;

float avg = 0.0;
int level = 1;
int currentDistance = 0;

// Movement state tracking
enum RobotState {
  NORMAL_FORWARD,
  TURNING_LEFT,
  BACKING_UP,
  TURNING_RIGHT,
  STOPPED
};

RobotState robotState = STOPPED;
int turnAttempts = 0;
unsigned long stateChangeTime = 0;
const unsigned long TURN_DURATION = 250;
const unsigned long BACKUP_DURATION = 300;
const unsigned long RIGHT_TURN_DURATION = 400;

// ======== WIFI, SERVER AND MQTT SETUP ========
const char* ssid = "SoundBot-AP";
const char* password = "12345678";
WebServer server(80);

// MQTT Configuration
WiFiClient espClient;
PubSubClient mqtt(espClient);
const char* mqtt_broker = "192.168.4.1";
const int mqtt_port = 1883;
const char* status_topic = "soundbot/status";

// Connection management - FIXED: Non-blocking MQTT
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000;
unsigned long lastMQTTPublish = 0;
const unsigned long MQTT_PUBLISH_INTERVAL = 1000;
unsigned long lastMQTTAttempt = 0;
const unsigned long MQTT_RETRY_INTERVAL = 10000;  // Try MQTT every 10 seconds
bool mqttEnabled = true;  // Can be toggled via web interface
bool mqttConnected = false;
int mqttRetryCount = 0;
const int MAX_MQTT_RETRIES = 3;

// ======== FUNCTION DEFINITIONS ========
void updateAmbient(float level) {
  ambient = AMB_ALPHA * ambient + (1.0 - AMB_ALPHA) * level;
}

bool isExternalSound(float level) {
  return (level - ambient) > THRESHOLD_DELTA;
}

void rotateMotor(int rightMotorSpeed, int leftMotorSpeed) {
  rightMotorSpeed = constrain(rightMotorSpeed, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
  leftMotorSpeed = constrain(leftMotorSpeed, -MAX_MOTOR_SPEED, MAX_MOTOR_SPEED);
  
  digitalWrite(rightMotorPin1, rightMotorSpeed > 0);
  digitalWrite(rightMotorPin2, rightMotorSpeed < 0);
  digitalWrite(leftMotorPin1, leftMotorSpeed > 0);
  digitalWrite(leftMotorPin2, leftMotorSpeed < 0);

  ledcWrite(rightMotorPWMSpeedChannel, abs(rightMotorSpeed));
  ledcWrite(leftMotorPWMSpeedChannel, abs(leftMotorSpeed));
}

void stopMotors() {
  rotateMotor(0, 0);
  robotState = STOPPED;
}

void turnLeft(int speed) {
  rotateMotor(-speed/2, speed/2);
  robotState = TURNING_LEFT;
}

void turnRight(int speed) {
  rotateMotor(speed/2, -speed/2);
  robotState = TURNING_RIGHT;
}

void moveBackward(int speed) {
  rotateMotor(-speed, -speed);
  robotState = BACKING_UP;
}

void moveForward(int speed) {
  rotateMotor(speed, speed);
  robotState = NORMAL_FORWARD;
}

int getSoundLevel(float avg) {
  if (avg <= 600) return 1;
  else if (avg <= 1000) return 2;
  else if (avg <= 1600) return 3;
  else return 4;
}

int getSpeedByLevel(int level) {
  switch (level) {
    case 2: return 80;
    case 3: return 140;
    case 4: return 200;
    default: return 0;
  }
}

int getObstacleRadiusByLevel(int level) {
  switch (level) {
    case 2: return 25;
    case 3: return 45;
    case 4: return 70;
    default: return 0;
  }
}

int readDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000);
  int distance = duration / 58.2;
  return (distance > 0 && distance < 400) ? distance : 999;
}

void handleObstacleAvoidance(int speed, int radius) {
  unsigned long currentTime = millis();
  
  switch (robotState) {
    case NORMAL_FORWARD:
      if (currentDistance <= radius && currentDistance > 0) {
        turnLeft(speed);
        turnAttempts = 1;
        stateChangeTime = currentTime;
      } else {
        moveForward(speed);
      }
      break;
      
    case TURNING_LEFT:
      if (currentTime - stateChangeTime >= TURN_DURATION) {
        stopMotors();
        delay(50);
        currentDistance = readDistance();
        
        if (currentDistance > radius || currentDistance <= 0) {
          robotState = NORMAL_FORWARD;
          turnAttempts = 0;
        } else if (turnAttempts < 5) {
          turnLeft(speed);
          turnAttempts++;
          stateChangeTime = currentTime;
        } else {
          moveBackward(speed);
          stateChangeTime = currentTime;
          turnAttempts = 0;
        }
      }
      break;
      
    case BACKING_UP:
      if (currentTime - stateChangeTime >= BACKUP_DURATION) {
        turnRight(speed);
        stateChangeTime = currentTime;
      }
      break;
      
    case TURNING_RIGHT:
      if (currentTime - stateChangeTime >= RIGHT_TURN_DURATION) {
        robotState = NORMAL_FORWARD;
      }
      break;
      
    default:
      robotState = NORMAL_FORWARD;
      break;
  }
}

void checkWiFiConnection() {
  if (millis() - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
    lastWiFiCheck = millis();
    
    if (WiFi.getMode() != WIFI_AP) {
      Serial.println("WiFi AP mode lost - restarting AP");
      WiFi.softAP(ssid, password);
      delay(100);
    }
  }
}

// FIXED: Non-blocking MQTT connection
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("MQTT message received: ");
  Serial.println(message);
}

void tryMQTTConnection() {
  // Only try if MQTT is enabled and it's time to retry
  if (!mqttEnabled || mqtt.connected() || 
      (millis() - lastMQTTAttempt < MQTT_RETRY_INTERVAL)) {
    return;
  }
  
  lastMQTTAttempt = millis();
  
  Serial.print("Attempting MQTT connection...");
  
  // Set shorter timeout for connection attempt
  mqtt.setSocketTimeout(2);  // 2 second timeout
  
  if (mqtt.connect("SoundBot")) {
    Serial.println("connected");
    mqttConnected = true;
    mqttRetryCount = 0;
    mqtt.subscribe("soundbot/control");  // Optional control topic
  } else {
    mqttRetryCount++;
    Serial.print("failed, rc=");
    Serial.print(mqtt.state());
    Serial.print(" (attempt ");
    Serial.print(mqttRetryCount);
    Serial.println(")");
    
    // Disable MQTT temporarily after max retries
    if (mqttRetryCount >= MAX_MQTT_RETRIES) {
      Serial.println("Max MQTT retries reached. MQTT disabled for this session.");
      Serial.println("Robot will continue operating without MQTT.");
      mqttEnabled = false;  // Disable for this session
    }
    
    mqttConnected = false;
  }
}

void publishMQTTStatus() {
  // Only publish if MQTT is enabled, connected, and it's time to publish
  if (!mqttEnabled || !mqtt.connected() || 
      (millis() - lastMQTTPublish < MQTT_PUBLISH_INTERVAL)) {
    return;
  }
  
  lastMQTTPublish = millis();
  
  StaticJsonDocument<200> doc;
  doc["avg"] = avg;
  doc["level"] = level;
  doc["distance"] = currentDistance;
  doc["state"] = robotState;
  doc["ambient"] = ambient;
  doc["connected_stations"] = WiFi.softAPgetStationNum();
  doc["uptime"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  if (!mqtt.publish(status_topic, jsonString.c_str())) {
    Serial.println("MQTT publish failed");
    mqttConnected = false;
  }
}

// ======== SETUP ========
void setup() {
  Serial.begin(115200);
  Serial.println("Starting SoundBot...");
  Serial.println("Robot will work independently of MQTT/App connections");
  
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

  // Ambient calibration
  Serial.println("Calibrating ambient noise...");
  delay(1000);
  long sumCal = 0;
  int validReadings = 0;
  
  for (int i = 0; i < 100; i++) {
    int reading = analogRead(analogPin);
    if (reading > 0) {
      sumCal += reading;
      validReadings++;
    }
    delay(10);
  }
  
  if (validReadings > 0) {
    ambient = (float)sumCal / validReadings;
  }
  Serial.print("Initial ambient level: ");
  Serial.println(ambient);

  // WiFi AP setup
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(
    IPAddress(192, 168, 4, 1),
    IPAddress(192, 168, 4, 1),
    IPAddress(255, 255, 255, 0)
  );
  
  bool apStarted = WiFi.softAP(ssid, password, 1, false, 4);
  
  if (apStarted) {
    Serial.println("WiFi AP started successfully");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Users can connect to 'SoundBot-AP' network");
  } else {
    Serial.println("Failed to start WiFi AP - Robot will work in standalone mode");
  }

  // MQTT setup - non-blocking
  mqtt.setServer(mqtt_broker, mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(15);  // Shorter keepalive
  Serial.println("MQTT configured - will attempt connection when possible");

  // HTTP API endpoints
  server.on("/status", HTTP_GET, []() {
    StaticJsonDocument<400> doc;
    doc["avg"] = avg;
    doc["level"] = level;
    doc["distance"] = currentDistance;
    doc["ambient"] = ambient;
    doc["state"] = robotState;
    doc["uptime"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();
    doc["connected_stations"] = WiFi.softAPgetStationNum();
    doc["mqtt_enabled"] = mqttEnabled;
    doc["mqtt_connected"] = mqtt.connected();
    doc["wifi_ap_active"] = (WiFi.getMode() == WIFI_AP);
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", jsonString);
  });

  // FIXED: Add control endpoints
  server.on("/control", HTTP_POST, []() {
    if (server.hasArg("mqtt")) {
      String mqttControl = server.arg("mqtt");
      if (mqttControl == "enable") {
        mqttEnabled = true;
        mqttRetryCount = 0;
        Serial.println("MQTT enabled via web interface");
      } else if (mqttControl == "disable") {
        mqttEnabled = false;
        if (mqtt.connected()) {
          mqtt.disconnect();
        }
        Serial.println("MQTT disabled via web interface");
      }
    }
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // FIXED: Simple web interface with proper string handling
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>SoundBot Control</title>";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>body { font-family: Arial, sans-serif; margin: 20px; }";
    html += ".status { background: #f0f0f0; padding: 10px; margin: 10px 0; border-radius: 5px; }";
    html += ".button { background: #007cba; color: white; padding: 10px 20px; border: none; border-radius: 5px; margin: 5px; cursor: pointer; }";
    html += ".button:hover { background: #005a87; }";
    html += ".red { background: #dc3545; } .green { background: #28a745; }</style></head>";
    html += "<body><h1>SoundBot Control Panel</h1>";
    html += "<div class=\"status\" id=\"status\">Loading...</div>";
    html += "<button class=\"button\" onclick=\"toggleMQTT()\">Toggle MQTT</button>";
    html += "<button class=\"button\" onclick=\"refreshStatus()\">Refresh</button>";
    html += "<script>";
    html += "function refreshStatus() {";
    html += "fetch('/status').then(response => response.json()).then(data => {";
    html += "document.getElementById('status').innerHTML = ";
    html += "'<strong>Robot Status:</strong><br>' +";
    html += "'Sound Level: ' + data.level + '<br>' +";
    html += "'Distance: ' + data.distance + ' cm<br>' +";
    html += "'State: ' + data.state + '<br>' +";
    html += "'Connected Devices: ' + data.connected_stations + '<br>' +";
    html += "'MQTT: ' + (data.mqtt_connected ? 'Connected' : 'Disconnected') + '<br>' +";
    html += "'Uptime: ' + Math.floor(data.uptime / 1000) + ' seconds';";
    html += "}); }";
    html += "function toggleMQTT() {";
    html += "fetch('/control', {";
    html += "method: 'POST',";
    html += "headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
    html += "body: 'mqtt=' + (Math.random() > 0.5 ? 'enable' : 'disable')";
    html += "}).then(() => refreshStatus()); }";
    html += "setInterval(refreshStatus, 2000); refreshStatus();";
    html += "</script></body></html>";
    
    server.send(200, "text/html", html);
  });

  server.begin();
  Serial.println("HTTP server started on http://192.168.4.1");
  Serial.println("=== SoundBot Ready ===");
  Serial.println("Robot is operational and ready to respond to sound");
}

// ======== MAIN LOOP ========
void loop() {
  // Handle WiFi and server - these are non-blocking
  checkWiFiConnection();
  server.handleClient();
  
  // Handle MQTT - non-blocking, won't interfere with robot operation
  if (mqttEnabled) {
    if (mqtt.connected()) {
      mqtt.loop();  // Handle MQTT messages
      publishMQTTStatus();  // Publish status if connected
    } else {
      tryMQTTConnection();  // Try to connect if not connected
    }
  }

  // ===== CORE ROBOT FUNCTIONALITY - ALWAYS RUNS =====
  const int sampleCount = 50;
  long sum = 0;
  
  for (int i = 0; i < sampleCount; i++) {
    int minVal = 4095, maxVal = 0;
    unsigned long start = millis();
    
    while (millis() - start < 5) {
      int val = analogRead(analogPin);
      if (val < minVal) minVal = val;
      if (val > maxVal) maxVal = val;
    }
    
    int peakToPeak = maxVal - minVal;
    sum += peakToPeak;
  }

  avg = (float)sum / sampleCount * 10;

  bool external = isExternalSound(avg);
  if (!external) {
    updateAmbient(avg);
  }

  level = external ? getSoundLevel(avg) : 1;
  int speed = getSpeedByLevel(level);
  int radius = getObstacleRadiusByLevel(level);
  
  // Read distance
  currentDistance = readDistance();

  // Debug output
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 2000) {
    lastDebug = millis();
    Serial.print("Sound: "); Serial.print(avg, 1);
    Serial.print(" | Level: "); Serial.print(level);
    Serial.print(" | Distance: "); Serial.print(currentDistance);
    Serial.print(" | State: "); Serial.print(robotState);
    Serial.print(" | WiFi Clients: "); Serial.print(WiFi.softAPgetStationNum());
    Serial.print(" | MQTT: "); Serial.println(mqtt.connected() ? "OK" : "OFF");
  }

  // ===== ROBOT MOVEMENT CONTROL - CORE FUNCTIONALITY =====
  if (level >= 2) {
    handleObstacleAvoidance(speed, radius);
  } else {
    stopMotors();
  }

  delay(100);  // Balanced delay for responsive operation
}