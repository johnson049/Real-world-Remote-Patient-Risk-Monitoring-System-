
# Task 2: Dual-Role IoT Monitoring System (Role-Based Dashboards) The system separates data into two logical dashboards: a Medical Staff Dashboard displaying patient vitals (heart rate, SpO₂, body temperature) and a Facility Management Dashboard showing environmental conditions (room temperature, oxygen level, AQI). Each dataset is published to separate MQTT topics, enabling role-based access and monitoring. Gauge widgets are used with predefined safe thresholds, and additional alert feeds are implemented for abnormal conditions. The system also performs periodic data aggregation to provide summarized insights, making it scalable and closer to enterprise IoT architecture.

## Overview
This task separates monitoring information into two logical dashboards: a Medical Staff Dashboard for patient vitals and a Facility Management Dashboard for environmental conditions.

## Objectives
- Monitor patient vital parameters.
- Monitor room and environmental conditions.
- Separate medical and facility data streams.
- Publish monitoring data through MQTT.
- Display abnormal conditions and alerts.

## Hardware and Software
- ESP32 DevKit
- ECG signal generator
- Temperature sensors
- Oxygen sensor input
- Air-quality sensor input
- PIR motion sensor
- OLED display
- Arduino IDE / Wokwi
- FreeRTOS
- MQTT and Adafruit IO

## Dashboard Functions

### Medical Staff Dashboard
Displays patient-related measurements, including:
- Body temperature
- Heart rate
- SpO₂
- ECG-related reading

### Facility Management Dashboard
Displays environmental measurements, including:
- Room temperature
- Oxygen level
- Air Quality Index (AQI)
- Motion detection status

## Implementation
The system processes patient and environmental readings and presents them through separate dashboard feeds. MQTT is used to transfer data to the monitoring interface. Threshold-based alerts indicate abnormal readings.

## Testing and Results
The Wokwi simulation provides OLED and serial-monitor evidence of patient and facility monitoring. Dashboard screenshots should be added to document the configured feeds and widgets.

## Screenshots
- T2 Represents the TASK2 so and then the next number represents the level of screen shots like 1 for the circuit and then console op and Adafruit dashboard etc.,

## Conclusion
This task organizes patient and facility monitoring into separate logical views, improving the clarity of monitoring information.

