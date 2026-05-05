/****************************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* clang-format off */
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "bl_fw_api.h"

#include "bflb_gpio.h"
#include "bflb_irq.h"
#include "bflb_uart.h"

#include "rfparam_adapter.h"

#include "board.h"
#include "shell.h"

#include "wl80211.h"
#include "async_event.h"

#include <lwip/etharp.h>
#include <lwip/netdb.h>
#include <lwip/netifapi.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
/* clang-format on */

#define DBG_TAG "MAIN"
#include "log.h"

#if defined(BL616)
#include <bl616_mfg_media.h>
#elif defined(BL616CL)
#include <bl616cl_mfg_media.h>
#endif

#include "wifi_mgmr.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TCP_GPIO_PORT      2323
#define TCP_RX_BUF_SIZE    256
#define TCP_LINE_MAX       128
#define MAX_PIN_CONFIGS    8

#define AUTO_SSID          "Xiaomi_F7AA"
#define AUTO_PASSWD        "1124732794"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct {
    uint8_t pin;
    uint32_t cfg;
    bool configured;
} gpio_pin_cfg_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bflb_device_s *uart0;
static struct bflb_device_s *gpio;
static gpio_pin_cfg_t pin_cfgs[MAX_PIN_CONFIGS];
static int pin_cfg_count = 0;

static TaskHandle_t tcp_task_handle = NULL;

extern void shell_init_with_task(struct bflb_device_s *shell);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void wifi_event_handler(async_input_event_t ev, void *priv);
static void tcp_server_task(void *param);
static int  tcp_send_line(int fd, const char *line);
static int  tcp_handle_command(int fd, char *cmd);
static gpio_pin_cfg_t *pin_cfg_find(uint8_t pin);
static gpio_pin_cfg_t *pin_cfg_add(uint8_t pin);

/****************************************************************************
 * GPIO Pin Config Helpers
 ****************************************************************************/

static gpio_pin_cfg_t *pin_cfg_find(uint8_t pin)
{
    for (int i = 0; i < pin_cfg_count; i++) {
        if (pin_cfgs[i].pin == pin) {
            return &pin_cfgs[i];
        }
    }
    return NULL;
}

static gpio_pin_cfg_t *pin_cfg_add(uint8_t pin)
{
    gpio_pin_cfg_t *cfg = pin_cfg_find(pin);
    if (cfg) {
        return cfg;
    }
    if (pin_cfg_count >= MAX_PIN_CONFIGS) {
        return NULL;
    }
    cfg = &pin_cfgs[pin_cfg_count++];
    cfg->pin = pin;
    cfg->cfg = 0;
    cfg->configured = false;
    return cfg;
}

/****************************************************************************
 * TCP Protocol Handlers
 ****************************************************************************/

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

static int tcp_send_line(int fd, const char *line)
{
    int len = strlen(line);
    int sent = send(fd, line, len, 0);
    if (sent < 0) return -1;
    send(fd, "\r\n", 2, 0);
    return sent;
}

static int tcp_handle_command(int fd, char *cmd_line)
{
    char *line = trim(cmd_line);
    if (*line == '\0') {
        return 0;
    }

    char cmd[16];
    char arg1[16];
    char arg2[16];
    char arg3[16];
    int n = sscanf(line, "%15s %15s %15s %15s", cmd, arg1, arg2, arg3);

    if (n < 1) {
        tcp_send_line(fd, "ERR invalid command");
        return -1;
    }

    if (strcmp(cmd, "SET") == 0) {
        if (n < 3) {
            tcp_send_line(fd, "ERR usage: SET <pin> <0|1>");
            return -1;
        }
        int pin = atoi(arg1);
        int val = atoi(arg2);
        gpio_pin_cfg_t *cfg = pin_cfg_find(pin);
        if (!cfg || !cfg->configured) {
            tcp_send_line(fd, "ERR pin not configured, use MODE first");
            return -1;
        }
        if (!(cfg->cfg & GPIO_OUTPUT)) {
            tcp_send_line(fd, "ERR pin is not in output mode");
            return -1;
        }
        if (val) {
            bflb_gpio_set(gpio, pin);
        } else {
            bflb_gpio_reset(gpio, pin);
        }
        LOG_I("TCP: SET pin %d -> %d\r\n", pin, val);
        tcp_send_line(fd, "OK");
        return 0;
    }

    if (strcmp(cmd, "GET") == 0) {
        if (n < 2) {
            tcp_send_line(fd, "ERR usage: GET <pin>");
            return -1;
        }
        int pin = atoi(arg1);
        bool val = bflb_gpio_read(gpio, pin);
        char resp[32];
        snprintf(resp, sizeof(resp), "OK %d", val ? 1 : 0);
        tcp_send_line(fd, resp);
        return 0;
    }

    if (strcmp(cmd, "MODE") == 0) {
        if (n < 3) {
            tcp_send_line(fd, "ERR usage: MODE <pin> <out|in> [pullup|pulldown|float]");
            return -1;
        }
        int pin = atoi(arg1);
        uint32_t cfg_val = 0;

        if (strcmp(arg2, "out") == 0) {
            cfg_val |= GPIO_OUTPUT;
        } else if (strcmp(arg2, "in") == 0) {
            cfg_val |= GPIO_INPUT;
        } else {
            tcp_send_line(fd, "ERR mode must be 'out' or 'in'");
            return -1;
        }

        if (n >= 4) {
            if (strcmp(arg3, "pullup") == 0) {
                cfg_val |= GPIO_PULLUP;
            } else if (strcmp(arg3, "pulldown") == 0) {
                cfg_val |= GPIO_PULLDOWN;
            } else if (strcmp(arg3, "float") == 0) {
                cfg_val |= GPIO_FLOAT;
            } else {
                tcp_send_line(fd, "ERR pull must be 'pullup', 'pulldown', or 'float'");
                return -1;
            }
        } else {
            cfg_val |= GPIO_FLOAT;
        }
        cfg_val |= GPIO_SMT_EN | GPIO_DRV_0;

        gpio_pin_cfg_t *cfg = pin_cfg_add(pin);
        if (!cfg) {
            tcp_send_line(fd, "ERR too many pins configured");
            return -1;
        }

        bflb_gpio_init(gpio, pin, cfg_val);
        cfg->cfg = cfg_val;
        cfg->configured = true;
        LOG_I("TCP: MODE pin %d -> %s\r\n", pin, arg2);
        tcp_send_line(fd, "OK");
        return 0;
    }

    if (strcmp(cmd, "LIST") == 0) {
        char buf[TCP_LINE_MAX];
        if (pin_cfg_count == 0) {
            tcp_send_line(fd, "OK no pins configured");
            return 0;
        }
        for (int i = 0; i < pin_cfg_count; i++) {
            gpio_pin_cfg_t *cfg = &pin_cfgs[i];
            const char *mode = (cfg->cfg & GPIO_OUTPUT) ? "out" : "in";
            const char *pull = "float";
            if (cfg->cfg & GPIO_PULLUP) pull = "pullup";
            else if (cfg->cfg & GPIO_PULLDOWN) pull = "pulldown";
            snprintf(buf, sizeof(buf), "%d %s %s", cfg->pin, mode, pull);
            tcp_send_line(fd, buf);
        }
        return 0;
    }

    char resp[64];
    snprintf(resp, sizeof(resp), "ERR unknown command '%s'", cmd);
    tcp_send_line(fd, resp);
    return -1;
}

/****************************************************************************
 * TCP Server Task
 ****************************************************************************/

static void tcp_server_task(void *param)
{
    int server_fd;
    struct sockaddr_in addr;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LOG_E("tcp_server: socket failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_GPIO_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_E("tcp_server: bind failed\r\n");
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_fd, 1) < 0) {
        LOG_E("tcp_server: listen failed\r\n");
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    LOG_I("GPIO TCP server listening on port %d\r\n", TCP_GPIO_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        LOG_I("GPIO client connected from %s:%d\r\n",
              inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        tcp_send_line(client_fd, "OK GPIO Controller Ready");

        char rx_buf[TCP_RX_BUF_SIZE];
        char line_buf[TCP_LINE_MAX];
        int  line_pos = 0;

        while (1) {
            int rx_len = recv(client_fd, rx_buf, sizeof(rx_buf) - 1, 0);
            if (rx_len <= 0) {
                break;
            }
            rx_buf[rx_len] = '\0';

            for (int i = 0; i < rx_len; i++) {
                char c = rx_buf[i];
                if (c == '\r' || c == '\n') {
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';
                        tcp_handle_command(client_fd, line_buf);
                        line_pos = 0;
                    }
                } else if (line_pos < TCP_LINE_MAX - 1) {
                    line_buf[line_pos++] = c;
                }
            }
        }

        LOG_I("GPIO client disconnected\r\n");
        close(client_fd);
    }
}

/****************************************************************************
 * WiFi
 ****************************************************************************/

void wifi_start_firmware_task(void *param)
{
    LOG_I("Starting wifi ...\r\n");

    async_register_event_filter(EV_WIFI, wifi_event_handler, NULL);

    wifi_task_create();

    tcpip_init(NULL, NULL);

    wl80211_init();

    wifi_mgmr_init();

    extern uint8_t _heap_wifi_size[];
    printf("heap_wifi_size: %p\r\n", _heap_wifi_size);

    vTaskDelete(NULL);
}

static bool wifi_auto_connected = false;

static void wifi_auto_connect_task(void *arg1)
{
    LOG_I("[APP] Auto-connecting to %s...\r\n", AUTO_SSID);
    wifi_mgmr_sta_connect_params_t param;
    memset(&param, 0, sizeof(param));
    strcpy(param.ssid, AUTO_SSID);
    strcpy(param.key, AUTO_PASSWD);
    param.timeout_ms = 10000;
    param.use_dhcp = 1;
    if (wifi_mgmr_sta_connect(&param)) {
        LOG_E("[APP] Auto-connect failed\r\n");
    }
    wifi_mgmr_sta_autoconnect_enable();
    vTaskDelete(NULL);
}

static void wifi_event_handler(async_input_event_t ev, void *priv)
{
    uint32_t code = ev->code;

    switch (code) {
        case CODE_WIFI_ON_MGMR_DONE: {
            LOG_I("[APP] WiFi mgmr ready, scheduling auto-connect...\r\n");
            if (!wifi_auto_connected) {
                wifi_auto_connected = true;
                xTaskCreate(wifi_auto_connect_task, "wifi_connect", 1536, NULL, 5, NULL);
            }
        } break;
        case CODE_WIFI_ON_CONNECTED: {
            LOG_I("[APP] WiFi connected\r\n");
        } break;
        case CODE_WIFI_ON_GOT_IP: {
            LOG_I("[APP] WiFi got IP, starting GPIO TCP server\r\n");
            if (tcp_task_handle == NULL) {
                xTaskCreate(tcp_server_task, "tcp_gpio", 1536, NULL, 5, &tcp_task_handle);
            }
        } break;
        case CODE_WIFI_ON_DISCONNECT: {
            LOG_I("[APP] WiFi disconnected\r\n");
        } break;
        default: {
        } break;
    }
}

/* async event handler */
static void async_event_handler(void *arg1, uint32_t arg2)
{
    vTaskSuspendAll();
    async_event_loop();
    xTaskResumeAll();
}

static void async_event_loop_wake(void)
{
    BaseType_t xReturn;
    TickType_t wait = portMAX_DELAY;

    if (xTimerGetTimerDaemonTaskHandle() == xTaskGetCurrentTaskHandle()) {
        wait = 0;
    }

    xReturn = xTimerPendFunctionCall(async_event_handler, (void *)NULL, 0, wait);
    configASSERT(xReturn == pdPASS);
}

/****************************************************************************
 * GPIO Shell Commands (for UART debug)
 ****************************************************************************/

static int gpio_mode_cmd(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: gpio_mode <pin> <out|in> [pullup|pulldown|float]\r\n");
        return 0;
    }
    uint8_t pin = (uint8_t)atoi(argv[1]);
    uint32_t cfg = 0;

    if (strcmp(argv[2], "out") == 0) {
        cfg |= GPIO_OUTPUT;
    } else if (strcmp(argv[2], "in") == 0) {
        cfg |= GPIO_INPUT;
    } else {
        printf("gpio_mode: invalid mode '%s'\r\n", argv[2]);
        return 0;
    }
    if (argc >= 4) {
        if (strcmp(argv[3], "pullup") == 0)      cfg |= GPIO_PULLUP;
        else if (strcmp(argv[3], "pulldown") == 0) cfg |= GPIO_PULLDOWN;
        else if (strcmp(argv[3], "float") == 0)    cfg |= GPIO_FLOAT;
        else { printf("gpio_mode: invalid pull '%s'\r\n", argv[3]); return 0; }
    } else {
        cfg |= GPIO_FLOAT;
    }
    cfg |= GPIO_SMT_EN | GPIO_DRV_0;

    gpio_pin_cfg_t *pcfg = pin_cfg_add(pin);
    if (!pcfg) {
        printf("gpio_mode: too many pins\r\n");
        return 0;
    }
    bflb_gpio_init(gpio, pin, cfg);
    pcfg->cfg = cfg;
    pcfg->configured = true;
    printf("gpio_mode: pin %d set to %s\r\n", pin, argv[2]);
    return 0;
}

static int gpio_set_cmd(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: gpio_set <pin> <0|1>\r\n");
        return 0;
    }
    uint8_t pin = (uint8_t)atoi(argv[1]);
    int val = atoi(argv[2]);
    if (val) {
        bflb_gpio_set(gpio, pin);
        printf("gpio_set: pin %d -> HIGH\r\n", pin);
    } else {
        bflb_gpio_reset(gpio, pin);
        printf("gpio_set: pin %d -> LOW\r\n", pin);
    }
    return 0;
}

static int gpio_read_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: gpio_read <pin>\r\n");
        return 0;
    }
    uint8_t pin = (uint8_t)atoi(argv[1]);
    printf("gpio_read: pin %d = %s\r\n", pin, bflb_gpio_read(gpio, pin) ? "HIGH" : "LOW");
    return 0;
}

SHELL_CMD_EXPORT_ALIAS(gpio_mode_cmd, gpio_mode, configure GPIO pin mode.);
SHELL_CMD_EXPORT_ALIAS(gpio_set_cmd, gpio_set, set GPIO output level.);
SHELL_CMD_EXPORT_ALIAS(gpio_read_cmd, gpio_read, read GPIO input level.);

/****************************************************************************
 * Legacy macsw Commands
 ****************************************************************************/

int macsw_scan_cmd(int argc, char **argv)
{
    if (wl80211_scan(NULL)) {
        printf("wl80211_scan failed\n");
    }
    return 0;
}

int macsw_connect_cmd(int argc, char **argv)
{
    const char *ssid = AUTO_SSID, *passwd = AUTO_PASSWD;
    switch (argc) {
        case 3: passwd = argv[2];
        case 2: ssid = argv[1]; break;
        default: break;
    }
    printf("connect wifi: %s/%s\n", ssid, passwd);
    wifi_mgmr_sta_connect_params_t param;
    memset(&param, 0, sizeof(param));
    strcpy(param.ssid, ssid);
    strcpy(param.key, passwd);
    param.timeout_ms = 10000;
    param.use_dhcp = 1;
    if (wifi_mgmr_sta_connect(&param)) {
        printf("connect failed\n");
    }
    return 0;
}

int macsw_start_ap_cmd(int argc, char **argv)
{
    const char *ssid = "BL616-AP";
    const char *passwd = NULL;
    struct wl80211_ap_settings ap_setting;
    if (argc >= 2) ssid = argv[1];
    if (argc >= 3) passwd = argv[2];
    memset(&ap_setting, 0, sizeof(ap_setting));
    strncpy((char *)ap_setting.ssid, ssid, 32);
    if (passwd != NULL) strncpy((char *)ap_setting.password, passwd, 64);
    ap_setting.auth_type = WL80211_AUTHTYPE_OPEN_SYSTEM;
    ap_setting.beacon_interval = 100;
    ap_setting.center_freq1 = wl80211_channel_to_freq(11);
    ap_setting.channel_width = WL80211_CHAN_WIDTH_20;
    ap_setting.max_power = 0x14;
    printf("start ap: %s, password: %s\n", ssid, (passwd == NULL ? "null" : passwd));
    if (wl80211_ap_start(&ap_setting)) {
        printf("ap failed to start!\n");
    }
    return 0;
}

int macsw_stop_ap_cmd(int argc, char **argv)
{
    if (wl80211_ap_stop()) printf("ap failed to stop!\n");
    return 0;
}

static uint32_t monitor_rx_count = 0;
static TaskHandle_t mon_dump_tsk = NULL;
static void wl80211_monitor_rx(void *ctx, void *pkt, size_t len, size_t mac_hdr_len, int rssi)
{
    monitor_rx_count++;
}
static void monitor_rx_count_dump(void *param)
{
    while (wl80211_monitor_status()) {
        printf("monitor rx count: %u\n", monitor_rx_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    printf("monitor rx dump exit\n");
    mon_dump_tsk = NULL;
    vTaskDelete(NULL);
}
static void wifi_monitor_start_cmd(int argc, char **argv)
{
    struct wl80211_monitor_settings mon_settings;
    int ret;
    if (argc < 2) { printf("usage: %s <freq>\n", argv[0]); return; }
    mon_settings.channel_width = WL80211_CHAN_WIDTH_20;
    mon_settings.center_freq1 = atoi(argv[1]);
    mon_settings.recv = wl80211_monitor_rx;
    printf("freq: %d\n", mon_settings.center_freq1);
    if ((ret = wl80211_monitor_start(&mon_settings))) {
        printf("wl80211 monitor start failed:%d\n", ret);
    } else {
        monitor_rx_count = 0;
        if (!mon_dump_tsk) xTaskCreate(monitor_rx_count_dump, "monitor_rx_dump", 512, NULL, 10, &mon_dump_tsk);
    }
}
static void wifi_monitor_stop_cmd(int argc, char **argv)
{
    if (wl80211_monitor_stop()) printf("wl80211 monitor stop failed\n");
}

SHELL_CMD_EXPORT_ALIAS(macsw_connect_cmd, macsw_connect, macsw connect to AP.);
SHELL_CMD_EXPORT_ALIAS(macsw_scan_cmd, macsw_scan, macsw scan.);
SHELL_CMD_EXPORT_ALIAS(macsw_start_ap_cmd, macsw_start_ap, macsw start ap.);
SHELL_CMD_EXPORT_ALIAS(macsw_stop_ap_cmd, macsw_stop_ap, macsw stop ap.);
SHELL_CMD_EXPORT_ALIAS(wifi_monitor_start_cmd, monitor_start, monitor start.);
SHELL_CMD_EXPORT_ALIAS(wifi_monitor_stop_cmd, monitor_stop, monitor stop.);

static void wifi_inject_frame_test_cmd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s <freq_in_mhz>\n", argv[0]);
        return;
    }
    struct wl80211_inject_frame_params params;
    uint16_t freq = atoi(argv[1]);
    uint8_t test_frame[] = {
        0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x00, 0x00,
        0xDE, 0xAD, 0xBE, 0xEF
    };
    params.frame = test_frame;
    params.len = sizeof(test_frame);
    params.freq = freq;
    params.cb = NULL;
    params.opaque = NULL;
    printf("Injecting raw 802.11 frame on freq %d MHz (len=%zu)\n", freq, sizeof(test_frame));
    int ret = wl80211_inject_frame(&params);
    if (ret == 0) {
        printf("wl80211_inject_frame succeeded\n");
    } else {
        printf("wl80211_inject_frame failed: %d\n", ret);
    }
}

SHELL_CMD_EXPORT_ALIAS(wifi_inject_frame_test_cmd, inject_frame_test, inject raw 802.11 frame test);

static void udp_echo_server_task(void *param)
{
    int port = (int)(uintptr_t)param;
    int sock;
    struct sockaddr_in addr, client;
    socklen_t client_len = sizeof(client);
    char buf[256];
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { vTaskDelete(NULL); return; }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(sock); vTaskDelete(NULL); return; }
    printf("udp_echo: listening on port %d\n", port);
    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&client, &client_len);
        if (n > 0) {
            buf[n] = '\0';
            printf("udp_echo: recv %d bytes from %s:%d -> \"%s\"\n", n, inet_ntoa(client.sin_addr), ntohs(client.sin_port), buf);
            sendto(sock, buf, n, 0, (struct sockaddr *)&client, client_len);
        }
    }
}
static void udp_echo_server_cmd(int argc, char **argv)
{
    int port = 5000;
    if (argc >= 2) port = atoi(argv[1]);
    xTaskCreate(udp_echo_server_task, "udp_echo", 1024, (void *)(uintptr_t)port, 5, NULL);
}
static void udp_send_cmd(int argc, char **argv)
{
    if (argc < 4) { printf("usage: udp_send <ip> <port> <message>\n"); return; }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *msg = argv[3];
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { printf("udp_send: socket failed\n"); return; }
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = inet_addr(ip);
    printf("udp_send: sending \"%s\" to %s:%d\n", msg, ip, port);
    sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest, sizeof(dest));
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &from_len);
    if (n > 0) {
        buf[n] = '\0';
        printf("udp_send: reply from %s:%d -> \"%s\" [OK]\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), buf);
    } else {
        printf("udp_send: no reply (timeout) [FAIL]\n");
    }
    close(sock);
}

SHELL_CMD_EXPORT_ALIAS(udp_echo_server_cmd, udp_echo_server, start UDP echo server for AP forward test.);
SHELL_CMD_EXPORT_ALIAS(udp_send_cmd, udp_send, send UDP msg and wait for echo reply.);

/****************************************************************************
 * Main
 ****************************************************************************/

int main(void)
{
    board_init();

    uart0 = bflb_device_get_by_name("uart0");
    shell_init_with_task(uart0);

    gpio = bflb_device_get_by_name("gpio");

    if (0 != rfparam_init(0, NULL, 0)) {
        LOG_I("PHY RF init failed!\r\n");
        return 0;
    }

    LOG_I("PHY RF init success!\r\n");

    async_event_init(async_event_loop_wake);

    xTaskCreate(wifi_start_firmware_task, "wifi init", 1024, NULL, 10, NULL);

    vTaskStartScheduler();

    while (1) {}
}
