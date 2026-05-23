#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#define LM75_DEVICE_PATH "/dev/lm75_misc"
#define MQ3_DEVICE_PATH "/dev/mq3_misc"
#define ADXL345_DEVICE_PATH "/dev/adxl345_misc"
#define DHT11_DEVICE_PATH "/dev/dht11_misc"
#define BEEP_DEVICE_PATH "/dev/beep_misc"
#define APP_LOG_PATH "./app.log"
#define SQLITE_DB_PATH "./sensor_history.db"
#define WEATHER_CITY "xian"
#define WEATHER_REFRESH_INTERVAL_SEC 600

#define MQTT_BROKER "tcp://183.230.40.96:1883"
#define MQTT_DEVICE_NAME "device_01"
#define MQTT_CLIENT_ID MQTT_DEVICE_NAME
#define MQTT_PRODUCT_ID "6x3XZH6Gv4"
#define MQTT_PASSWORD "version=2018-10-31&res=products%2F6x3XZH6Gv4%2Fdevices%2Fdevice_01&et=1837255523&method=md5&sign=0jJNfNUIug3KwvrvlWrJVg%3D%3D"
#define MQTT_TEMP_KEY "tempval"
#define MQTT_HUMIDITY_KEY "humval"
#define MQTT_ALCOHOL_KEY "aclval"
#define MQTT_ACCEL_X_KEY "accx"
#define MQTT_ACCEL_Y_KEY "accy"
#define MQTT_ACCEL_Z_KEY "accz"
#define MQTT_MOTION_STATE_KEY "motionst"
#define MQTT_WEATHER_DAY_KEY "wday"
#define MQTT_WEATHER_WEEK_KEY "wweek"
#define MQTT_WEATHER_CITY_KEY "wcity"
#define MQTT_WEATHER_TEXT_KEY "wtext"
#define MQTT_WEATHER_TEMP_KEY "wtemp"
#define MQTT_ENABLE_REPLY_SUBSCRIBE 1
#define MQTT_QOS 0
#define MQTT_TIMEOUT_MS 10000L

#define COLLECT_POLL_INTERVAL_SEC 1
#define MQTT_RETRY_INTERVAL_SEC 5
#define SQLITE_RETRY_INTERVAL_SEC 5

#define ALCOHOL_LOW_THRESHOLD 1200
#define ALCOHOL_HIGH_THRESHOLD 1800
#define ALCOHOL_ALARM_ASSERT_COUNT 3
#define ALCOHOL_ALARM_CLEAR_COUNT 3

#define MOTION_SHAKE_DELTA_THRESHOLD 60
#define MOTION_VIBRATION_DELTA_THRESHOLD 120
#define MOTION_TILT_AXIS_THRESHOLD 180
#define MOTION_ALARM_ASSERT_COUNT 3
#define MOTION_ALARM_CLEAR_COUNT 3

#endif
