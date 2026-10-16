
# Task 1: Multi-Sensor Expansion Using FreeRTOS (Concurrent System Design) The system is extended by adding additional health parameters such as blood pressure or ECG monitoring, each managed by separate FreeRTOS tasks. Multiple tasks—including sensor reading, environment monitoring, MQTT communication, alert handling, and display updates—run concurrently using priority-based scheduling. Inter-task communication is handled using queues, semaphores, and mutexes to ensure data consistency and avoid race conditions. A watchdog timer is implemented for system reliability, ensuring that the system remains stable, responsive, and capable of handling real-time healthcare monitoring without failure.

## Overview
This task implements a multi-sensor patient monitoring system using an ESP32 and FreeRTOS. Multiple tasks handle sensor acquisition, environmental monitoring, alerts, MQTT communication, and display updates concurrently.

## Objectives
- Monitor patient and environmental parameters.
- Execute independent operations using FreeRTOS tasks.
- Display sensor readings and alerts on an OLED display.
- Support remote monitoring through MQTT.
- Improve responsiveness through concurrent task execution.

## Hardware and Software
- ESP32 DevKit
- ECG signal generator
- Temperature sensors
- PIR motion sensor
- Oxygen and air-quality sensor inputs
- OLED display (SSD1306)
- LEDs, buzzers, and supporting components
- Arduino IDE / Wokwi
- FreeRTOS
- MQTT and Adafruit IO

## Implementation
The ESP32 acquires sensor readings and processes patient and environmental information. Separate FreeRTOS tasks manage monitoring, communication, alert handling, and display operations. The OLED provides a local interface for viewing readings and alert conditions.

MQTT enables the system to exchange monitoring data with a remote dashboard.

## Testing and Results
The project is simulated using Wokwi. Sensor readings, OLED output, and alert indicators can be observed during simulation.

## Screenshots
C1-C7 and I1-I4, then the console and the circuit images as the Proofs

## Conclusion
This task establishes the concurrent, multi-sensor monitoring foundation for the remote patient risk monitoring system.
