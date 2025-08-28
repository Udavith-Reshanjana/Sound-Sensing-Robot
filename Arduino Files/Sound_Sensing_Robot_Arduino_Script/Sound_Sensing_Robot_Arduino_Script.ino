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

// Connection management - Non-blocking MQTT
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000;
unsigned long lastMQTTPublish = 0;
const unsigned long MQTT_PUBLISH_INTERVAL = 1000;
unsigned long lastMQTTAttempt = 0;
const unsigned long MQTT_RETRY_INTERVAL = 10000;
bool mqttEnabled = true;
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

// MQTT callback - silent operation
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  // Process MQTT messages silently
}

void tryMQTTConnection() {
  // Only try if MQTT is enabled and it's time to retry
  if (!mqttEnabled || mqtt.connected() || 
      (millis() - lastMQTTAttempt < MQTT_RETRY_INTERVAL)) {
    return;
  }
  
  lastMQTTAttempt = millis();
  
  // Set shorter timeout for connection attempt
  mqtt.setSocketTimeout(2);
  
  if (mqtt.connect("SoundBot")) {
    mqttConnected = true;
    mqttRetryCount = 0;
    mqtt.subscribe("soundbot/control");
  } else {
    mqttRetryCount++;
    
    // Disable MQTT temporarily after max retries
    if (mqttRetryCount >= MAX_MQTT_RETRIES) {
      mqttEnabled = false;
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
    mqttConnected = false;
  }
}

// ======== SETUP ========
void setup() {
  Serial.begin(115200);
  Serial.println("SoundBot Starting - IoT Sound-Sensing Robot");
  Serial.println("Owner: Udavith-Reshanjana");
  Serial.println("Date: 2025-08-28 09:50:35 UTC");
  
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
  Serial.println("Calibrating ambient noise level...");
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
  Serial.print("Initial ambient noise level: ");
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
    Serial.println("WiFi access point started successfully");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Network name: SoundBot-AP");
  } else {
    Serial.println("Failed to start WiFi AP - Robot will work in standalone mode");
  }

  // MQTT setup - silent operation
  mqtt.setServer(mqtt_broker, mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(15);

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
    doc["wifi_ap_active"] = (WiFi.getMode() == WIFI_AP);
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", jsonString);
  });

  // Clean web interface without MQTT references
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>SoundBot Control</title>";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }";
    html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += ".status { background: #f8f9fa; padding: 15px; margin: 15px 0; border-radius: 5px; border-left: 4px solid #007bff; }";
    html += ".header { text-align: center; color: #333; margin-bottom: 20px; }";
    html += ".data-row { display: flex; justify-content: space-between; margin: 8px 0; padding: 5px 0; border-bottom: 1px solid #eee; }";
    html += ".label { font-weight: bold; color: #555; }";
    html += ".value { color: #333; }";
    html += ".button { background: #007bff; color: white; padding: 12px 24px; border: none; border-radius: 5px; margin: 5px; cursor: pointer; font-size: 16px; }";
    html += ".button:hover { background: #0056b3; }";
    html += ".footer { text-align: center; margin-top: 20px; color: #666; font-size: 14px; }</style></head>";
    html += "<body><div class=\"container\">";
    html += "<div class=\"header\"><h1>SoundBot Control Panel</h1>";
    html += "<p>Owner: Udavith-Reshanjana</p></div>";
    html += "<div class=\"status\" id=\"status\">Loading robot status...</div>";
    html += "<div style=\"text-align: center;\">";
    html += "<button class=\"button\" onclick=\"refreshStatus()\">Refresh Status</button></div>";
    html += "<div class=\"footer\">";
    html += "<p>IoT Sound-Sensing Robot | Independent Operation</p>";
    html += "<p>Last updated: <span id=\"timestamp\">--</span></p></div>";
    html += "</div>";
    html += "<script>";
    html += "function refreshStatus() {";
    html += "fetch('/status').then(response => response.json()).then(data => {";
    html += "const stateNames = ['Forward', 'Turn Left', 'Backing Up', 'Turn Right', 'Stopped'];";
    html += "document.getElementById('status').innerHTML = ";
    html += "'<div class=\"data-row\"><span class=\"label\">Sound Level:</span><span class=\"value\">' + data.level + '</span></div>' +";
    html += "'<div class=\"data-row\"><span class=\"label\">Sound Intensity:</span><span class=\"value\">' + data.avg.toFixed(1) + ' units</span></div>' +";
    html += "'<div class=\"data-row\"><span class=\"label\">Distance:</span><span class=\"value\">' + data.distance + ' cm</span></div>' +";
    html += "'<div class=\"data-row\"><span class=\"label\">Robot State:</span><span class=\"value\">' + (stateNames[data.state] || 'Unknown') + '</span></div>' +";
    html += "'<div class=\"data-row\"><span class=\"label\">Connected Devices:</span><span class=\"value\">' + data.connected_stations + '</span></div>' +";
    html += "'<div class=\"data-row\"><span class=\"label\">Uptime:</span><span class=\"value\">' + Math.floor(data.uptime / 1000) + ' seconds</span></div>' +";
    html += "'<div class=\"data-row\"><span class=\"label\">Free Memory:</span><span class=\"value\">' + Math.floor(data.free_heap / 1024) + ' KB</span></div>';";
    html += "document.getElementById('timestamp').textContent = new Date().toLocaleTimeString();";
    html += "}); }";
    html += "setInterval(refreshStatus, 3000); refreshStatus();";
    html += "</script></body></html>";
    
    server.send(200, "text/html", html);
  });

  server.begin();
  Serial.println("HTTP server started on http://192.168.4.1");
  Serial.println("=== SoundBot Ready ===");
  Serial.println("Robot is operational and ready to respond to sound");
  Serial.println("IoT monitoring available via mobile app connection");
}

// ======== MAIN LOOP ========
void loop() {
  // Handle WiFi and server - non-blocking
  checkWiFiConnection();
  server.handleClient();
  
  // Handle MQTT - silent background operation
  if (mqttEnabled) {
    if (mqtt.connected()) {
      mqtt.loop();
      publishMQTTStatus();
    } else {
      tryMQTTConnection();
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

  // Clean debug output - no MQTT status
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug >= 2000) {
    lastDebug = millis();
    Serial.print("Sound: "); Serial.print(avg, 1);
    Serial.print(" | Level: "); Serial.print(level);
    Serial.print(" | Distance: "); Serial.print(currentDistance);
    Serial.print(" | State: "); Serial.print(robotState);
    Serial.print(" | WiFi Clients: "); Serial.println(WiFi.softAPgetStationNum());
  }

  // ===== ROBOT MOVEMENT CONTROL - CORE FUNCTIONALITY =====
  if (level >= 2) {
    handleObstacleAvoidance(speed, radius);
  } else {
    stopMotors();
  }

  delay(100);
}