
# Task 6: Advanced Offline Detection (Fault-Tolerant System) The ESP32 continuously monitors Wi-Fi, MQTT, and system health to determine three states: ONLINE, DEGRADED, and OFFLINE. If Wi-Fi or MQTT fails, the system enters offline mode, displays “LOGGING OFFLINE” on the OLED, and triggers distinct buzzer alerts depending on the failure type. Instead of losing data, sensor readings are stored in a local buffer using a queue or SPIFFS. Once connectivity is restored, the device automatically synchronizes all missed data to the cloud using a retry mechanism with exponential backoff. This ensures reliability and prevents data loss in real-world scenarios.

## Overview
This task adds connectivity monitoring and local data buffering to the ESP32 patient monitoring system. When MQTT connectivity is interrupted, sensor snapshots can be stored locally and synchronized after the connection is restored.

## Objectives
- Monitor Wi-Fi, MQTT, and system connectivity.
- Identify ONLINE, DEGRADED, and OFFLINE states.
- Display connectivity status on the OLED.
- Store sensor snapshots locally during communication failures.
- Retry MQTT connections.
- Synchronize queued data after reconnection.

## Hardware and Software
- ESP32 DevKit
- OLED display
- Patient and environmental sensor inputs
- Arduino IDE / Wokwi
- FreeRTOS
- MQTT and Adafruit IO
- SPIFFS local file storage

## Connectivity States
- **ONLINE:** MQTT connectivity is available.
- **DEGRADED:** A service such as MQTT is unavailable while other connectivity may remain active.
- **OFFLINE:** The system detects the configured offline condition, such as loss of Wi-Fi connectivity.

The exact state transition depends on the connectivity checks implemented in the firmware.

## Implementation
The ESP32 monitors connectivity and updates the OLED with the system status. When MQTT is disconnected, the local buffering logic stores sensor snapshots using SPIFFS. Once MQTT reconnects, queued snapshots can be published to the cloud and removed from the local queue after successful synchronization.

## Testing and Results
The captured Wokwi evidence demonstrates:
- MQTT deliberately disconnected while Wi-Fi remains enabled.
- A transition from ONLINE to DEGRADED.
- Local storage of a sensor snapshot.
- OLED feedback indicating MQTT disconnection and local buffering.
- Reconnection and a transition from DEGRADED to ONLINE.
- Successful synchronization of one queued snapshot.

Wi-Fi loss, multiple queued records, and data persistence after a device restart should be reported as tested only if separate evidence is available.

## Screenshots
Complete ESP32 monitoring circuit, MQTT disconnection and local buffer status, Serial output confirming local snapshot storage, OLED showing synchronization in progress, Reconnection and queued-snapshot synchronization.

## Conclusion
This task demonstrates fault-tolerant monitoring behavior through connectivity-state tracking, local buffering, reconnection, and queued-data synchronization.

## Limitation
The project is a simulation and does not establish clinical-grade reliability or guaranteed data delivery under every possible network failure.

