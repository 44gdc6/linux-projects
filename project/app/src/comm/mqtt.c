#include "comm/mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "MQTTClient.h"
#include "config/app_config.h"
#include "core/log.h"
#include "core/linkqueue.h"
#include "core/mailbox.h"

typedef struct mqtt_runtime {
    MQTTClient client;
    int connected;
    char publish_topic[200];
    char reply_topic[200];
    time_t last_connect_attempt;
} mqtt_runtime_t;

static volatile MQTTClient_deliveryToken delivered_token;

static int mqtt_extract_reply_code(const char *payload)
{
    const char *code_ptr = NULL;

    if (payload == NULL) {
        return -1;
    }

    code_ptr = strstr(payload, "\"code\":");
    if (code_ptr == NULL) {
        return -1;
    }

    code_ptr += strlen("\"code\":");
    return atoi(code_ptr);
}

static void mqtt_escape_json_string(const char *src, char *dst, size_t dst_size)
{
    size_t src_index = 0;
    size_t dst_index = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[src_index] != '\0' && dst_index + 1 < dst_size) {
        if ((src[src_index] == '"' || src[src_index] == '\\') && dst_index + 2 < dst_size) {
            dst[dst_index++] = '\\';
        }
        dst[dst_index++] = src[src_index++];
    }

    dst[dst_index] = '\0';
}

static void pack_topics(mqtt_runtime_t *runtime)
{
    sprintf(runtime->reply_topic,
            "$sys/%s/%s/thing/property/post/reply",
            MQTT_PRODUCT_ID,
            MQTT_DEVICE_NAME);
    sprintf(runtime->publish_topic,
            "$sys/%s/%s/thing/property/post",
            MQTT_PRODUCT_ID,
            MQTT_DEVICE_NAME);
}

static void delivered(void *context, MQTTClient_deliveryToken dt)
{
    (void)context;
    delivered_token = dt;
}

static int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    int i = 0;
    int reply_code = -1;
    char *payloadptr = NULL;
    char payload_buf[512];
    int copy_len = 0;

    (void)context;
    (void)topicLen;
    printf("mqtt message arrived\n");
    LOG_INFO("mqtt message arrived: topic=%s payload_len=%d", topicName, message->payloadlen);
    printf("topic: %s\n", topicName);
    printf("message: ");
    payloadptr = (char *)message->payload;
    for (i = 0; i < message->payloadlen; ++i) {
        putchar(*payloadptr++);
    }
    putchar('\n');

    copy_len = message->payloadlen;
    if (copy_len >= (int)sizeof(payload_buf)) {
        copy_len = (int)sizeof(payload_buf) - 1;
    }
    memcpy(payload_buf, message->payload, copy_len);
    payload_buf[copy_len] = '\0';
    reply_code = mqtt_extract_reply_code(payload_buf);
    if (reply_code >= 0) {
        if (reply_code == 200) {
            LOG_INFO("mqtt platform accepted property post, code=%d", reply_code);
        } else {
            LOG_ERROR("mqtt platform rejected property post, code=%d payload=%s", reply_code, payload_buf);
        }
    } else {
        LOG_WARN("mqtt reply payload has no code field: %s", payload_buf);
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

static void connlost(void *context, char *cause)
{
    mqtt_runtime_t *runtime = (mqtt_runtime_t *)context;

    if (runtime != NULL) {
        runtime->connected = 0;
    }
    printf("mqtt connection lost: %s\n", cause == NULL ? "unknown" : cause);
    LOG_WARN("mqtt connection lost: %s", cause == NULL ? "unknown" : cause);
}

static int mqtt_runtime_init(mqtt_runtime_t *runtime)
{
    int rc = 0;

    memset(runtime, 0, sizeof(*runtime));
    pack_topics(runtime);

    rc = MQTTClient_create(&runtime->client,
                           MQTT_BROKER,
                           MQTT_CLIENT_ID,
                           MQTTCLIENT_PERSISTENCE_NONE,
                           NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt create failed: %d\n", rc);
        LOG_ERROR("mqtt create failed: %d", rc);
        return -1;
    }

    rc = MQTTClient_setCallbacks(runtime->client, runtime, connlost, msgarrvd, delivered);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt set callbacks failed: %d\n", rc);
        LOG_ERROR("mqtt set callbacks failed: %d", rc);
        MQTTClient_destroy(&runtime->client);
        return -1;
    }

    return 0;
}

static int mqtt_runtime_connect(mqtt_runtime_t *runtime)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc = 0;

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.username = MQTT_PRODUCT_ID;
    conn_opts.password = MQTT_PASSWORD;

    runtime->last_connect_attempt = time(NULL);
    LOG_INFO("mqtt connecting broker=%s client_id=%s username=%s",
             MQTT_BROKER,
             MQTT_CLIENT_ID,
             MQTT_PRODUCT_ID);
    rc = MQTTClient_connect(runtime->client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt connect failed: %d\n", rc);
        LOG_ERROR("mqtt connect failed: %d", rc);
        runtime->connected = 0;
        return -1;
    }

#if MQTT_ENABLE_REPLY_SUBSCRIBE
    rc = MQTTClient_subscribe(runtime->client, runtime->reply_topic, MQTT_QOS);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt subscribe failed: %d\n", rc);
        LOG_ERROR("mqtt subscribe failed: %d", rc);
    } else {
        LOG_INFO("mqtt subscribed reply topic: %s", runtime->reply_topic);
    }
#endif

    runtime->connected = 1;
    LOG_INFO("mqtt connected to broker, publish topic: %s", runtime->publish_topic);
    return 0;
}

static int mqtt_publish_temperature(mqtt_runtime_t *runtime, const data_t *sample)
{
    MQTTClient_deliveryToken delivery_token;
    MQTTClient_message message = MQTTClient_message_initializer;
    char payload[2048];
    char weather_day[WEATHER_FIELD_LEN * 2];
    char weather_week[WEATHER_FIELD_LEN * 2];
    char weather_city[WEATHER_FIELD_LEN * 2];
    char weather_text[WEATHER_FIELD_LEN * 2];
    char weather_temp[WEATHER_FIELD_LEN * 2];
    char params[1024];
    int rc = 0;
    static int message_id = 10000;
    int temp_value = 0;

    temp_value = (int)lround(sample->temp);

    snprintf(params, sizeof(params), "\"%s\":{\"value\":%d}", MQTT_TEMP_KEY, temp_value);

    if (sample->hum_valid) {
        snprintf(params + strlen(params),
                 sizeof(params) - strlen(params),
                 ",\"%s\":{\"value\":%d}",
                 MQTT_HUMIDITY_KEY,
                 (int)lround(sample->hum));
    }

    if (sample->alcohol_valid) {
        snprintf(params + strlen(params),
                 sizeof(params) - strlen(params),
                 ",\"%s\":{\"value\":%d}",
                 MQTT_ALCOHOL_KEY,
                 sample->alcohol_raw);
    }

    if (sample->accel_valid) {
        snprintf(params + strlen(params),
                 sizeof(params) - strlen(params),
                 ",\"%s\":{\"value\":%d},\"%s\":{\"value\":%d},\"%s\":{\"value\":%d},\"%s\":{\"value\":%d}",
                 MQTT_ACCEL_X_KEY,
                 sample->accel_x,
                 MQTT_ACCEL_Y_KEY,
                 sample->accel_y,
                 MQTT_ACCEL_Z_KEY,
                 sample->accel_z,
                 MQTT_MOTION_STATE_KEY,
                 sample->motion_state);
    }

    if (sample->weather_valid) {
        mqtt_escape_json_string(sample->weather_day, weather_day, sizeof(weather_day));
        mqtt_escape_json_string(sample->weather_week, weather_week, sizeof(weather_week));
        mqtt_escape_json_string(sample->weather_city, weather_city, sizeof(weather_city));
        mqtt_escape_json_string(sample->weather_text, weather_text, sizeof(weather_text));
        mqtt_escape_json_string(sample->weather_temp, weather_temp, sizeof(weather_temp));
        snprintf(params + strlen(params),
                 sizeof(params) - strlen(params),
                 ",\"%s\":{\"value\":\"%s\"},\"%s\":{\"value\":\"%s\"},\"%s\":{\"value\":\"%s\"},\"%s\":{\"value\":\"%s\"},\"%s\":{\"value\":\"%s\"}",
                 MQTT_WEATHER_DAY_KEY,
                 weather_day,
                 MQTT_WEATHER_WEEK_KEY,
                 weather_week,
                 MQTT_WEATHER_CITY_KEY,
                 weather_city,
                 MQTT_WEATHER_TEXT_KEY,
                 weather_text,
                 MQTT_WEATHER_TEMP_KEY,
                 weather_temp);
    }

    snprintf(payload,
             sizeof(payload),
             "{\"id\":\"%d\",\"version\":\"1.0\",\"params\":{%s}}",
             message_id++,
             params);

    message.qos = MQTT_QOS;
    message.retained = 0;
    message.payload = payload;
    message.payloadlen = (int)strlen(payload);

    printf("%s\n", payload);
    LOG_INFO("mqtt publish topic=%s payload=%s", runtime->publish_topic, payload);

    rc = MQTTClient_publishMessage(runtime->client,
                                   runtime->publish_topic,
                                   &message,
                                   &delivery_token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt publish failed: %d\n", rc);
        LOG_ERROR("mqtt publish failed: %d", rc);
        runtime->connected = 0;
        return -1;
    }

    rc = MQTTClient_waitForCompletion(runtime->client, delivery_token, MQTT_TIMEOUT_MS);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("mqtt wait for completion failed: %d\n", rc);
        LOG_ERROR("mqtt wait for completion failed: %d", rc);
        runtime->connected = 0;
        return -1;
    }

    LOG_INFO("mqtt publish completed, token=%d", delivery_token);
    return 0;
}

static void mqtt_runtime_deinit(mqtt_runtime_t *runtime)
{
    if (runtime->connected) {
        MQTTClient_disconnect(runtime->client, 1000);
    }
    MQTTClient_destroy(&runtime->client);
}

void *mqtt_thread(void *arg)
{
    mqtt_runtime_t runtime;
    data_t sample;
    int initialized = 0;

    (void)arg;
    memset(&runtime, 0, sizeof(runtime));

    while (1) {
        if (!initialized) {
            if (mqtt_runtime_init(&runtime) == 0) {
                initialized = 1;
            } else {
                sleep(MQTT_RETRY_INTERVAL_SEC);
                continue;
            }
        }

        if (!runtime.connected) {
            time_t now = time(NULL);
            if (now - runtime.last_connect_attempt >= MQTT_RETRY_INTERVAL_SEC) {
                mqtt_runtime_connect(&runtime);
            }
        }

        if (mailbox_recv_msg(&sample) <= 0) {
            usleep(200000);
            continue;
        }

        if (!sample.temp_valid || !runtime.connected) {
            if (!sample.temp_valid) {
                LOG_WARN("mqtt skip publish because temperature sample invalid");
            }
            continue;
        }

        if (mqtt_publish_temperature(&runtime, &sample) != 0) {
            runtime.connected = 0;
        }
    }

    mqtt_runtime_deinit(&runtime);
    return NULL;
}
