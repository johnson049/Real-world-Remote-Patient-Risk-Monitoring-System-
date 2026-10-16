
# Task 3: Smart Medication Dosage Adjustment System A dashboard slider (0–100 mg/hr) allows doctors to remotely control medication dosage, which is displayed in real-time on the OLED. The system includes safety levels: normal, warning, and critical. If the dosage exceeds 80 mg/hr, a red LED blinks rapidly and a buzzer alerts for safety. To prevent sudden changes, rate-limiting logic restricts abrupt dosage increases, and critical values may require confirmation. This ensures a safer and more controlled simulation of a medical infusion system.

## Overview
This task implements a simulated medication dosage control system. A remote dashboard is used to request dosage adjustments, while the ESP32 processes the requested value and provides status feedback.

## Objectives
- Support remote dosage adjustment from 0 to 100 mg/hr.
- Display dosage information and status.
- Classify dosage into normal, warning, and critical levels.
- Provide LED and buzzer alerts for critical values.
- Apply rate limiting to dosage increases.
- Require confirmation for critical requests when configured.

## Hardware and Software
- ESP32 DevKit
- OLED display
- LED indicator
- Buzzer
- Arduino IDE / Wokwi
- FreeRTOS
- MQTT and Adafruit IO

## Safety Logic
The simulation uses the following configured thresholds:

| Dosage | Status |
|---|---|
| 0–50 mg/hr | NORMAL |
| 51–80 mg/hr | WARNING |
| 81–100 mg/hr | CRITICAL |

The system is configured to limit dosage increases to 10 mg/hr per one-second interval. Critical dosage requests can require confirmation before acceptance.

## Implementation
The ESP32 receives dosage commands through MQTT and updates the medication status. The OLED and alert outputs provide feedback about the current simulated state. The LED blinks and the buzzer alerts when the configured critical condition is triggered.

## Testing and Results
Use the captured Wokwi and serial-monitor screenshots to document normal, warning, and critical conditions, dosage changes, rate limiting, and confirmation behavior where tested.

## Screenshots
- Circuit connections, Normal dosage status, Warning status, if captured, Critical alert, if captured.


## Conclusion
This task demonstrates remote dosage adjustment with threshold-based alerts and control safeguards in a simulated medication management system.

**Disclaimer:** This is an educational simulation, not a certified medical infusion controller.

