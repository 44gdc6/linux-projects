#include "comm/can_node.h"
#include "config/app_config.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

can_network_status_t g_can_status;

static uint32_t get_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

int can_socket_init(const char *ifname)
{
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;

    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        LOG_ERROR("can socket creation failed");
        return -1;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        LOG_ERROR("can ioctl SIOCGIFINDEX failed on %s", ifname);
        close(s);
        return -1;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("can bind failed");
        close(s);
        return -1;
    }

    struct can_filter rfilter[4];
    rfilter[0].can_id   = CAN_HEARTBEAT_ID_BASE + CAN_NODE_ID_F103;
    rfilter[0].can_mask = CAN_SFF_MASK;
    rfilter[1].can_id   = CAN_HEARTBEAT_ID_BASE + CAN_NODE_ID_F407;
    rfilter[1].can_mask = CAN_SFF_MASK;
    rfilter[2].can_id   = CAN_RESP_ID_BASE + CAN_NODE_ID_F103;
    rfilter[2].can_mask = CAN_SFF_MASK;
    rfilter[3].can_id   = CAN_RESP_ID_BASE + CAN_NODE_ID_F407;
    rfilter[3].can_mask = CAN_SFF_MASK;

    if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter)) < 0) {
        LOG_WARN("can setsockopt filter failed, continuing without filter");
    }

    return s;
}

void can_send_command(int sock,
                      uint8_t node_id,
                      uint8_t cmd,
                      uint8_t seq,
                      uint8_t d0,
                      uint8_t d1,
                      uint8_t d2,
                      uint8_t d3)
{
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = CAN_CMD_ID_BASE + node_id;
    frame.can_dlc = CAN_FRAME_DLC;
    frame.data[0] = cmd;
    frame.data[1] = seq;
    frame.data[2] = d0;
    frame.data[3] = d1;
    frame.data[4] = d2;
    frame.data[5] = d3;

    if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
        LOG_ERROR("can send command failed, node=0x%02X cmd=0x%02X", node_id, cmd);
    }
}

static void update_node_heartbeat(uint8_t node_id, const uint8_t *data)
{
    can_node_status_t *node = NULL;

    if (node_id == CAN_NODE_ID_F103) {
        node = &g_can_status.f103;
    } else if (node_id == CAN_NODE_ID_F407) {
        node = &g_can_status.f407;
    } else {
        return;
    }

    pthread_mutex_lock(&g_can_status.lock);
    node->online = 1;
    node->last_seen_ms = get_monotonic_ms();
    node->heartbeat_counter = ((uint16_t)data[2] << 8) | data[1];
    node->last_cmd = data[3];
    node->last_status = data[4];
    node->tx_ok_counter = data[5];
    node->rx_ok_counter = data[6];
    node->last_error = data[7];
    pthread_mutex_unlock(&g_can_status.lock);
}

void can_parse_frame(int sock, const struct can_frame *frame)
{
    uint32_t id;

    (void)sock;
    if (frame == NULL) {
        return;
    }

    id = frame->can_id & CAN_SFF_MASK;

    if (id == (CAN_HEARTBEAT_ID_BASE + CAN_NODE_ID_F103)) {
        update_node_heartbeat(CAN_NODE_ID_F103, frame->data);
    } else if (id == (CAN_HEARTBEAT_ID_BASE + CAN_NODE_ID_F407)) {
        update_node_heartbeat(CAN_NODE_ID_F407, frame->data);
    }
}

static void check_node_timeout(can_node_status_t *node, uint32_t now_ms, uint32_t timeout_ms)
{
    if (node->online == 0) {
        return;
    }
    if ((now_ms - node->last_seen_ms) > timeout_ms) {
        node->online = 0;
    }
}

void can_status_copy(can_network_status_t *out)
{
    if (out == NULL) {
        return;
    }
    pthread_mutex_lock(&g_can_status.lock);
    memcpy(out, &g_can_status, sizeof(*out));
    pthread_mutex_unlock(&g_can_status.lock);
}

void *can_thread(void *arg)
{
    int sock;
    fd_set rfds;
    struct timeval tv;
    struct can_frame frame;
    uint32_t last_cmd_ms = 0;
    uint8_t seq = 0;

    (void)arg;

    memset(&g_can_status, 0, sizeof(g_can_status));
    pthread_mutex_init(&g_can_status.lock, NULL);

    sock = can_socket_init(CAN_INTERFACE_NAME);
    if (sock < 0) {
        LOG_ERROR("can_thread: socket init failed");
        return NULL;
    }

    LOG_INFO("can_thread started on %s", CAN_INTERFACE_NAME);

    can_send_command(sock, CAN_NODE_ID_F103, 0x03, seq++, 0xE8, 0x03, 0, 0);
    can_send_command(sock, CAN_NODE_ID_F407, 0x03, seq++, 0xE8, 0x03, 0, 0);

    while (1) {
        int ret;
        uint32_t now_ms = get_monotonic_ms();

        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        ret = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(sock, &rfds)) {
            int nbytes = read(sock, &frame, sizeof(frame));
            if (nbytes == sizeof(frame)) {
                can_parse_frame(sock, &frame);
            }
        }

        check_node_timeout(&g_can_status.f103, now_ms, CAN_NODE_TIMEOUT_MS);
        check_node_timeout(&g_can_status.f407, now_ms, CAN_NODE_TIMEOUT_MS);

        if ((now_ms - last_cmd_ms) > 5000) {
            can_send_command(sock, CAN_NODE_ID_F103, 0x01, seq++, 0, 0, 0, 0);
            can_send_command(sock, CAN_NODE_ID_F407, 0x01, seq++, 0, 0, 0, 0);
            last_cmd_ms = now_ms;
        }
    }

    close(sock);
    pthread_mutex_destroy(&g_can_status.lock);
    return NULL;
}
