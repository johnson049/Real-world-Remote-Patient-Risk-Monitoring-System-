
# Task 5: Smart Dynamic Sampling Rate (Adaptive Monitoring) The sampling rate is controlled through a dashboard slider ranging from 5 to 60 seconds, allowing doctors to manually adjust monitoring frequency. In addition, an automatic mode intelligently adapts the sampling rate based on patient vitals—switching to high-frequency monitoring (5 seconds) during abnormal conditions and reducing frequency during stable periods to optimize battery life. This is implemented using FreeRTOS with non-blocking delays (vTaskDelay) and dynamic updates through queues, ensuring efficient and responsive real-time behavior.

## Overview
This task implements configurable sampling intervals for patient and facility monitoring. The sampling interval can be changed manually from the dashboard or controlled automatically according to the configured patient-condition logic.

## Objectives
- Support manual sampling intervals from 5 to 60 seconds.
- Provide AUTO and MANUAL operating modes.
- Use a 5-second interval during detected abnormal conditions in AUTO mode.
- Use a reduced monitoring frequency during stable conditions.
- Apply dynamic configuration updates within the FreeRTOS application.

## Hardware and Software
- ESP32 DevKit
- Patient and environmental sensor inputs
- OLED display
- Arduino IDE / Wokwi
- FreeRTOS
- MQTT and Adafruit IO

## Implementation
The sampling configuration includes an interval and an operating mode. MQTT dashboard commands update the configuration, and the ESP32 reports the active sampling interval through the serial monitor.

The configured behavior is:
- **MANUAL:** Sampling interval selected by the user, from 5 to 60 seconds.
- **AUTO:** Switch to a 5-second interval when the abnormal-condition logic is triggered and use a longer interval when conditions are stable.

The implementation uses FreeRTOS task scheduling and non-blocking task delays to support dynamic interval changes.

## Testing and Results
The captured evidence demonstrates manual intervals of 20, 25, 40, and 60 seconds and a switch to AUTO mode reporting a 5-second interval.

Screenshots should also demonstrate the automatic return to a longer interval under stable conditions if that behavior was tested.

## Screenshots
Complete monitoring circuit, Manual interval changes, AUTO mode and 5-second interval, Dashboard in MANUAL mode, Dashboard in AUTO mode, Abnormal readings.

## Conclusion
This task demonstrates configurable sampling and adaptive monitoring behavior designed to increase monitoring frequency when abnormal conditions are detected.

## Limitation
The displayed interval and mode confirm configuration changes. End-to-end adaptive behavior should be validated by observing both abnormal-condition activation and the return to a stable-condition interval.

