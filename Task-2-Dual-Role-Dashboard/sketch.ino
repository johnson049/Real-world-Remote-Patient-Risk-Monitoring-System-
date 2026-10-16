#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_task_wdt.h"


// ============================================================
// TASK 2
// DUAL-ROLE IoT MONITORING SYSTEM
//
// MEDICAL STAFF DASHBOARD
//   - Body Temperature
//   - Heart Rate
//   - SpO2
//
// FACILITY MANAGEMENT DASHBOARD
//   - Room Temperature
//   - Oxygen Level
//   - AQI
//
// Additional:
//   - Separate MQTT topics
//   - Separate alerts
//   - Periodic aggregation
//   - FreeRTOS multitasking
//   - Watchdog monitoring
// ============================================================


// ============================================================
// PIN DEFINITIONS
// ============================================================

// ---------------------------
// Medical / Existing Sensors
// ---------------------------

#define BODY_TEMP_SENSOR   14
#define ECG_SENSOR         34
#define MOTION_DETECTOR    12

// ---------------------------
// Facility Sensors
// ---------------------------

#define ROOM_TEMP_SENSOR   27

// Potentiometers used as
// simulated facility sensors
#define OXYGEN_SENSOR      35
#define AQI_SENSOR         39

// ---------------------------
// Buzzers
// ---------------------------

#define BUZZER_PIN_1       4
#define BUZZER_PIN_2       16
#define BUZZER_PIN_3       17
#define BUZZER_PIN_4       5

// ---------------------------
// DIP Switches
// ---------------------------

#define SW_TEMP            32
#define SW_HR              33
#define SW_SPO2            25
#define SW_MOTION          26


// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH       128
#define SCREEN_HEIGHT      64
#define OLED_RESET         -1
#define OLED_ADDRESS       0x3C


// ============================================================
// WIFI
// ============================================================

#define WIFI_SSID          "Wokwi-GUEST"
#define WIFI_PASS          ""


// ============================================================
// ADAFRUIT IO
// ============================================================

#define IO_USERNAME        "jssg1234"

// IMPORTANT:
// Put your Adafruit IO key here locally.
// DO NOT publish your real key to GitHub.
//
// Example:
// #define IO_KEY "aio_xxxxxxxxxxxxxxxxx"
//
#define IO_KEY             "aio_KEY"

#define AIO_SERVER         "io.adafruit.com"
#define AIO_SERVERPORT     1883


// ============================================================
// TASK 2 MQTT FEEDS
// ============================================================

// ------------------------------------------------------------
// MEDICAL STAFF DASHBOARD
// ------------------------------------------------------------

#define MEDICAL_BODY_TEMP_FEED \
  IO_USERNAME "/feeds/medical-body-temperature"

#define MEDICAL_HR_FEED \
  IO_USERNAME "/feeds/medical-heart-rate"

#define MEDICAL_SPO2_FEED \
  IO_USERNAME "/feeds/medical-spo2"

#define MEDICAL_ALERT_FEED \
  IO_USERNAME "/feeds/medical-alert"

#define MEDICAL_SUMMARY_FEED \
  IO_USERNAME "/feeds/medical-summary"


// ------------------------------------------------------------
// FACILITY MANAGEMENT DASHBOARD
// ------------------------------------------------------------

#define FACILITY_ROOM_TEMP_FEED \
  IO_USERNAME "/feeds/facility-room-temperature"

#define FACILITY_OXYGEN_FEED \
  IO_USERNAME "/feeds/facility-oxygen-level"

#define FACILITY_AQI_FEED \
  IO_USERNAME "/feeds/facility-aqi"

#define FACILITY_ALERT_FEED \
  IO_USERNAME "/feeds/facility-alert"

#define FACILITY_SUMMARY_FEED \
  IO_USERNAME "/feeds/facility-summary"


// ------------------------------------------------------------
// Existing Task 1 feeds
// Kept for continuity / optional monitoring
// ------------------------------------------------------------

#define MOTION_FEED \
  IO_USERNAME "/feeds/motion-detector"

#define ECG_FEED \
  IO_USERNAME "/feeds/ecg-signal"


// ============================================================
// WIFI / MQTT OBJECTS
// ============================================================

WiFiClient client;

Adafruit_MQTT_Client mqtt(
  &client,
  AIO_SERVER,
  AIO_SERVERPORT,
  IO_USERNAME,
  IO_KEY
);


// ============================================================
// MQTT PUBLISH OBJECTS
// ============================================================

// ---------------------------
// Medical
// ---------------------------

Adafruit_MQTT_Publish medicalBodyTempFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    MEDICAL_BODY_TEMP_FEED
  );

Adafruit_MQTT_Publish medicalHRFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    MEDICAL_HR_FEED
  );

Adafruit_MQTT_Publish medicalSpO2Feed =
  Adafruit_MQTT_Publish(
    &mqtt,
    MEDICAL_SPO2_FEED
  );

Adafruit_MQTT_Publish medicalAlertFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    MEDICAL_ALERT_FEED
  );

Adafruit_MQTT_Publish medicalSummaryFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    MEDICAL_SUMMARY_FEED
  );


// ---------------------------
// Facility
// ---------------------------

Adafruit_MQTT_Publish facilityRoomTempFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    FACILITY_ROOM_TEMP_FEED
  );

Adafruit_MQTT_Publish facilityOxygenFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    FACILITY_OXYGEN_FEED
  );

Adafruit_MQTT_Publish facilityAQIFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    FACILITY_AQI_FEED
  );

Adafruit_MQTT_Publish facilityAlertFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    FACILITY_ALERT_FEED
  );

Adafruit_MQTT_Publish facilitySummaryFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    FACILITY_SUMMARY_FEED
  );


// ---------------------------
// Existing Task 1 signals
// ---------------------------

Adafruit_MQTT_Publish motionFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    MOTION_FEED
  );

Adafruit_MQTT_Publish ecgFeed =
  Adafruit_MQTT_Publish(
    &mqtt,
    ECG_FEED
  );


// ============================================================
// SENSOR OBJECTS
// ============================================================

OneWire bodyTempWire(BODY_TEMP_SENSOR);

DallasTemperature bodyTempSensor(
  &bodyTempWire
);


OneWire roomTempWire(ROOM_TEMP_SENSOR);

DallasTemperature roomTempSensor(
  &roomTempWire
);


// ============================================================
// OLED OBJECT
// ============================================================

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);


// ============================================================
// MEDICAL DATA
// ============================================================

struct MedicalData
{
  float bodyTemperature;

  int heartRate;

  int spo2;
};


// ============================================================
// FACILITY DATA
// ============================================================

struct FacilityData
{
  float roomTemperature;

  float oxygenLevel;

  int aqi;
};


// ============================================================
// COMPLETE SYSTEM DATA
// ============================================================

struct SystemData
{
  MedicalData medical;

  FacilityData facility;

  bool motion;

  int ecg;

  char medicalAlert[64];

  char facilityAlert[64];
};


SystemData systemData;


// ============================================================
// AGGREGATION DATA
// ============================================================

struct AggregationData
{
  // Medical
  float bodyTempSum;
  float bodyTempMin;
  float bodyTempMax;

  float heartRateSum;
  int heartRateMin;
  int heartRateMax;

  float spo2Sum;
  int spo2Min;
  int spo2Max;

  // Facility
  float roomTempSum;
  float roomTempMin;
  float roomTempMax;

  float oxygenSum;
  float oxygenMin;
  float oxygenMax;

  long aqiSum;
  int aqiMin;
  int aqiMax;

  // Counters
  uint32_t medicalSamples;
  uint32_t facilitySamples;

  uint32_t medicalAbnormalEvents;
  uint32_t facilityAbnormalEvents;

  uint32_t lastAggregation;
};


AggregationData aggregation;


// ============================================================
// THRESHOLDS
//
// These are simulation / project thresholds.
// Document them as engineering assumptions in the report.
// ============================================================

// ---------------------------
// Medical
// ---------------------------

#define BODY_TEMP_HIGH       38.0

#define HEART_RATE_LOW       60
#define HEART_RATE_HIGH      300

#define SPO2_LOW             85


// ---------------------------
// Facility
// ---------------------------

#define ROOM_TEMP_LOW        18.0
#define ROOM_TEMP_HIGH       30.0

#define OXYGEN_LOW           19.5
#define OXYGEN_HIGH          23.5

#define AQI_HIGH              100


// ============================================================
// AGGREGATION PERIOD
// ============================================================

#define AGGREGATION_INTERVAL_MS 30000


// ============================================================
// WATCHDOG
// ============================================================
//
// 8 monitored application tasks:
//
// 0 MedicalSensorTask
// 1 FacilitySensorTask
// 2 ECGTask
// 3 EnvironmentTask
// 4 DataManagerTask
// 5 DisplayTask
// 6 AlertTask
// 7 MQTTTask
//
// WatchdogTask monitors them.
// ============================================================

#define TASK_COUNT 8

#define WATCHDOG_CHECK_INTERVAL_MS 1000

#define TASK_TIMEOUT_MS 4000


enum TaskID
{
  TASK_MEDICAL_SENSOR = 0,

  TASK_FACILITY_SENSOR,

  TASK_ECG,

  TASK_ENVIRONMENT,

  TASK_DATA_MANAGER,

  TASK_DISPLAY,

  TASK_ALERT,

  TASK_MQTT
};


volatile uint32_t taskHeartbeat[TASK_COUNT] = {0};


// ============================================================
// QUEUES
// ============================================================

QueueHandle_t medicalQueue;

QueueHandle_t facilityQueue;

QueueHandle_t environmentQueue;

QueueHandle_t ecgQueue;


// ============================================================
// MUTEXES
// ============================================================

SemaphoreHandle_t dataMutex;

SemaphoreHandle_t displayMutex;


// ============================================================
// SEMAPHORES
// ============================================================

SemaphoreHandle_t displaySemaphore;

SemaphoreHandle_t alertSemaphore;

SemaphoreHandle_t mqttSemaphore;


// ============================================================
// DISPLAY MODE
// ============================================================

volatile bool showMedicalDashboard = true;


// ============================================================
// SIMULATED OXYGEN SENSOR
// ============================================================
//
// ADC 0-4095
// mapped to 18.0% - 25.0% oxygen
//
// This is a simulated facility sensor using a
// potentiometer because Wokwi does not provide
// a native oxygen sensor for this setup.
// ============================================================

float readOxygenLevel()
{
  int rawValue = analogRead(OXYGEN_SENSOR);

  float oxygen =
    18.0 +
    ((float)rawValue / 4095.0) * 7.0;

  return oxygen;
}


// ============================================================
// SIMULATED AQI SENSOR
// ============================================================
//
// ADC 0-4095
// mapped to AQI 0-500
// ============================================================

int readAQI()
{
  int rawValue = analogRead(AQI_SENSOR);

  int aqi = map(
    rawValue,
    0,
    4095,
    0,
    500
  );

  return aqi;
}


// ============================================================
// MEDICAL SENSOR TASK
// ============================================================

void medicalSensorTask(void *param)
{
  MedicalData data;

  while (1)
  {
    taskHeartbeat[TASK_MEDICAL_SENSOR] =
      millis();


    // --------------------------------------------------------
    // BODY TEMPERATURE
    // --------------------------------------------------------

    data.bodyTemperature = 0.0;

    if (!digitalRead(SW_TEMP))
    {
      bodyTempSensor.requestTemperatures();

      data.bodyTemperature =
        bodyTempSensor.getTempCByIndex(0);
    }


    // --------------------------------------------------------
    // HEART RATE
    // --------------------------------------------------------

    if (!digitalRead(SW_HR))
    {
      data.heartRate =
        random(45, 351);
    }
    else
    {
      data.heartRate = 0;
    }


    // --------------------------------------------------------
    // SPO2
    // --------------------------------------------------------

    if (!digitalRead(SW_SPO2))
    {
      data.spo2 =
        random(60, 101);
    }
    else
    {
      data.spo2 = 0;
    }


    // --------------------------------------------------------
    // SEND TO DATA MANAGER
    // --------------------------------------------------------

    xQueueOverwrite(
      medicalQueue,
      &data
    );


    Serial.println(
      "MedicalSensorTask: data updated"
    );


    vTaskDelay(
      pdMS_TO_TICKS(2000)
    );
  }
}


// ============================================================
// FACILITY SENSOR TASK
// ============================================================

void facilitySensorTask(void *param)
{
  FacilityData data;

  while (1)
  {
    taskHeartbeat[TASK_FACILITY_SENSOR] =
      millis();


    // --------------------------------------------------------
    // ROOM TEMPERATURE
    // --------------------------------------------------------

    roomTempSensor.requestTemperatures();

    data.roomTemperature =
      roomTempSensor.getTempCByIndex(0);


    // --------------------------------------------------------
    // OXYGEN
    // --------------------------------------------------------

    data.oxygenLevel =
      readOxygenLevel();


    // --------------------------------------------------------
    // AQI
    // --------------------------------------------------------

    data.aqi =
      readAQI();


    // --------------------------------------------------------
    // SEND TO DATA MANAGER
    // --------------------------------------------------------

    xQueueOverwrite(
      facilityQueue,
      &data
    );


    Serial.print(
      "FacilitySensorTask: Room="
    );

    Serial.print(
      data.roomTemperature
    );

    Serial.print(
      " C, O2="
    );

    Serial.print(
      data.oxygenLevel
    );

    Serial.print(
      " %, AQI="
    );

    Serial.println(
      data.aqi
    );


    vTaskDelay(
      pdMS_TO_TICKS(2000)
    );
  }
}


// ============================================================
// ENVIRONMENT / PIR TASK
// ============================================================

void environmentTask(void *param)
{
  bool motion;

  while (1)
  {
    taskHeartbeat[TASK_ENVIRONMENT] =
      millis();


    motion =
      digitalRead(MOTION_DETECTOR);


    xQueueOverwrite(
      environmentQueue,
      &motion
    );


    vTaskDelay(
      pdMS_TO_TICKS(500)
    );
  }
}


// ============================================================
// ECG TASK
// ============================================================

void ecgTask(void *param)
{
  int ecgValue;

  int printCounter = 0;


  while (1)
  {
    taskHeartbeat[TASK_ECG] =
      millis();


    ecgValue =
      analogRead(ECG_SENSOR);


    xQueueOverwrite(
      ecgQueue,
      &ecgValue
    );


    printCounter++;


    if (printCounter >= 50)
    {
      Serial.print(
        "ECGTask: ECG = "
      );

      Serial.println(
        ecgValue
      );

      printCounter = 0;
    }


    // 100 Hz
    vTaskDelay(
      pdMS_TO_TICKS(10)
    );
  }
}


// ============================================================
// CHECK MEDICAL ALERT
// ============================================================

void updateMedicalAlert()
{
  strcpy(
    systemData.medicalAlert,
    "OK"
  );


  // Body temperature
  if (
    !digitalRead(SW_TEMP) &&
    systemData.medical.bodyTemperature >
      BODY_TEMP_HIGH
  )
  {
    strcpy(
      systemData.medicalAlert,
      "High body temperature"
    );

    return;
  }


  // Heart rate low
  if (
    !digitalRead(SW_HR) &&
    systemData.medical.heartRate <
      HEART_RATE_LOW
  )
  {
    strcpy(
      systemData.medicalAlert,
      "HR below normal"
    );

    return;
  }


  // Heart rate high
  if (
    !digitalRead(SW_HR) &&
    systemData.medical.heartRate >
      HEART_RATE_HIGH
  )
  {
    strcpy(
      systemData.medicalAlert,
      "HR above normal"
    );

    return;
  }


  // SpO2
  if (
    !digitalRead(SW_SPO2) &&
    systemData.medical.spo2 <
      SPO2_LOW
  )
  {
    strcpy(
      systemData.medicalAlert,
      "SpO2 below normal"
    );

    return;
  }


  // Motion
  if (
    systemData.motion &&
    !digitalRead(SW_MOTION)
  )
  {
    strcpy(
      systemData.medicalAlert,
      "Motion detected"
    );

    return;
  }
}


// ============================================================
// CHECK FACILITY ALERT
// ============================================================

void updateFacilityAlert()
{
  strcpy(
    systemData.facilityAlert,
    "OK"
  );


  // Room temperature
  if (
    systemData.facility.roomTemperature <
      ROOM_TEMP_LOW
  )
  {
    strcpy(
      systemData.facilityAlert,
      "Room temperature low"
    );

    return;
  }


  if (
    systemData.facility.roomTemperature >
      ROOM_TEMP_HIGH
  )
  {
    strcpy(
      systemData.facilityAlert,
      "Room temperature high"
    );

    return;
  }


  // Oxygen
  if (
    systemData.facility.oxygenLevel <
      OXYGEN_LOW
  )
  {
    strcpy(
      systemData.facilityAlert,
      "Oxygen level low"
    );

    return;
  }


  if (
    systemData.facility.oxygenLevel >
      OXYGEN_HIGH
  )
  {
    strcpy(
      systemData.facilityAlert,
      "Oxygen level high"
    );

    return;
  }


  // AQI
  if (
    systemData.facility.aqi >
      AQI_HIGH
  )
  {
    strcpy(
      systemData.facilityAlert,
      "AQI above safe level"
    );

    return;
  }
}


// ============================================================
// UPDATE MEDICAL AGGREGATION
// ============================================================

void updateMedicalAggregation(
  const MedicalData &data
)
{
  aggregation.bodyTempSum +=
    data.bodyTemperature;

  aggregation.heartRateSum +=
    data.heartRate;

  aggregation.spo2Sum +=
    data.spo2;


  if (
    aggregation.medicalSamples == 0
  )
  {
    aggregation.bodyTempMin =
      data.bodyTemperature;

    aggregation.bodyTempMax =
      data.bodyTemperature;

    aggregation.heartRateMin =
      data.heartRate;

    aggregation.heartRateMax =
      data.heartRate;

    aggregation.spo2Min =
      data.spo2;

    aggregation.spo2Max =
      data.spo2;
  }
  else
  {
    if (
      data.bodyTemperature <
      aggregation.bodyTempMin
    )
    {
      aggregation.bodyTempMin =
        data.bodyTemperature;
    }


    if (
      data.bodyTemperature >
      aggregation.bodyTempMax
    )
    {
      aggregation.bodyTempMax =
        data.bodyTemperature;
    }


    if (
      data.heartRate <
      aggregation.heartRateMin
    )
    {
      aggregation.heartRateMin =
        data.heartRate;
    }


    if (
      data.heartRate >
      aggregation.heartRateMax
    )
    {
      aggregation.heartRateMax =
        data.heartRate;
    }


    if (
      data.spo2 <
      aggregation.spo2Min
    )
    {
      aggregation.spo2Min =
        data.spo2;
    }


    if (
      data.spo2 >
      aggregation.spo2Max
    )
    {
      aggregation.spo2Max =
        data.spo2;
    }
  }


  aggregation.medicalSamples++;


  if (
    strcmp(
      systemData.medicalAlert,
      "OK"
    ) != 0
  )
  {
    aggregation.medicalAbnormalEvents++;
  }
}


// ============================================================
// UPDATE FACILITY AGGREGATION
// ============================================================

void updateFacilityAggregation(
  const FacilityData &data
)
{
  aggregation.roomTempSum +=
    data.roomTemperature;

  aggregation.oxygenSum +=
    data.oxygenLevel;

  aggregation.aqiSum +=
    data.aqi;


  if (
    aggregation.facilitySamples == 0
  )
  {
    aggregation.roomTempMin =
      data.roomTemperature;

    aggregation.roomTempMax =
      data.roomTemperature;

    aggregation.oxygenMin =
      data.oxygenLevel;

    aggregation.oxygenMax =
      data.oxygenLevel;

    aggregation.aqiMin =
      data.aqi;

    aggregation.aqiMax =
      data.aqi;
  }
  else
  {
    if (
      data.roomTemperature <
      aggregation.roomTempMin
    )
    {
      aggregation.roomTempMin =
        data.roomTemperature;
    }


    if (
      data.roomTemperature >
      aggregation.roomTempMax
    )
    {
      aggregation.roomTempMax =
        data.roomTemperature;
    }


    if (
      data.oxygenLevel <
      aggregation.oxygenMin
    )
    {
      aggregation.oxygenMin =
        data.oxygenLevel;
    }


    if (
      data.oxygenLevel >
      aggregation.oxygenMax
    )
    {
      aggregation.oxygenMax =
        data.oxygenLevel;
    }


    if (
      data.aqi <
      aggregation.aqiMin
    )
    {
      aggregation.aqiMin =
        data.aqi;
    }


    if (
      data.aqi >
      aggregation.aqiMax
    )
    {
      aggregation.aqiMax =
        data.aqi;
    }
  }


  aggregation.facilitySamples++;


  if (
    strcmp(
      systemData.facilityAlert,
      "OK"
    ) != 0
  )
  {
    aggregation.facilityAbnormalEvents++;
  }
}


// ============================================================
// DATA MANAGER TASK
// ============================================================

void dataManagerTask(void *param)
{
  MedicalData medicalData;

  FacilityData facilityData;

  bool motion;

  int ecgValue;


  while (1)
  {
    taskHeartbeat[TASK_DATA_MANAGER] =
      millis();


    // --------------------------------------------------------
    // MEDICAL DATA
    // --------------------------------------------------------

    if (
      xQueueReceive(
        medicalQueue,
        &medicalData,
        0
      ) == pdPASS
    )
    {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        systemData.medical =
          medicalData;


        xSemaphoreGive(
          dataMutex
        );
      }
    }


    // --------------------------------------------------------
    // FACILITY DATA
    // --------------------------------------------------------

    if (
      xQueueReceive(
        facilityQueue,
        &facilityData,
        0
      ) == pdPASS
    )
    {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        systemData.facility =
          facilityData;


        xSemaphoreGive(
          dataMutex
        );
      }
    }


    // --------------------------------------------------------
    // MOTION
    // --------------------------------------------------------

    if (
      xQueueReceive(
        environmentQueue,
        &motion,
        0
      ) == pdPASS
    )
    {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        systemData.motion =
          motion;


        xSemaphoreGive(
          dataMutex
        );
      }
    }


    // --------------------------------------------------------
    // ECG
    // --------------------------------------------------------

    if (
      xQueueReceive(
        ecgQueue,
        &ecgValue,
        0
      ) == pdPASS
    )
    {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        systemData.ecg =
          ecgValue;


        xSemaphoreGive(
          dataMutex
        );
      }
    }


    // --------------------------------------------------------
    // ALERT CALCULATION
    // --------------------------------------------------------

    if (
      xSemaphoreTake(
        dataMutex,
        pdMS_TO_TICKS(100)
      ) == pdTRUE
    )
    {
      updateMedicalAlert();

      updateFacilityAlert();


      xSemaphoreGive(
        dataMutex
      );
    }


    // --------------------------------------------------------
    // AGGREGATION
    //
    // Add latest medical/facility values when they
    // are available.
    // --------------------------------------------------------

    if (
      xSemaphoreTake(
        dataMutex,
        pdMS_TO_TICKS(100)
      ) == pdTRUE
    )
    {
      if (
        systemData.medical.bodyTemperature != 0.0
      )
      {
        static float lastBodyTemp = -999;

        if (
          systemData.medical.bodyTemperature !=
          lastBodyTemp
        )
        {
          updateMedicalAggregation(
            systemData.medical
          );

          lastBodyTemp =
            systemData.medical.bodyTemperature;
        }
      }


      static float lastRoomTemp = -999;

      if (
        systemData.facility.roomTemperature !=
        lastRoomTemp
      )
      {
        updateFacilityAggregation(
          systemData.facility
        );

        lastRoomTemp =
          systemData.facility.roomTemperature;
      }


      xSemaphoreGive(
        dataMutex
      );
    }


    // --------------------------------------------------------
    // NOTIFY OTHER TASKS
    // --------------------------------------------------------

    xSemaphoreGive(
      displaySemaphore
    );

    xSemaphoreGive(
      alertSemaphore
    );

    xSemaphoreGive(
      mqttSemaphore
    );


    // --------------------------------------------------------
    // 30-SECOND AGGREGATION FLAG
    // --------------------------------------------------------

    if (
      millis() -
      aggregation.lastAggregation >=
      AGGREGATION_INTERVAL_MS
    )
    {
      xSemaphoreGive(
        mqttSemaphore
      );
    }


    vTaskDelay(
      pdMS_TO_TICKS(100)
    );
  }
}


// ============================================================
// DISPLAY TASK
// ============================================================

void displayTask(void *param)
{
  SystemData data;

  uint32_t lastScreenChange = 0;


  while (1)
  {
    taskHeartbeat[TASK_DISPLAY] =
      millis();


    // --------------------------------------------------------
    // Switch dashboard every 5 seconds
    // --------------------------------------------------------

    if (
      millis() -
      lastScreenChange >= 5000
    )
    {
      showMedicalDashboard =
        !showMedicalDashboard;

      lastScreenChange =
        millis();
    }


    if (
      xSemaphoreTake(
        displaySemaphore,
        pdMS_TO_TICKS(1000)
      ) == pdTRUE
    )
    {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        data =
          systemData;

        xSemaphoreGive(
          dataMutex
        );
      }


      if (
        xSemaphoreTake(
          displayMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        display.clearDisplay();

        display.setTextSize(1);

        display.setTextColor(
          SSD1306_WHITE
        );

        display.setCursor(0, 0);


        // ==================================================
        // MEDICAL DASHBOARD
        // ==================================================

        if (showMedicalDashboard)
        {
          display.println(
            "MEDICAL DASHBOARD"
          );

          display.print(
            "Body: "
          );

          display.print(
            data.medical.bodyTemperature,
            1
          );

          display.println(
            " C"
          );


          display.print(
            "HR: "
          );

          display.println(
            data.medical.heartRate
          );


          display.print(
            "SpO2: "
          );

          display.print(
            data.medical.spo2
          );

          display.println(
            " %"
          );


          display.print(
            "ECG: "
          );

          display.println(
            data.ecg
          );


          display.print(
            "Alert: "
          );

          display.println(
            data.medicalAlert
          );
        }


        // ==================================================
        // FACILITY DASHBOARD
        // ==================================================

        else
        {
          display.println(
            "FACILITY DASHBOARD"
          );


          display.print(
            "Room: "
          );

          display.print(
            data.facility.roomTemperature,
            1
          );

          display.println(
            " C"
          );


          display.print(
            "O2: "
          );

          display.print(
            data.facility.oxygenLevel,
            1
          );

          display.println(
            " %"
          );


          display.print(
            "AQI: "
          );

          display.println(
            data.facility.aqi
          );


          display.print(
            "Motion: "
          );

          display.println(
            data.motion ?
            "YES" :
            "NO"
          );


          display.print(
            "Alert: "
          );

          display.println(
            data.facilityAlert
          );
        }


        display.display();


        xSemaphoreGive(
          displayMutex
        );
      }
    }


    vTaskDelay(
      pdMS_TO_TICKS(100)
    );
  }
}


// ============================================================
// ALERT TASK
// ============================================================

void alertTask(void *param)
{
  SystemData data;


  while (1)
  {
    taskHeartbeat[TASK_ALERT] =
      millis();


    if (
      xSemaphoreTake(
        alertSemaphore,
        pdMS_TO_TICKS(1000)
      ) == pdTRUE
    )
    {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        data =
          systemData;

        xSemaphoreGive(
          dataMutex
        );
      }


      // ======================================================
      // MEDICAL ALERTS
      // ======================================================

      if (
        !digitalRead(SW_TEMP) &&
        data.medical.bodyTemperature >
        BODY_TEMP_HIGH
      )
      {
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
      )
      {
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
      )
      {
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


      // ======================================================
      // FACILITY ALERT
      // Buzzer 4
      // ======================================================

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
        AQI_HIGH
      )
      {
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


      // Existing motion alert
      if (
        data.motion &&
        !digitalRead(SW_MOTION)
      )
      {
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


    vTaskDelay(
      pdMS_TO_TICKS(100)
    );
  }
}


// ============================================================
// MQTT CONNECTION
// ============================================================

void connectMQTT()
{
  if (mqtt.connected())
  {
    return;
  }


  Serial.println();

  Serial.println(
    "MQTT: attempting connection..."
  );


  int8_t ret =
    mqtt.connect();


  if (ret == 0)
  {
    Serial.println(
      "MQTT: connected!"
    );
  }
  else
  {
    Serial.print(
      "MQTT connection failed: "
    );

    Serial.println(
      ret
    );


    mqtt.disconnect();


    vTaskDelay(
      pdMS_TO_TICKS(3000)
    );
  }
}


// ============================================================
// BUILD MEDICAL SUMMARY
// ============================================================

void publishMedicalSummary()
{
  if (
    aggregation.medicalSamples == 0
  )
  {
    return;
  }


  float avgTemp =
    aggregation.bodyTempSum /
    aggregation.medicalSamples;


  float avgHR =
    aggregation.heartRateSum /
    aggregation.medicalSamples;


  float avgSpO2 =
    aggregation.spo2Sum /
    aggregation.medicalSamples;


  char summary[256];


  snprintf(
    summary,
    sizeof(summary),

    "Medical 30s | "
    "Temp Avg=%.1f Min=%.1f Max=%.1f | "
    "HR Avg=%.1f Min=%d Max=%d | "
    "SpO2 Avg=%.1f Min=%d Max=%d | "
    "Abnormal=%lu",

    avgTemp,

    aggregation.bodyTempMin,

    aggregation.bodyTempMax,

    avgHR,

    aggregation.heartRateMin,

    aggregation.heartRateMax,

    avgSpO2,

    aggregation.spo2Min,

    aggregation.spo2Max,

    (unsigned long)
    aggregation.medicalAbnormalEvents
  );


  medicalSummaryFeed.publish(
    summary
  );


  Serial.println(
    "Medical summary published"
  );
}


// ============================================================
// BUILD FACILITY SUMMARY
// ============================================================

void publishFacilitySummary()
{
  if (
    aggregation.facilitySamples == 0
  )
  {
    return;
  }


  float avgRoomTemp =
    aggregation.roomTempSum /
    aggregation.facilitySamples;


  float avgOxygen =
    aggregation.oxygenSum /
    aggregation.facilitySamples;


  float avgAQI =
    (float)aggregation.aqiSum /
    aggregation.facilitySamples;


  char summary[256];


  snprintf(
    summary,
    sizeof(summary),

    "Facility 30s | "
    "Room Avg=%.1f Min=%.1f Max=%.1f | "
    "O2 Avg=%.1f Min=%.1f Max=%.1f | "
    "AQI Avg=%.1f Min=%d Max=%d | "
    "Abnormal=%lu",

    avgRoomTemp,

    aggregation.roomTempMin,

    aggregation.roomTempMax,

    avgOxygen,

    aggregation.oxygenMin,

    aggregation.oxygenMax,

    avgAQI,

    aggregation.aqiMin,

    aggregation.aqiMax,

    (unsigned long)
    aggregation.facilityAbnormalEvents
  );


  facilitySummaryFeed.publish(
    summary
  );


  Serial.println(
    "Facility summary published"
  );
}


// ============================================================
// RESET AGGREGATION
// ============================================================

void resetAggregation()
{
  aggregation.bodyTempSum = 0;

  aggregation.bodyTempMin = 0;

  aggregation.bodyTempMax = 0;


  aggregation.heartRateSum = 0;

  aggregation.heartRateMin = 0;

  aggregation.heartRateMax = 0;


  aggregation.spo2Sum = 0;

  aggregation.spo2Min = 0;

  aggregation.spo2Max = 0;


  aggregation.roomTempSum = 0;

  aggregation.roomTempMin = 0;

  aggregation.roomTempMax = 0;


  aggregation.oxygenSum = 0;

  aggregation.oxygenMin = 0;

  aggregation.oxygenMax = 0;


  aggregation.aqiSum = 0;

  aggregation.aqiMin = 0;

  aggregation.aqiMax = 0;


  aggregation.medicalSamples = 0;

  aggregation.facilitySamples = 0;


  aggregation.medicalAbnormalEvents = 0;

  aggregation.facilityAbnormalEvents = 0;


  aggregation.lastAggregation =
    millis();
}


// ============================================================
// MQTT TASK
// ============================================================

void mqttTask(void *param)
{
  SystemData data;

  uint32_t lastPublish = 0;


  while (1)
  {
    taskHeartbeat[TASK_MQTT] =
      millis();


    // --------------------------------------------------------
    // Maintain MQTT connection
    // --------------------------------------------------------

    if (
      WiFi.status() ==
      WL_CONNECTED
    )
    {
      if (!mqtt.connected())
      {
        connectMQTT();
      }
    }


    // --------------------------------------------------------
    // Publish every 5 seconds
    // --------------------------------------------------------

    if (
      mqtt.connected() &&
      millis() - lastPublish >= 5000
    )
    {
      lastPublish =
        millis();


      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        data =
          systemData;

        xSemaphoreGive(
          dataMutex
        );
      }


      // ======================================================
      // MEDICAL DASHBOARD
      // ======================================================

      medicalBodyTempFeed.publish(
        data.medical.bodyTemperature
      );


      medicalHRFeed.publish(
        (int32_t)
        data.medical.heartRate
      );


      medicalSpO2Feed.publish(
        (int32_t)
        data.medical.spo2
      );


      medicalAlertFeed.publish(
        data.medicalAlert
      );


      // ======================================================
      // FACILITY DASHBOARD
      // ======================================================

      facilityRoomTempFeed.publish(
        data.facility.roomTemperature
      );


      facilityOxygenFeed.publish(
        data.facility.oxygenLevel
      );


      facilityAQIFeed.publish(
        (int32_t)
        data.facility.aqi
      );


      facilityAlertFeed.publish(
        data.facilityAlert
      );


      // ======================================================
      // EXISTING TASK 1 SIGNALS
      // ======================================================

      motionFeed.publish(
        (int32_t)
        data.motion
      );


      ecgFeed.publish(
        (int32_t)
        data.ecg
      );


      Serial.println(
        "MQTTTask: dashboards updated"
      );
    }


    // --------------------------------------------------------
    // 30-second aggregation
    // --------------------------------------------------------

    if (
      mqtt.connected() &&
      millis() -
      aggregation.lastAggregation >=
      AGGREGATION_INTERVAL_MS
    )
    {
      if (
        xSemaphoreTake(
          dataMutex,
          pdMS_TO_TICKS(100)
        ) == pdTRUE
      )
      {
        publishMedicalSummary();

        publishFacilitySummary();

        resetAggregation();

        xSemaphoreGive(
          dataMutex
        );
      }
    }


    mqtt.processPackets(10);


    if (mqtt.connected())
    {
      mqtt.ping();
    }


    vTaskDelay(
      pdMS_TO_TICKS(500)
    );
  }
}


// ============================================================
// WATCHDOG TASK
// ============================================================

void watchdogTask(void *param)
{
  esp_task_wdt_add(NULL);


  const char *taskNames[TASK_COUNT] =
  {
    "MedicalSensorTask",

    "FacilitySensorTask",

    "ECGTask",

    "EnvironmentTask",

    "DataManagerTask",

    "DisplayTask",

    "AlertTask",

    "MQTTTask"
  };


  Serial.println(
    "WatchdogTask: monitoring tasks"
  );


  while (1)
  {
    bool allTasksAlive =
      true;


    uint32_t currentTime =
      millis();


    for (
      int i = 0;
      i < TASK_COUNT;
      i++
    )
    {
      uint32_t elapsed =
        currentTime -
        taskHeartbeat[i];


      if (
        elapsed >
        TASK_TIMEOUT_MS
      )
      {
        allTasksAlive =
          false;


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


    if (allTasksAlive)
    {
      esp_task_wdt_reset();

      Serial.println(
        "WatchdogTask: all tasks healthy"
      );
    }
    else
    {
      Serial.println(
        "WATCHDOG FAILURE: "
        "system task stalled!"
      );

      Serial.println(
        "Watchdog will NOT be fed."
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

void setup()
{
  Serial.begin(115200);

  delay(500);


  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "TASK 2 - DUAL ROLE IoT MONITORING"
  );

  Serial.println(
    "Medical + Facility Dashboards"
  );

  Serial.println(
    "========================================"
  );


  // ==========================================================
  // BUZZERS
  // ==========================================================

  pinMode(
    BUZZER_PIN_1,
    OUTPUT
  );

  pinMode(
    BUZZER_PIN_2,
    OUTPUT
  );

  pinMode(
    BUZZER_PIN_3,
    OUTPUT
  );

  pinMode(
    BUZZER_PIN_4,
    OUTPUT
  );


  digitalWrite(
    BUZZER_PIN_1,
    LOW
  );

  digitalWrite(
    BUZZER_PIN_2,
    LOW
  );

  digitalWrite(
    BUZZER_PIN_3,
    LOW
  );

  digitalWrite(
    BUZZER_PIN_4,
    LOW
  );


  // ==========================================================
  // PIR
  // ==========================================================

  pinMode(
    MOTION_DETECTOR,
    INPUT
  );


  // ==========================================================
  // DIP SWITCHES
  // ==========================================================

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


  // ==========================================================
  // ADC
  // ==========================================================

  analogReadResolution(12);


  // ==========================================================
  // TEMPERATURE SENSORS
  // ==========================================================

  bodyTempSensor.begin();

  roomTempSensor.begin();


  Serial.println(
    "Body DS18B20 initialized"
  );

  Serial.println(
    "Room DS18B20 initialized"
  );


  // ==========================================================
  // I2C / OLED
  // ==========================================================

  Wire.begin(
    21,
    22
  );


  Serial.println(
    "Initializing OLED..."
  );


  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  )
  {
    Serial.println(
      "OLED initialization FAILED!"
    );
  }
  else
  {
    Serial.println(
      "OLED initialized successfully."
    );


    display.clearDisplay();

    display.setTextSize(1);

    display.setTextColor(
      SSD1306_WHITE
    );

    display.setCursor(
      0,
      0
    );


    display.println(
      "TASK 2 IoT SYSTEM"
    );

    display.println();

    display.println(
      "Medical Dashboard"
    );

    display.println(
      "Facility Dashboard"
    );

    display.println();

    display.println(
      "Starting..."
    );


    display.display();


    delay(1500);
  }


  // ==========================================================
  // WIFI
  // ==========================================================

  Serial.println(
    "Connecting to WiFi..."
  );


  WiFi.begin(
    WIFI_SSID,
    WIFI_PASS
  );


  int wifiAttempts = 0;


  while (
    WiFi.status() !=
    WL_CONNECTED &&
    wifiAttempts < 30
  )
  {
    delay(500);

    Serial.print(
      "."
    );

    wifiAttempts++;
  }


  Serial.println();


  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
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
  else
  {
    Serial.println(
      "WiFi connection failed!"
    );
  }


  // ==========================================================
  // CREATE QUEUES
  // ==========================================================

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


  // ==========================================================
  // CREATE MUTEXES
  // ==========================================================

  dataMutex =
    xSemaphoreCreateMutex();


  displayMutex =
    xSemaphoreCreateMutex();


  // ==========================================================
  // CREATE SEMAPHORES
  // ==========================================================

  displaySemaphore =
    xSemaphoreCreateBinary();


  alertSemaphore =
    xSemaphoreCreateBinary();


  mqttSemaphore =
    xSemaphoreCreateBinary();


  // ==========================================================
  // INITIALIZE SYSTEM DATA
  // ==========================================================

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


  // ==========================================================
  // INITIALIZE AGGREGATION
  // ==========================================================

  resetAggregation();


  // ==========================================================
  // INITIALIZE HEARTBEATS
  // ==========================================================

  uint32_t startTime =
    millis();


  for (
    int i = 0;
    i < TASK_COUNT;
    i++
  )
  {
    taskHeartbeat[i] =
      startTime;
  }


  // ==========================================================
  // CREATE MEDICAL SENSOR TASK
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


  // ==========================================================
  // CREATE FACILITY SENSOR TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    facilitySensorTask,

    "FacilitySensorTask",

    4096,

    NULL,

    3,

    NULL,

    1
  );


  // ==========================================================
  // CREATE ECG TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    ecgTask,

    "ECGTask",

    4096,

    NULL,

    3,

    NULL,

    1
  );


  // ==========================================================
  // CREATE ENVIRONMENT TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    environmentTask,

    "EnvironmentTask",

    2048,

    NULL,

    3,

    NULL,

    1
  );


  // ==========================================================
  // CREATE DATA MANAGER TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    dataManagerTask,

    "DataManagerTask",

    4096,

    NULL,

    4,

    NULL,

    1
  );


  // ==========================================================
  // CREATE DISPLAY TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    displayTask,

    "DisplayTask",

    4096,

    NULL,

    2,

    NULL,

    0
  );


  // ==========================================================
  // CREATE ALERT TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    alertTask,

    "AlertTask",

    2048,

    NULL,

    5,

    NULL,

    1
  );


  // ==========================================================
  // CREATE MQTT TASK
  // ==========================================================

  xTaskCreatePinnedToCore(
    mqttTask,

    "MQTTTask",

    8192,

    NULL,

    1,

    NULL,

    0
  );


  // ==========================================================
  // CREATE WATCHDOG TASK
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


  // ==========================================================
  // STARTUP INFORMATION
  // ==========================================================

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
    "WatchdogTask        : Core 0"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "Task 2 system running..."
  );

  Serial.println(
    "========================================"
  );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
  vTaskDelay(
    portMAX_DELAY
  );
}