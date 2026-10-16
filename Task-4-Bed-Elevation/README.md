
# Task 4: Intelligent Remote Bed Elevation Control The system enables remote adjustment of bed angle (0–90°) using a dashboard slider connected to a servo motor. Instead of abrupt movement, the servo transitions smoothly between angles to ensure patient comfort and safety. Safety constraints are applied to prevent sudden large movements, and predefined modes such as sleeping (10°), breathing support (45°), and emergency (90°) are included. Additionally, the system can automatically adjust the bed angle based on patient condition, simulating a real smart hospital bed with intelligent control.

## Overview
This task implements remote bed-angle control using an ESP32 and a servo motor. A dashboard command is used to request the bed angle within the configured range.

## Objectives
- Support bed-angle adjustment from 0° to 90°.
- Receive remote commands through MQTT.
- Control a servo motor based on the requested angle.
- Provide predefined bed-position modes.
- Apply movement constraints for smoother transitions.

## Hardware and Software
- ESP32 DevKit
- Servo motor
- OLED display, if used in the implementation
- Arduino IDE / Wokwi
- FreeRTOS
- MQTT and Adafruit IO

## Predefined Modes
- Sleeping: 10°
- Breathing support: 45°
- Emergency: 90°

## Implementation
The ESP32 receives bed-control commands and converts the requested angle into a servo position. Where implemented, gradual angle changes reduce abrupt movements. The configured modes provide convenient preset positions.

## Testing and Results
Use the Wokwi simulation to verify dashboard commands, servo positioning, and any implemented movement constraints. Document only the modes and transitions demonstrated in test evidence.

## Screenshots
ESP32 and servo connections, Bed-angle control dashboard, Sleeping mode at 10°, if captured, Breathing-support mode at 45°, if captured, Emergency mode at 90°.

## Conclusion
This task demonstrates remote positioning of a simulated hospital bed using MQTT commands and servo control.

**Disclaimer:** The simulation is for educational purposes and is not a clinically validated patient bed controller.

