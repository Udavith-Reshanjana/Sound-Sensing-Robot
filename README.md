# 🧠 Sound-Sensing Robot

## 🚀 Overview

This robot is equipped with a **KY-037 sound sensor** and an **ultrasonic obstacle sensor**, enabling it to respond dynamically to ambient sound intensity and surrounding obstacles. It simulates intelligent behavior using a simple hardware setup.

### 🎧 Sound-Based Motion Logic

* Continuously samples ambient sound via the **KY-037 sensor**
* Classifies sound into **4 distinct intensity levels**
* Adjusts motor speed and movement accordingly:

  * **Very Quiet** → Stop
  * **Moderate to Very Loud** → Move forward at varying speeds
* Continuously monitors for obstacles using an **ultrasonic sensor**

### 🛑 Obstacle Avoidance Strategy

* If an obstacle is detected within a level-specific radius:

  * Attempts up to **5 left turns**
  * If still blocked: **backs up**, then **turns right**
  * Resumes normal forward motion (if path is clear)

This approach enables environment-aware motion using just a **single ultrasonic sensor**, eliminating the need for servos or additional sensing hardware.

---

## 🔢 Sound Levels and Corresponding Behavior

| Sound Level        | Speed (PWM) | Obstacle Detection Radius | Behavior                          |
| ------------------ | ----------- | ------------------------- | --------------------------------- |
| **1 - Very Quiet** | 0           | ∞ (no motion)             | Stop                              |
| **2 - Moderate**   | 100         | 30 cm                     | Slow forward, avoid close objects |
| **3 - Loud**       | 180         | 60 cm                     | Medium speed, wider sensing       |
| **4 - Very Loud**  | 255         | 90 cm                     | Fast, long-range avoidance        |

---

## 🔄 Obstacle Avoidance Logic

When an obstacle is detected:

1. The robot tries up to **5 left turns**, checking after each if the path is clear.
2. If still blocked:

   * Reverses slightly
   * Performs a **right turn**
3. If path clears, resumes forward motion (based on current sound level).

This enables **self-correcting behavior** using only front-facing sensing, emulating decision-making in complex environments.

---
