#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_task_wdt.h"

// ============================================================
// TASK 4 - INTELLIGENT REMOTE BED ELEVATION CONTROL

// ============================================================

// ---------------------------
// TASK 3 medication hardware
// ---------------------------
#define MEDICATION_LED_PIN       23
#define MEDICATION_BUZZER_PIN    18

#define MEDICATION_MIN_DOSAGE     0
#define MEDICATION_MAX_DOSAGE   100
#define MEDICATION_NORMAL_MAX    50
#define MEDICATION_WARNING_MAX   80
#define MEDICATION_CRITICAL_MIN  81
#define MEDICATION_RATE_LIMIT    10
#define MEDICATION_RATE_INTERVAL 1000
#define MEDICATION_ALARM_INTERVAL 150

// ---------------------------
// TASK 4 bed hardware
// ---------------------------
#define BED_SERVO_PIN            13
#define BED_MIN_ANGLE             0
#define BED_MAX_ANGLE            90

#define BED_SLEEP_ANGLE          10
#define BED_BREATHING_ANGLE      45
#define BED_EMERGENCY_ANGLE      90

#define BED_STEP_DEGREES          1
#define BED_STEP_INTERVAL_MS     40

// ---------------------------
// Existing Task 2 hardware
// ---------------------------
#define BODY_TEMP_SENSOR  14
#define ECG_SENSOR        34
#define MOTION_DETECTOR   12

#define ROOM_TEMP_SENSOR  27
#define OXYGEN_SENSOR     35
#define AQI_SENSOR        39

#define BUZZER_PIN_1       4
#define BUZZER_PIN_2      16
#define BUZZER_PIN_3      17
#define BUZZER_PIN_4       5

#define SW_TEMP           32
#define SW_HR             33
#define SW_SPO2           25
#define SW_MOTION         26

// ---------------------------
// OLED
// ---------------------------
#define SCREEN_WIDTH       128
#define SCREEN_HEIGHT       64
#define OLED_RESET          -1
#define OLED_ADDRESS       0x3C

// ---------------------------
// WiFi / Adafruit IO
// ---------------------------
#define WIFI_SSID         "Wokwi-GUEST"
#define WIFI_PASS         ""

#define IO_USERNAME       "jssg1234"
#define IO_KEY            "aio_KEY"

#define AIO_SERVER        "io.adafruit.com"
#define AIO_SERVERPORT    1883

// ============================================================
// FEEDS
// ============================================================

// Medical
#define MEDICAL_BODY_TEMP_FEED \
  IO_USERNAME "/feeds/medical-body-temperature"
#define MEDICAL_HR_FEED \
  IO_USERNAME "/feeds/medical-heart-rate"
#define MEDICAL_SPO2_FEED \
  IO_USERNAME "/feeds/medical-spo2"
#define MEDICAL_ALERT_FEED \
  IO_USERNAME "/feeds/medical-alert"

// Facility
#define FACILITY_ROOM_TEMP_FEED \
  IO_USERNAME "/feeds/facility-room-temperature"
#define FACILITY_OXYGEN_FEED \
  IO_USERNAME "/feeds/facility-oxygen-level"
#define FACILITY_AQI_FEED \
  IO_USERNAME "/feeds/facility-aqi"
#define FACILITY_ALERT_FEED \
  IO_USERNAME "/feeds/facility-alert"

// Medication
#define MEDICATION_CURRENT_FEED \
  IO_USERNAME "/feeds/medication-current"
#define MEDICATION_ALERT_FEED \
  IO_USERNAME "/feeds/medication-alert"

// Task 4
#define BED_CONTROL_FEED \
  IO_USERNAME "/feeds/bed-control"

// ============================================================
// MQTT
// ============================================================

WiFiClient client;

Adafruit_MQTT_Client mqtt(
  &client,
  AIO_SERVER,
  AIO_SERVERPORT,
  IO_USERNAME,
  IO_KEY
);

// Publishers
Adafruit_MQTT_Publish medicalBodyTempFeed(
  &mqtt, MEDICAL_BODY_TEMP_FEED
);
Adafruit_MQTT_Publish medicalHRFeed(
  &mqtt, MEDICAL_HR_FEED
);
Adafruit_MQTT_Publish medicalSpO2Feed(
  &mqtt, MEDICAL_SPO2_FEED
);
Adafruit_MQTT_Publish medicalAlertFeed(
  &mqtt, MEDICAL_ALERT_FEED
);

Adafruit_MQTT_Publish facilityRoomTempFeed(
  &mqtt, FACILITY_ROOM_TEMP_FEED
);
Adafruit_MQTT_Publish facilityOxygenFeed(
  &mqtt, FACILITY_OXYGEN_FEED
);
Adafruit_MQTT_Publish facilityAQIFeed(
  &mqtt, FACILITY_AQI_FEED
);
Adafruit_MQTT_Publish facilityAlertFeed(
  &mqtt, FACILITY_ALERT_FEED
);

Adafruit_MQTT_Publish medicationAlertFeed(
  &mqtt, MEDICATION_ALERT_FEED
);

// Task 4: one combined input feed
Adafruit_MQTT_Subscribe bedControlFeed(
  &mqtt, BED_CONTROL_FEED
);

// Task 3 subscriptions
Adafruit_MQTT_Subscribe medicationCurrentControlFeed(
  &mqtt, MEDICATION_CURRENT_FEED
);

Adafruit_MQTT_Subscribe medicationAlertControlFeed(
  &mqtt, MEDICATION_ALERT_FEED
);

// ============================================================
// SENSOR / OLED / SERVO
// ============================================================

OneWire bodyTempWire(BODY_TEMP_SENSOR);
DallasTemperature bodyTempSensor(&bodyTempWire);

OneWire roomTempWire(ROOM_TEMP_SENSOR);
DallasTemperature roomTempSensor(&roomTempWire);

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

Servo bedServo;

// ============================================================
// DATA STRUCTURES
// ============================================================

struct MedicalData {
  float bodyTemperature;
  int heartRate;
  int spo2;
};

struct FacilityData {
  float roomTemperature;
  float oxygenLevel;
  int aqi;
};

struct MedicationData {
  int requestedDosage;
  int currentDosage;
  char status[16];
  char alert[64];
  bool confirmationRequired;
  bool criticalConfirmed;
};

enum BedMode {
  BED_MANUAL,
  BED_SLEEP,
  BED_BREATHING,
  BED_EMERGENCY,
  BED_AUTO
};

struct BedData {
  int requestedAngle;
  int targetAngle;
  int currentAngle;
  BedMode mode;
  bool moving;
};

struct SystemData {
  MedicalData medical;
  FacilityData facility;
  MedicationData medication;
  BedData bed;
  bool motion;
  int ecg;
  char medicalAlert[64];
  char facilityAlert[64];
};

SystemData systemData;

// ============================================================
// TASK 2 THRESHOLDS
// ============================================================

#define BODY_TEMP_HIGH  38.0
#define HEART_RATE_LOW  60
#define HEART_RATE_HIGH 300
#define SPO2_LOW        85

#define ROOM_TEMP_LOW   18.0
#define ROOM_TEMP_HIGH  30.0
#define OXYGEN_LOW      19.5
#define OXYGEN_HIGH     23.5
#define AQI_HIGH        100

// ============================================================
// RTOS OBJECTS
// ============================================================

QueueHandle_t medicalQueue;
QueueHandle_t facilityQueue;
QueueHandle_t environmentQueue;
QueueHandle_t ecgQueue;

SemaphoreHandle_t dataMutex;
SemaphoreHandle_t displayMutex;
SemaphoreHandle_t displaySemaphore;
SemaphoreHandle_t alertSemaphore;

// ============================================================
// WATCHDOG
// ============================================================

#define TASK_COUNT 10
#define WATCHDOG_CHECK_INTERVAL_MS 1000
#define TASK_TIMEOUT_MS 4000

enum TaskID {
  TASK_MEDICAL_SENSOR = 0,
  TASK_FACILITY_SENSOR,
  TASK_ECG,
  TASK_ENVIRONMENT,
  TASK_DATA_MANAGER,
  TASK_DISPLAY,
  TASK_ALERT,
  TASK_MQTT,
  TASK_MEDICATION,
  TASK_BED
};

volatile uint32_t taskHeartbeat[TASK_COUNT] = {0};

// ============================================================
// MEDICATION STATE
// ============================================================

volatile int medicationRequestedDosage = 0;
volatile int medicationCurrentDosage = 0;
volatile bool medicationCriticalConfirmed = false;
volatile bool medicationConfirmationRequired = false;
volatile uint32_t medicationLastIncrease = 0;
volatile uint32_t medicationLastAlarm = 0;
volatile bool medicationAlarmState = false;

// ============================================================
// BED STATE
// ============================================================

volatile int bedRequestedAngle = BED_SLEEP_ANGLE;
volatile int bedTargetAngle = BED_SLEEP_ANGLE;
volatile int bedCurrentAngle = BED_SLEEP_ANGLE;

volatile BedMode bedMode = BED_MANUAL;
volatile bool bedMoving = false;
volatile uint32_t bedLastStep = 0;

// ============================================================
// DISPLAY
// ============================================================

volatile int displayMode = 0;
// 0 = Medical
// 1 = Facility
// 2 = Medication
// 3 = Bed

// ============================================================
// UTILITY
// ============================================================

float readOxygenLevel() {
  int rawValue = analogRead(OXYGEN_SENSOR);
  return 18.0 + ((float)rawValue / 4095.0) * 7.0;
}

int readAQI() {
  return map(analogRead(AQI_SENSOR), 0, 4095, 0, 500);
}

const char* getBedModeName() {
  switch (bedMode) {
    case BED_MANUAL:    return "MANUAL";
    case BED_SLEEP:     return "SLEEP";
    case BED_BREATHING: return "BREATHING";
    case BED_EMERGENCY: return "EMERGENCY";
    case BED_AUTO:      return "AUTO";
    default:            return "UNKNOWN";
  }
}

// ============================================================
// MEDICATION STATUS
// ============================================================

void updateMedicationStatus() {
  if (medicationCurrentDosage <= MEDICATION_NORMAL_MAX) {
    strcpy(systemData.medication.status, "NORMAL");
  }
  else if (medicationCurrentDosage <= MEDICATION_WARNING_MAX) {
    strcpy(systemData.medication.status, "WARNING");
  }
  else {
    strcpy(systemData.medication.status, "CRITICAL");
  }

  systemData.medication.requestedDosage =
    medicationRequestedDosage;

  systemData.medication.currentDosage =
    medicationCurrentDosage;

  systemData.medication.confirmationRequired =
    medicationConfirmationRequired;

  systemData.medication.criticalConfirmed =
    medicationCriticalConfirmed;
}

// ============================================================
// MEDICATION CONTROL
// ============================================================

void requestMedicationDosage(int requested) {
  requested = constrain(
    requested,
    MEDICATION_MIN_DOSAGE,
    MEDICATION_MAX_DOSAGE
  );

  medicationRequestedDosage = requested;

  if (
    requested >= MEDICATION_CRITICAL_MIN &&
    !medicationCriticalConfirmed
  ) {
    medicationConfirmationRequired = true;

    if (
      xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50))
      == pdTRUE
    ) {
      strcpy(
        systemData.medication.alert,
        "CRITICAL: CONFIRM REQUIRED"
      );

      updateMedicationStatus();
      xSemaphoreGive(dataMutex);
    }

    return;
  }

  medicationConfirmationRequired = false;

  if (requested < medicationCurrentDosage) {
    medicationCurrentDosage = requested;
  }
  else if (requested > medicationCurrentDosage) {
    uint32_t now = millis();

    if (
      now - medicationLastIncrease >=
      MEDICATION_RATE_INTERVAL
    ) {
      int increase =
        requested - medicationCurrentDosage;

      if (increase > MEDICATION_RATE_LIMIT) {
        increase = MEDICATION_RATE_LIMIT;
      }

      medicationCurrentDosage += increase;
      medicationLastIncrease = now;
    }
  }

  if (
    xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50))
    == pdTRUE
  ) {
    if (
      medicationCurrentDosage >=
      MEDICATION_CRITICAL_MIN
    ) {
      strcpy(
        systemData.medication.alert,
        "CRITICAL DOSAGE ACTIVE"
      );
    }
    else if (
      medicationCurrentDosage >
      MEDICATION_NORMAL_MAX
    ) {
      strcpy(
        systemData.medication.alert,
        "WARNING DOSAGE"
      );
    }
    else {
      strcpy(
        systemData.medication.alert,
        "Dosage normal"
      );
    }

    updateMedicationStatus();
    xSemaphoreGive(dataMutex);
  }

  Serial.print("Medication request: ");
  Serial.print(requested);
  Serial.print(" mg/hr -> current: ");
  Serial.print(medicationCurrentDosage);
  Serial.println(" mg/hr");
}

// ============================================================
// TASK 4 - BED TARGET LOGIC
// ============================================================

void updateBedTarget() {

  if (bedMode == BED_MANUAL) {
    bedTargetAngle = bedRequestedAngle;
  }

  else if (bedMode == BED_SLEEP) {
    bedTargetAngle = BED_SLEEP_ANGLE;
  }

  else if (bedMode == BED_BREATHING) {
    bedTargetAngle = BED_BREATHING_ANGLE;
  }

  else if (bedMode == BED_EMERGENCY) {
    bedTargetAngle = BED_EMERGENCY_ANGLE;
  }

  else if (bedMode == BED_AUTO) {

    // Critical condition has priority.
    if (
      (
        systemData.medical.heartRate > HEART_RATE_HIGH ||
        systemData.medical.heartRate < HEART_RATE_LOW
      )
      &&
      systemData.medical.heartRate != 0
    ) {
      bedTargetAngle = BED_EMERGENCY_ANGLE;
    }

    else if (
      systemData.medical.bodyTemperature >
      BODY_TEMP_HIGH
    ) {
      bedTargetAngle = BED_EMERGENCY_ANGLE;
    }

    // Low SpO2 -> breathing support.
    else if (
      systemData.medical.spo2 > 0 &&
      systemData.medical.spo2 < SPO2_LOW
    ) {
      bedTargetAngle = BED_BREATHING_ANGLE;
    }

    // Normal condition -> sleeping position.
    else {
      bedTargetAngle = BED_SLEEP_ANGLE;
    }
  }

  bedTargetAngle = constrain(
    bedTargetAngle,
    BED_MIN_ANGLE,
    BED_MAX_ANGLE
  );

  if (
    xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10))
    == pdTRUE
  ) {
    systemData.bed.requestedAngle =
      bedRequestedAngle;

    systemData.bed.targetAngle =
      bedTargetAngle;

    systemData.bed.currentAngle =
      bedCurrentAngle;

    systemData.bed.mode =
      bedMode;

    systemData.bed.moving =
      bedMoving;

    xSemaphoreGive(dataMutex);
  }
}

// ============================================================
// TASK 2 - MEDICAL SENSOR TASK
// ============================================================

void medicalSensorTask(void *param) {
  MedicalData data;

  while (1) {
    taskHeartbeat[TASK_MEDICAL_SENSOR] = millis();

    data.bodyTemperature = 0.0;

    if (!digitalRead(SW_TEMP)) {
      bodyTempSensor.requestTemperatures();
      data.bodyTemperature =
        bodyTempSensor.getTempCByIndex(0);
    }

    if (!digitalRead(SW_HR)) {
      data.heartRate = random(45, 351);
    }
    else {
      data.heartRate = 0;
    }

    if (!digitalRead(SW_SPO2)) {
      data.spo2 = random(60, 101);
    }
    else {
      data.spo2 = 0;
    }

    xQueueOverwrite(medicalQueue, &data);

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ============================================================
// TASK 2 - FACILITY SENSOR TASK
// ============================================================

void facilitySensorTask(void *param) {
  FacilityData data;

  while (1) {
    taskHeartbeat[TASK_FACILITY_SENSOR] = millis();

    roomTempSensor.requestTemperatures();

    data.roomTemperature =
      roomTempSensor.getTempCByIndex(0);

    data.oxygenLevel = readOxygenLevel();
    data.aqi = readAQI();

    xQueueOverwrite(facilityQueue, &data);

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ============================================================
// TASK 2 - ENVIRONMENT TASK
// ============================================================

void environmentTask(void *param) {
  bool motion;

  while (1) {
    taskHeartbeat[TASK_ENVIRONMENT] = millis();

    motion = digitalRead(MOTION_DETECTOR);

    xQueueOverwrite(environmentQueue, &motion);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ============================================================
// TASK 2 - ECG TASK
// ============================================================

void ecgTask(void *param) {
  int ecgValue;

  while (1) {
    taskHeartbeat[TASK_ECG] = millis();

    ecgValue = analogRead(ECG_SENSOR);

    xQueueOverwrite(ecgQueue, &ecgValue);

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
// TASK 3 - MEDICATION TASK
// ============================================================

void medicationTask(void *param) {

  pinMode(MEDICATION_LED_PIN, OUTPUT);
  pinMode(MEDICATION_BUZZER_PIN, OUTPUT);

  digitalWrite(MEDICATION_LED_PIN, LOW);
  digitalWrite(MEDICATION_BUZZER_PIN, LOW);

  while (1) {

    taskHeartbeat[TASK_MEDICATION] = millis();

    if (
      medicationRequestedDosage >
      medicationCurrentDosage &&
      !medicationConfirmationRequired
    ) {
      requestMedicationDosage(
        medicationRequestedDosage
      );
    }

    if (
      medicationCurrentDosage >=
      MEDICATION_CRITICAL_MIN
    ) {

      uint32_t now = millis();

      if (
        now - medicationLastAlarm >=
        MEDICATION_ALARM_INTERVAL
      ) {

        medicationLastAlarm = now;
        medicationAlarmState =
          !medicationAlarmState;

        digitalWrite(
          MEDICATION_LED_PIN,
          medicationAlarmState
        );

        digitalWrite(
          MEDICATION_BUZZER_PIN,
          medicationAlarmState
        );
      }
    }

    else {
      medicationAlarmState = false;

      digitalWrite(
        MEDICATION_LED_PIN,
        LOW
      );

      digitalWrite(
        MEDICATION_BUZZER_PIN,
        LOW
      );
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ============================================================
// TASK 4 - BED ELEVATION TASK
// ============================================================

void bedElevationTask(void *param) {

  while (1) {

    taskHeartbeat[TASK_BED] = millis();

    // AUTO mode continuously recalculates target.
    if (bedMode == BED_AUTO) {
      updateBedTarget();
    }

    uint32_t now = millis();

    if (
      now - bedLastStep >=
      BED_STEP_INTERVAL_MS
    ) {

      if (
        bedCurrentAngle <
        bedTargetAngle
      ) {

        bedCurrentAngle +=
          BED_STEP_DEGREES;

        if (
          bedCurrentAngle >
          bedTargetAngle
        ) {
          bedCurrentAngle =
            bedTargetAngle;
        }

        bedServo.write(
          bedCurrentAngle
        );

        bedMoving = true;
        bedLastStep = now;
      }

      else if (
        bedCurrentAngle >
        bedTargetAngle
      ) {

        bedCurrentAngle -=
          BED_STEP_DEGREES;

        if (
          bedCurrentAngle <
          bedTargetAngle
        ) {
          bedCurrentAngle =
            bedTargetAngle;
        }

        bedServo.write(
          bedCurrentAngle
        );

        bedMoving = true;
        bedLastStep = now;
      }

      else {
        bedMoving = false;
      }

      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(10)
        ) == pdTRUE
      ) {

        systemData.bed.currentAngle =
          bedCurrentAngle;

        systemData.bed.targetAngle =
          bedTargetAngle;

        systemData.bed.mode =
          bedMode;

        systemData.bed.moving =
          bedMoving;

        xSemaphoreGive(dataMutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================================
// TASK 2 - MEDICAL ALERT
// ============================================================

void updateMedicalAlert() {

  strcpy(
    systemData.medicalAlert,
    "OK"
  );

  if (
    !digitalRead(SW_TEMP) &&
    systemData.medical.bodyTemperature >
    BODY_TEMP_HIGH
  ) {
    strcpy(
      systemData.medicalAlert,
      "High body temperature"
    );
    return;
  }

  if (
    !digitalRead(SW_HR) &&
    systemData.medical.heartRate <
    HEART_RATE_LOW
  ) {
    strcpy(
      systemData.medicalAlert,
      "HR below normal"
    );
    return;
  }

  if (
    !digitalRead(SW_HR) &&
    systemData.medical.heartRate >
    HEART_RATE_HIGH
  ) {
    strcpy(
      systemData.medicalAlert,
      "HR above normal"
    );
    return;
  }

  if (
    !digitalRead(SW_SPO2) &&
    systemData.medical.spo2 <
    SPO2_LOW
  ) {
    strcpy(
      systemData.medicalAlert,
      "SpO2 below normal"
    );
    return;
  }

  if (
    systemData.motion &&
    !digitalRead(SW_MOTION)
  ) {
    strcpy(
      systemData.medicalAlert,
      "Motion detected"
    );
  }
}

// ============================================================
// TASK 2 - FACILITY ALERT
// ============================================================

void updateFacilityAlert() {

  strcpy(
    systemData.facilityAlert,
    "OK"
  );

  if (
    systemData.facility.roomTemperature <
    ROOM_TEMP_LOW
  ) {
    strcpy(
      systemData.facilityAlert,
      "Room temperature low"
    );
    return;
  }

  if (
    systemData.facility.roomTemperature >
    ROOM_TEMP_HIGH
  ) {
    strcpy(
      systemData.facilityAlert,
      "Room temperature high"
    );
    return;
  }

  if (
    systemData.facility.oxygenLevel <
    OXYGEN_LOW
  ) {
    strcpy(
      systemData.facilityAlert,
      "Oxygen level low"
    );
    return;
  }

  if (
    systemData.facility.oxygenLevel >
    OXYGEN_HIGH
  ) {
    strcpy(
      systemData.facilityAlert,
      "Oxygen level high"
    );
    return;
  }

  if (
    systemData.facility.aqi >
    AQI_HIGH
  ) {
    strcpy(
      systemData.facilityAlert,
      "AQI above safe level"
    );
  }
}

// ============================================================
// DATA MANAGER TASK
// ============================================================

void dataManagerTask(void *param) {

  MedicalData medicalData;
  FacilityData facilityData;
  bool motion;
  int ecgValue;

  while (1) {

    taskHeartbeat[TASK_DATA_MANAGER] =
      millis();

    if (
      xQueueReceive(
        medicalQueue,
        &medicalData,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.medical =
          medicalData;

        xSemaphoreGive(dataMutex);
      }
    }

    if (
      xQueueReceive(
        facilityQueue,
        &facilityData,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.facility =
          facilityData;

        xSemaphoreGive(dataMutex);
      }
    }

    if (
      xQueueReceive(
        environmentQueue,
        &motion,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.motion =
          motion;

        xSemaphoreGive(dataMutex);
      }
    }

    if (
      xQueueReceive(
        ecgQueue,
        &ecgValue,
        0
      ) == pdPASS
    ) {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {
        systemData.ecg =
          ecgValue;

        xSemaphoreGive(dataMutex);
      }
    }

    if (
      xSemaphoreTake(
        dataMutex,
        pdMS_TO_TICKS(100)
      ) == pdTRUE
    ) {

      updateMedicalAlert();
      updateFacilityAlert();

      xSemaphoreGive(dataMutex);
    }

    xSemaphoreGive(displaySemaphore);
    xSemaphoreGive(alertSemaphore);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ============================================================
// DISPLAY TASK
// ============================================================

void displayTask(void *param) {

  SystemData data;
  uint32_t lastScreenChange = 0;

  while (1) {

    taskHeartbeat[TASK_DISPLAY] =
      millis();

    if (
      millis() - lastScreenChange >=
      5000
    ) {

      displayMode++;

      if (displayMode > 3) {
        displayMode = 0;
      }

      lastScreenChange =
        millis();
    }

    if (
      xSemaphoreTake(
        displaySemaphore,
        pdMS_TO_TICKS(1000)
      ) == pdTRUE
    ) {

      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {

        data = systemData;

        xSemaphoreGive(dataMutex);
      }

      if (
        xSemaphoreTake(
          displayMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);

        if (displayMode == 0) {

          display.println(
            "MEDICAL DASHBOARD"
          );

          display.print("Body: ");
          display.print(
            data.medical.bodyTemperature,
            1
          );
          display.println(" C");

          display.print("HR: ");
          display.println(
            data.medical.heartRate
          );

          display.print("SpO2: ");
          display.print(
            data.medical.spo2
          );
          display.println(" %");

          display.print("ECG: ");
          display.println(
            data.ecg
          );

          display.print("Alert: ");
          display.println(
            data.medicalAlert
          );
        }

        else if (displayMode == 1) {

          display.println(
            "FACILITY DASHBOARD"
          );

          display.print("Room: ");
          display.print(
            data.facility.roomTemperature,
            1
          );
          display.println(" C");

          display.print("O2: ");
          display.print(
            data.facility.oxygenLevel,
            1
          );
          display.println(" %");

          display.print("AQI: ");
          display.println(
            data.facility.aqi
          );

          display.print("Motion: ");
          display.println(
            data.motion ? "YES" : "NO"
          );

          display.print("Alert: ");
          display.println(
            data.facilityAlert
          );
        }

        else if (displayMode == 2) {

          display.println(
            "MEDICATION CONTROL"
          );

          display.print("Request: ");
          display.print(
            data.medication.requestedDosage
          );
          display.println(" mg/hr");

          display.print("Dosage: ");
          display.print(
            data.medication.currentDosage
          );
          display.println(" mg/hr");

          display.print("Status: ");
          display.println(
            data.medication.status
          );

          if (
            data.medication.confirmationRequired
          ) {
            display.println(
              "CONFIRM REQUIRED"
            );
          }

          else if (
            data.medication.currentDosage >=
            MEDICATION_CRITICAL_MIN
          ) {
            display.println(
              "ALARM ACTIVE"
            );
          }

          else {
            display.println(
              "Control: ACTIVE"
            );
          }
        }

        else {

          display.println(
            "BED ELEVATION"
          );

          display.print("Mode: ");
          display.println(
            getBedModeName()
          );

          display.print("Target: ");
          display.print(
            data.bed.targetAngle
          );
          display.println(" deg");

          display.print("Angle: ");
          display.print(
            data.bed.currentAngle
          );
          display.println(" deg");

          display.print("Status: ");
          display.println(
            data.bed.moving ?
            "MOVING" :
            "STABLE"
          );
        }

        display.display();

        xSemaphoreGive(displayMutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ============================================================
// ALERT TASK
// ============================================================

void alertTask(void *param) {

  SystemData data;

  while (1) {

    taskHeartbeat[TASK_ALERT] =
      millis();

    if (
      xSemaphoreTake(
        alertSemaphore,
        pdMS_TO_TICKS(1000)
      ) == pdTRUE
    ) {

      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {

        data = systemData;

        xSemaphoreGive(dataMutex);
      }

      if (
        !digitalRead(SW_TEMP) &&
        data.medical.bodyTemperature >
        BODY_TEMP_HIGH
      ) {

        digitalWrite(
          BUZZER_PIN_1,
          HIGH
        );

        vTaskDelay(
          pdMS_TO_TICKS(100)
        );

        digitalWrite(
          BUZZER_PIN_1,
          LOW
        );
      }

      if (
        !digitalRead(SW_HR) &&
        (
          data.medical.heartRate <
          HEART_RATE_LOW ||
          data.medical.heartRate >
          HEART_RATE_HIGH
        )
      ) {

        digitalWrite(
          BUZZER_PIN_2,
          HIGH
        );

        vTaskDelay(
          pdMS_TO_TICKS(100)
        );

        digitalWrite(
          BUZZER_PIN_2,
          LOW
        );
      }

      if (
        !digitalRead(SW_SPO2) &&
        data.medical.spo2 <
        SPO2_LOW
      ) {

        digitalWrite(
          BUZZER_PIN_3,
          HIGH
        );

        vTaskDelay(
          pdMS_TO_TICKS(100)
        );

        digitalWrite(
          BUZZER_PIN_3,
          LOW
        );
      }

      if (
        data.facility.roomTemperature <
        ROOM_TEMP_LOW ||
        data.facility.roomTemperature >
        ROOM_TEMP_HIGH ||
        data.facility.oxygenLevel <
        OXYGEN_LOW ||
        data.facility.oxygenLevel >
        OXYGEN_HIGH ||
        data.facility.aqi >
        AQI_HIGH ||
        (
          data.motion &&
          !digitalRead(SW_MOTION)
        )
      ) {

        digitalWrite(
          BUZZER_PIN_4,
          HIGH
        );

        vTaskDelay(
          pdMS_TO_TICKS(100)
        );

        digitalWrite(
          BUZZER_PIN_4,
          LOW
        );
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ============================================================
// MQTT CONNECTION
// ============================================================

bool connectMQTT() {

  if (mqtt.connected()) {
    return true;
  }

  Serial.println(
    "MQTT: attempting connection..."
  );

  int8_t ret = mqtt.connect();

  if (ret != 0) {

    Serial.print(
      "MQTT connection failed: "
    );

    Serial.println(ret);

    mqtt.disconnect();

    return false;
  }

  Serial.println(
    "MQTT: connected!"
  );

  if (
    mqtt.subscribe(
      &medicationCurrentControlFeed
    )
  ) {
    Serial.println(
      "MQTT: subscribed to medication-current"
    );
  }

  if (
    mqtt.subscribe(
      &medicationAlertControlFeed
    )
  ) {
    Serial.println(
      "MQTT: subscribed to medication-alert"
    );
  }

  if (
    mqtt.subscribe(
      &bedControlFeed
    )
  ) {
    Serial.println(
      "MQTT: subscribed to bed-control"
    );
  }

  return true;
}

// ============================================================
// MQTT TASK
// ============================================================

void mqttTask(void *param) {

  SystemData data;
  uint32_t lastPublish = 0;

  while (1) {

    taskHeartbeat[TASK_MQTT] =
      millis();

    if (
      WiFi.status() ==
      WL_CONNECTED
    ) {

      if (!mqtt.connected()) {
        connectMQTT();
      }
    }

    // -------------------------
    // Publish monitoring feeds
    // -------------------------

    if (
      mqtt.connected() &&
      millis() - lastPublish >= 20000
    ) {

      lastPublish = millis();

      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {

        data = systemData;

        xSemaphoreGive(dataMutex);
      }

      medicalBodyTempFeed.publish(
        data.medical.bodyTemperature
      );

      medicalHRFeed.publish(
        (int32_t)data.medical.heartRate
      );

      medicalSpO2Feed.publish(
        (int32_t)data.medical.spo2
      );

      medicalAlertFeed.publish(
        data.medicalAlert
      );

      facilityRoomTempFeed.publish(
        data.facility.roomTemperature
      );

      facilityOxygenFeed.publish(
        data.facility.oxygenLevel
      );

      facilityAQIFeed.publish(
        (int32_t)data.facility.aqi
      );

      facilityAlertFeed.publish(
        data.facilityAlert
      );
    }

    // ========================================================
    // ONE MQTT SUBSCRIPTION LOOP
    // ========================================================

    if (mqtt.connected()) {

      Adafruit_MQTT_Subscribe *subscription;

      Serial.println("MQTT: checking for incoming commands...");

      while (
        (
          subscription =
          mqtt.readSubscription(100)
        )
      ) {

        Serial.println("MQTT: incoming message received!");

        // -------------------------
        // TASK 3 dosage
        // -------------------------

        if (
          subscription ==
          &medicationCurrentControlFeed
        ) {

          int requested = atoi(
            (char *)
            medicationCurrentControlFeed.lastread
          );

          requested = constrain(
            requested,
            MEDICATION_MIN_DOSAGE,
            MEDICATION_MAX_DOSAGE
          );

          Serial.print(
            "MQTT dosage request: "
          );

          Serial.print(requested);
          Serial.println(" mg/hr");

          requestMedicationDosage(
            requested
          );
        }

        // -------------------------
        // TASK 3 confirmation
        // -------------------------

        else if (
          subscription ==
          &medicationAlertControlFeed
        ) {

          String command = String(
            (char *)
            medicationAlertControlFeed.lastread
          );

          command.trim();
          command.toUpperCase();

          Serial.print(
            "MQTT confirmation: "
          );

          Serial.println(command);

          if (
            command == "CONFIRM" ||
            command == "YES" ||
            command == "1"
          ) {

            medicationCriticalConfirmed =
              true;

            medicationConfirmationRequired =
              false;

            Serial.println(
              "Medication CRITICAL dosage CONFIRMED"
            );

            requestMedicationDosage(
              medicationRequestedDosage
            );
          }
        }

        // ====================================================
        // TASK 4 BED CONTROL
        // ====================================================

        else if (
          subscription ==
          &bedControlFeed
        ) {

          String command = String(
            (char *)
            bedControlFeed.lastread
          );

          command.trim();

          String upperCommand = command;
          upperCommand.toUpperCase();

          // -------------------------
          // Numeric angle = MANUAL
          // -------------------------

          bool numeric = true;

          for (
            unsigned int i = 0;
            i < command.length();
            i++
          ) {

            if (
              !isDigit(command[i])
            ) {
              numeric = false;
              break;
            }
          }

          if (
            numeric &&
            command.length() > 0
          ) {

            int requestedAngle =
              command.toInt();

            requestedAngle = constrain(
              requestedAngle,
              BED_MIN_ANGLE,
              BED_MAX_ANGLE
            );

            bedRequestedAngle =
              requestedAngle;

            bedMode =
              BED_MANUAL;

            updateBedTarget();

            Serial.print(
              "BED: MANUAL angle request = "
            );

            Serial.print(
              requestedAngle
            );

            Serial.println(
              " degrees"
            );
          }

          // -------------------------
          // Sleeping
          // -------------------------

          else if (
            upperCommand == "SLEEP" ||
            upperCommand == "SLEEPING"
          ) {

            bedMode =
              BED_SLEEP;

            bedRequestedAngle =
              BED_SLEEP_ANGLE;

            updateBedTarget();

            Serial.println(
              "BED: SLEEPING mode -> 10 degrees"
            );
          }

          // -------------------------
          // Breathing support
          // -------------------------

          else if (
            upperCommand == "BREATHING" ||
            upperCommand ==
            "BREATHING SUPPORT"
          ) {

            bedMode =
              BED_BREATHING;

            bedRequestedAngle =
              BED_BREATHING_ANGLE;

            updateBedTarget();

            Serial.println(
              "BED: BREATHING SUPPORT mode -> 45 degrees"
            );
          }

          // -------------------------
          // Emergency
          // -------------------------

          else if (
            upperCommand == "EMERGENCY"
          ) {

            bedMode =
              BED_EMERGENCY;

            bedRequestedAngle =
              BED_EMERGENCY_ANGLE;

            updateBedTarget();

            Serial.println(
              "BED: EMERGENCY mode -> 90 degrees"
            );
          }

          // -------------------------
          // Automatic mode
          // -------------------------

          else if (
            upperCommand == "AUTO"
          ) {

            bedMode =
              BED_AUTO;

            updateBedTarget();

            Serial.println(
              "BED: AUTO mode enabled"
            );
          }

          else {

            Serial.print(
              "BED: unknown command: "
            );

            Serial.println(command);
          }
        }
      }
    }

    // -------------------------
    // Medication status
    // -------------------------

    static uint32_t lastMedicationAlertPublish =
      0;

    if (
      mqtt.connected() &&
      millis() -
      lastMedicationAlertPublish >=
      2000
    ) {

      lastMedicationAlertPublish =
        millis();

      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      ) {

        data = systemData;

        xSemaphoreGive(dataMutex);
      }

      char medicationMessage[160];

      snprintf(
        medicationMessage,
        sizeof(medicationMessage),
        "Status=%s | Request=%d mg/hr | Current=%d mg/hr | %s",
        data.medication.status,
        data.medication.requestedDosage,
        data.medication.currentDosage,
        data.medication.alert
      );

      medicationAlertFeed.publish(
        medicationMessage
      );
    }

    if (mqtt.connected()) {
      mqtt.ping();
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ============================================================
// WATCHDOG TASK
// ============================================================

void watchdogTask(void *param) {

  esp_task_wdt_add(NULL);

  const char *taskNames[TASK_COUNT] = {
    "MedicalSensorTask",
    "FacilitySensorTask",
    "ECGTask",
    "EnvironmentTask",
    "DataManagerTask",
    "DisplayTask",
    "AlertTask",
    "MQTTTask",
    "MedicationTask",
    "BedElevationTask"
  };

  Serial.println(
    "WatchdogTask: monitoring tasks"
  );

  while (1) {

    bool allTasksAlive = true;
    uint32_t currentTime =
      millis();

    for (
      int i = 0;
      i < TASK_COUNT;
      i++
    ) {

      uint32_t elapsed =
        currentTime -
        taskHeartbeat[i];

      if (
        elapsed >
        TASK_TIMEOUT_MS
      ) {

        allTasksAlive = false;

        Serial.print(
          "WATCHDOG WARNING: "
        );

        Serial.print(
          taskNames[i]
        );

        Serial.print(
          " not responding for "
        );

        Serial.print(
          elapsed
        );

        Serial.println(
          " ms"
        );
      }
    }

    if (allTasksAlive) {
      esp_task_wdt_reset();
    }
    else {
      Serial.println(
        "WATCHDOG FAILURE: system task stalled!"
      );
    }

    vTaskDelay(
      pdMS_TO_TICKS(
        WATCHDOG_CHECK_INTERVAL_MS
      )
    );
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println(
    "========================================"
  );
  Serial.println(
    "TASK 4 - INTELLIGENT BED ELEVATION"
  );
  Serial.println(
    "Task 2 + Task 3 + Task 4"
  );
  Serial.println(
    "========================================"
  );

  // -------------------------
  // Existing buzzers
  // -------------------------

  pinMode(BUZZER_PIN_1, OUTPUT);
  pinMode(BUZZER_PIN_2, OUTPUT);
  pinMode(BUZZER_PIN_3, OUTPUT);
  pinMode(BUZZER_PIN_4, OUTPUT);

  digitalWrite(BUZZER_PIN_1, LOW);
  digitalWrite(BUZZER_PIN_2, LOW);
  digitalWrite(BUZZER_PIN_3, LOW);
  digitalWrite(BUZZER_PIN_4, LOW);

  // -------------------------
  // Medication hardware
  // -------------------------

  pinMode(
    MEDICATION_LED_PIN,
    OUTPUT
  );

  pinMode(
    MEDICATION_BUZZER_PIN,
    OUTPUT
  );

  digitalWrite(
    MEDICATION_LED_PIN,
    LOW
  );

  digitalWrite(
    MEDICATION_BUZZER_PIN,
    LOW
  );

  // -------------------------
  // PIR
  // -------------------------

  pinMode(
    MOTION_DETECTOR,
    INPUT
  );

  // -------------------------
  // DIP switches
  // -------------------------

  pinMode(
    SW_TEMP,
    INPUT_PULLUP
  );

  pinMode(
    SW_HR,
    INPUT_PULLUP
  );

  pinMode(
    SW_SPO2,
    INPUT_PULLUP
  );

  pinMode(
    SW_MOTION,
    INPUT_PULLUP
  );

  analogReadResolution(12);

  // -------------------------
  // Temperature sensors
  // -------------------------

  bodyTempSensor.begin();
  roomTempSensor.begin();

  // -------------------------
  // OLED
  // -------------------------

  Wire.begin(21, 22);

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  ) {

    Serial.println(
      "OLED initialization FAILED!"
    );
  }
  else {

    Serial.println(
      "OLED initialized successfully."
    );

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(
      SSD1306_WHITE
    );
    display.setCursor(0, 0);

    display.println(
      "TASK 4 SMART BED"
    );

    display.println();
    display.println(
      "Medical"
    );
    display.println(
      "Facility"
    );
    display.println(
      "Medication"
    );
    display.println(
      "Bed Elevation"
    );

    display.println();
    display.println(
      "Starting..."
    );

    display.display();

    delay(1500);
  }

  // ==========================================================
  // TASK 4 SERVO
  // ==========================================================

  bedServo.setPeriodHertz(50);

  bedServo.attach(
    BED_SERVO_PIN,
    500,
    2400
  );

  bedCurrentAngle =
    BED_SLEEP_ANGLE;

  bedTargetAngle =
    BED_SLEEP_ANGLE;

  bedRequestedAngle =
    BED_SLEEP_ANGLE;

  bedServo.write(
    bedCurrentAngle
  );

  Serial.println(
    "Bed servo initialized on GPIO 13"
  );

  Serial.println(
    "Initial bed angle: 10 degrees"
  );

  // -------------------------
  // WiFi
  // -------------------------

  Serial.println(
    "Connecting to WiFi..."
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASS
  );

  int wifiAttempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    wifiAttempts < 30
  ) {

    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    Serial.println(
      "WiFi connected!"
    );

    Serial.print(
      "IP Address: "
    );

    Serial.println(
      WiFi.localIP()
    );
  }
  else {

    Serial.println(
      "WiFi connection failed!"
    );
  }

  // -------------------------
  // Queues
  // -------------------------

  medicalQueue =
    xQueueCreate(
      1,
      sizeof(MedicalData)
    );

  facilityQueue =
    xQueueCreate(
      1,
      sizeof(FacilityData)
    );

  environmentQueue =
    xQueueCreate(
      1,
      sizeof(bool)
    );

  ecgQueue =
    xQueueCreate(
      1,
      sizeof(int)
    );

  // -------------------------
  // Mutexes
  // -------------------------

  dataMutex =
    xSemaphoreCreateMutex();

  displayMutex =
    xSemaphoreCreateMutex();

  // -------------------------
  // Semaphores
  // -------------------------

  displaySemaphore =
    xSemaphoreCreateBinary();

  alertSemaphore =
    xSemaphoreCreateBinary();

  // -------------------------
  // Initial data
  // -------------------------

  systemData.medical.bodyTemperature =
    0.0;

  systemData.medical.heartRate =
    0;

  systemData.medical.spo2 =
    0;

  systemData.facility.roomTemperature =
    0.0;

  systemData.facility.oxygenLevel =
    0.0;

  systemData.facility.aqi =
    0;

  systemData.motion =
    false;

  systemData.ecg =
    0;

  strcpy(
    systemData.medicalAlert,
    "System Starting"
  );

  strcpy(
    systemData.facilityAlert,
    "System Starting"
  );

  // Medication
  systemData.medication.requestedDosage =
    0;

  systemData.medication.currentDosage =
    0;

  strcpy(
    systemData.medication.status,
    "NORMAL"
  );

  strcpy(
    systemData.medication.alert,
    "Dosage normal"
  );

  systemData.medication.confirmationRequired =
    false;

  systemData.medication.criticalConfirmed =
    false;

  medicationRequestedDosage =
    0;

  medicationCurrentDosage =
    0;

  medicationCriticalConfirmed =
    false;

  medicationConfirmationRequired =
    false;

  medicationLastIncrease =
    millis();

  medicationLastAlarm =
    millis();

  medicationAlarmState =
    false;

  // Bed
  systemData.bed.requestedAngle =
    BED_SLEEP_ANGLE;

  systemData.bed.targetAngle =
    BED_SLEEP_ANGLE;

  systemData.bed.currentAngle =
    BED_SLEEP_ANGLE;

  systemData.bed.mode =
    BED_MANUAL;

  systemData.bed.moving =
    false;

  bedMode =
    BED_MANUAL;

  bedLastStep =
    millis();

  // -------------------------
  // Watchdog heartbeat
  // -------------------------

  uint32_t startTime =
    millis();

  for (
    int i = 0;
    i < TASK_COUNT;
    i++
  ) {
    taskHeartbeat[i] =
      startTime;
  }

  // ==========================================================
  // CREATE TASKS
  // ==========================================================

  xTaskCreatePinnedToCore(
    medicalSensorTask,
    "MedicalSensorTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    facilitySensorTask,
    "FacilitySensorTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    ecgTask,
    "ECGTask",
    4096,
    NULL,
    3,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    environmentTask,
    "EnvironmentTask",
    2048,
    NULL,
    3,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    dataManagerTask,
    "DataManagerTask",
    4096,
    NULL,
    4,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    displayTask,
    "DisplayTask",
    4096,
    NULL,
    2,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    alertTask,
    "AlertTask",
    2048,
    NULL,
    5,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    mqttTask,
    "MQTTTask",
    8192,
    NULL,
    1,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    medicationTask,
    "MedicationTask",
    4096,
    NULL,
    5,
    NULL,
    1
  );

  // ==========================================================
  // TASK 4 BED TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    bedElevationTask,
    "BedElevationTask",
    4096,
    NULL,
    4,
    NULL,
    1
  );

  // ==========================================================
  // WATCHDOG
  // ==========================================================

  xTaskCreatePinnedToCore(
    watchdogTask,
    "WatchdogTask",
    4096,
    NULL,
    6,
    NULL,
    0
  );

  Serial.println();
  Serial.println(
    "========================================"
  );
  Serial.println(
    "FreeRTOS tasks started"
  );
  Serial.println(
    "========================================"
  );

  Serial.println(
    "MedicalSensorTask   : Core 1"
  );

  Serial.println(
    "FacilitySensorTask  : Core 1"
  );

  Serial.println(
    "ECGTask             : Core 1"
  );

  Serial.println(
    "EnvironmentTask     : Core 1"
  );

  Serial.println(
    "DataManagerTask     : Core 1"
  );

  Serial.println(
    "DisplayTask         : Core 0"
  );

  Serial.println(
    "AlertTask           : Core 1"
  );

  Serial.println(
    "MQTTTask            : Core 0"
  );

  Serial.println(
    "MedicationTask      : Core 1"
  );

  Serial.println(
    "BedElevationTask    : Core 1"
  );

  Serial.println(
    "WatchdogTask        : Core 0"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "Task 4 smart bed system running..."
  );

  Serial.println(
    "========================================"
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  vTaskDelay(portMAX_DELAY);
}
