#ifndef __CAN_NODE_H__
#define __CAN_NODE_H__

#include <stdint.h>
#include <pthread.h>

/* Network configuration */
#define CAN_INTERFACE_NAME       "can0"

/* Node IDs */
#define CAN_NODE_ID_F103         0x01U
#define CAN_NODE_ID_F407         0x02U

/* Frame ID bases */
#define CAN_HEARTBEAT_ID_BASE    0x100U
#define CAN_RESP_ID_BASE         0x200U
#define CAN_CMD_ID_BASE          0x300U
#define CAN_SENSOR_DATA_ID_BASE  0x400U

/* Protocol constants */
#define CAN_FRAME_DLC            8U
#define CAN_NODE_TIMEOUT_MS      5000U

/* CAN commands */
#define CAN_CMD_PING             0x01U
#define CAN_CMD_READ_STATUS      0x02U
#define CAN_CMD_SET_HEARTBEAT    0x03U

/* Sensor data validity flags */
#define CAN_SENSOR_VALID_FLAG    0x01U

typedef struct {
    uint8_t online;
    uint8_t last_cmd;
    uint8_t last_status;
    uint16_t heartbeat_counter;
    uint32_t last_seen_ms;
    uint16_t tx_ok_counter;
    uint16_t rx_ok_counter;
    uint8_t last_error;

    /* DHT11 sensor data from F407 */
    uint8_t dht11_humidity;
    uint8_t dht11_temperature;
    uint8_t dht11_valid;

    /* Flame sensor data from F103 */
    uint8_t flame_status;
    uint8_t flame_valid;
} can_node_status_t;

typedef struct {
    can_node_status_t f103;
    can_node_status_t f407;
    pthread_mutex_t lock;
} can_network_status_t;

extern can_network_status_t g_can_status;

int can_socket_init(const char *ifname);
void can_send_command(int sock, uint8_t node_id, uint8_t cmd, uint8_t seq,
                      uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3);
void can_parse_frame(int sock, const struct can_frame *frame);
void can_status_copy(can_network_status_t *out);
void *can_thread(void *arg);

#endif
