# Ubuntu Web 管理端设计与实现审计

> 当前文档已按 `web_project` 现有代码审计更新。它描述的是 2026-07-06 当前已经实现的 Ubuntu Web 端，不再把未落地的 Paho MQTT 客户端、daemon 参数、WebSocket/HTTPS、设备端配置下发等功能写成已完成能力。

## 一、定位与目标

Web 管理端部署在 Ubuntu 服务器上，独立于嵌入式板端运行。设备端负责采集、CAN、GPS、告警判断和 MQTT/OneNET 上报；Ubuntu Web 端负责接收 JSON 遥测、更新实时缓存、写入 SQLite，并把数据展示给浏览器。

当前目标：

1. 在 Ubuntu 上编译并运行一个 C Web 服务。
2. 接收普通遥测 JSON 和 OneNET 属性格式 JSON。
3. 提供实时、历史、告警和配置展示 API。
4. 提供登录页、实时监控、历史数据、告警记录、系统配置页面。
5. 明确当前安全边界和生产部署前必须补齐的能力。

## 二、当前架构

```text
嵌入式设备端 project/app
  collect_thread / can_thread / gps_thread / weather_thread
        │
        ▼
  mqtt_thread 打包 OneNET 属性 JSON
        │
        ▼
MQTT Broker / OneNET 平台 / Ubuntu Mosquitto
        │
        ├─ OneNET 数据流转或应用侧转发
        └─ scripts/mqtt_forward.sh: mosquitto_sub -> curl POST
        │
        ▼
Ubuntu Web 后端 web_project/bin/webserver
  /ingest/telemetry 或 /ingest/onenet
        │
        ├─ payload_parser.c 解析字段
        ├─ realtime_cache.c 保存最近一次样本
        └─ storage.c 写入 SQLite
        │
        ▼
浏览器静态页面
  /login.html /index.html /history.html /alarms.html /config.html
```

设计边界：

- Web 端不读取板端共享内存、设备节点或板端 SQLite。
- 当前 C Web 后端没有内置 MQTT 客户端，MQTT 进入 Web 的方式是外部脚本或 OneNET 平台回调。
- 当前配置页面可以保存 Ubuntu 本地 Web 配置，但未实现 MQTT 下发设备参数。

## 三、代码结构

```text
web_project/
├── Makefile
├── README.md
├── web-server-design.md
├── include/
│   ├── http/http_server.h
│   ├── config/web_config.h
│   ├── ingest/payload_parser.h
│   ├── ingest/realtime_cache.h
│   └── storage/storage.h
├── src/
│   ├── main.c
│   ├── config/web_config.c
│   ├── http/http_server.c
│   ├── ingest/payload_parser.c
│   ├── ingest/realtime_cache.c
│   └── storage/storage.c
├── scripts/
│   └── mqtt_forward.sh
├── tests/
│   ├── test_payload_cache.c
│   └── test_web_config.c
└── www/
    ├── login.html
    ├── index.html
    ├── history.html
    ├── alarms.html
    ├── config.html
    └── assets/
        ├── css/style.css
        └── js/app.js
```

不存在的旧设计模块：

- 没有 `src/api/`、`src/core/`、`src/utils/` 目录。
- 没有 `mqtt_ingest.c` 或 Paho MQTT C 客户端接入。
- 没有 `connection_pool.c`、独立日志模块、Basic Auth 模块、WebSocket 模块。
- 没有 Chart.js 文件，历史页使用前端原生 JS/CSS 柱状图。

## 四、后端实现

### 4.1 启动流程

`src/main.c` 负责解析参数并初始化上下文：

```text
-p     HTTP 端口，默认 8080
-r     静态页面目录，默认 ./www
--db   SQLite 数据库路径，默认 ./sensor_history.db
--config Web 本地配置文件路径，默认 ./web_config.json
```

启动时会：

1. 初始化 `realtime_cache_t`。
2. 打开 SQLite 数据库。
3. 创建或迁移 Web 端所需表结构。
4. 进入 `http_server_run()`。

当前不支持这些旧设计参数：

```text
-d
-l
--mqtt-host
--mqtt-port
--mqtt-topic
```

### 4.2 HTTP 服务

`src/http/http_server.c` 当前能力：

- `socket(AF_INET, SOCK_STREAM)` 默认监听 `127.0.0.1`；设置 `SENSOR_WEB_BIND=0.0.0.0` 后才对外监听。
- `SO_REUSEADDR` 开启端口复用。
- `listen(..., 128)`。
- 主监听 fd 设为非阻塞。
- 使用 `epoll_create1()` 与 `epoll_wait()`。
- 客户端 fd 使用 `EPOLLONESHOT`，处理完一个请求后关闭连接。
- 客户端读写超时均设置为 2 秒。
- 请求缓冲区 `REQ_BUF_SIZE=65536`，响应缓冲区 `RESP_BUF_SIZE=131072`。
- 支持 `Content-Length`，不支持 chunked body。
- 响应统一 `Connection: close`，不支持 HTTP keep-alive。
- JSON 响应通过 `send_text()` 输出，默认带 `Access-Control-Allow-Origin: *`，可用 `SENSOR_WEB_CORS_ORIGIN` 覆盖。

静态文件服务：

- `/` 映射到 `/index.html`。
- 根据扩展名返回 HTML/CSS/JS/JSON/PNG/JPEG MIME。
- 路径中包含 `..` 时返回 403。
- 使用 `sendfile()` 发送文件内容。

## 五、已实现 API

| 接口 | 当前行为 |
|------|----------|
| `GET /api/realtime` | 返回内存缓存中的最近一次遥测。无数据时返回 `{"online":false,"message":"no telemetry received"}`。 |
| `GET /api/status` | 返回监听地址、端口、DB/配置路径、鉴权/CORS 状态、最近遥测和当前本地配置。 |
| `GET /api/history?sensor=temperature&limit=100&start=1719840000&end=1719849600` | 查询 `sensor_samples_flat`。`sensor` 缺省为 `temperature`，`start/end` 为可选 Unix 秒级时间戳，`limit <= 0` 或 `limit > 1000` 时回退到 100。 |
| `GET /api/export?sensor=temperature&limit=1000` | 导出 CSV。`sensor/start/end` 可选，HTTP 层把 `limit` 控制在固定响应缓冲可承受范围内。 |
| `GET /api/alarms?limit=50` | 查询 `device_samples` 中告警字段非零的记录。同一采样存在多个告警时会拆成多条事件。`limit <= 0` 或 `limit > 500` 时回退到 50。 |
| `GET /api/config` | 返回当前 Web 本地配置。 |
| `PUT /api/config` | 校验并保存 Web 本地配置到 `--config` 指定 JSON 文件。当前不下发到设备端。 |
| `POST /ingest/telemetry` | 解析普通扁平 JSON，写入 SQLite，更新实时缓存。 |
| `POST /ingest/onenet` | 解析 OneNET 属性格式 JSON，写入 SQLite，更新实时缓存。当前和 `/ingest/telemetry` 共用同一处理函数。 |

注意：除 `PUT /api/config` 外，`/api/*` 只允许 GET；`/ingest/*` 只允许 POST。其他方法返回 405。设置 `SENSOR_WEB_TOKEN` 后，`/api/*` 和 `/ingest/*` 需要 `Authorization: Bearer <token>` 或 `X-Device-Token: <token>`。

### 5.1 `/api/realtime` 响应

有数据时返回结构：

```json
{
  "timestamp": 1719849600,
  "device": "device_01",
  "online": true,
  "sensors": {
    "cargo": {
      "temperature": 25.6,
      "vibration": {
        "accel_x": 12,
        "accel_y": -8,
        "accel_z": 981,
        "motion_state": 0,
        "motion_alarm": false
      },
      "flame": {
        "valid": true,
        "status": 0
      }
    },
    "cabin": {
      "alcohol_raw": 980,
      "alcohol_level": 0,
      "alcohol_alarm": false,
      "humidity": 55,
      "gps": {
        "lat": 34.2614,
        "lon": 108.9404,
        "speed": 12.5,
        "satellites": 7,
        "fence_alarm": false
      }
    }
  },
  "can_nodes": [
    {"id": 1, "name": "STM32F103", "online": true, "flame_status": 0},
    {"id": 2, "name": "STM32F407", "online": true, "dht11_temperature": 26, "dht11_humidity": 55}
  ]
}
```

当前 Web 端没有解析或输出天气字段。

### 5.2 `/api/history`

当前支持：

```text
sensor
limit
start
end
```

常用 `sensor`：

```text
temperature
humidity
alcohol_raw
alcohol_level
alcohol_alarm
accel_x
accel_y
accel_z
motion_state
motion_alarm
flame_status
flame_valid
dht11_temperature
dht11_humidity
dht11_valid
gps_lat
gps_lon
gps_speed
gps_satellites
gps_fence_alarm
```

`start` 和 `end` 为可选 Unix 秒级时间戳，非法时间范围会返回错误。

### 5.3 `/api/alarms`

查询条件：

```sql
alcohol_alarm != 0
OR motion_alarm != 0
OR gps_fence_alarm != 0
OR flame_status != 0
```

输出字段：

```json
{
  "alarms": [
    {
      "timestamp": 1719849600,
      "device": "device_01",
      "type": "geofence",
      "level": "warning"
    }
  ]
}
```

同一条采样如果多个告警同时存在，当前会拆成多条事件。事件字段包含 `id`、`timestamp`、`device`、`type`、`level`、`sensor`、`message`、`resolved:false`。

### 5.4 `/api/config`

当前返回并保存这些本地配置：

```json
{
  "collect_interval": 1,
  "mqtt": {
    "mode": "ubuntu-ingest"
  },
  "alarm_thresholds": {
    "alcohol_high": 1800,
    "motion_delta": 120
  },
  "downlink": {
    "enabled": false,
    "status": "local-only"
  }
}
```

`PUT /api/config` 已实现本地保存，会写入 `--config` 指定的 JSON 文件。当前不会发布 MQTT 配置命令，也不会通知设备端立即变更。

## 六、Payload 解析

`src/ingest/payload_parser.c` 使用轻量字符串解析，不依赖 cJSON/Jansson。它会先检查输入是否是完整 JSON 对象，然后按固定 key 查找字段。解析到至少一个有效遥测字段才算成功。

默认值：

- `device` 默认 `device_01`。
- `timestamp` 默认当前 `time(NULL)`。
- `raw_payload` 最多保存到 `device_sample_t.raw_payload[2048]`。

### 6.1 普通扁平 JSON 字段

```text
device
timestamp
temperature 或 temp
humidity 或 hum
alcohol_raw 或 alcohol
alcohol_level
alcohol_alarm
accel_x
accel_y
accel_z
motion_state
motion_alarm
flame_status
flame_valid
dht11_temperature
dht11_humidity
dht11_valid
gps_lat 或 lat
gps_lon 或 lon
gps_speed
gps_satellites
gps_fence_alarm
```

### 6.2 OneNET 属性字段

OneNET 形式：

```json
{
  "id": "10001",
  "params": {
    "tempval": {"value": 26},
    "humval": {"value": 58}
  }
}
```

当前支持的属性名：

```text
tempval
humval
aclval
accx
accy
accz
motionst
flamest
dht11hum
dht11temp
gpslat
gpslon
gpsspd
gpssat
gpsfence
```

当前未解析设备端可能上报的 `gpsalt` 和天气属性 `wday`、`wweek`、`wcity`、`wtext`、`wtemp`。

## 七、SQLite 数据模型

`storage_open()` 会启用 WAL 并创建两张表。

### 7.1 原始采样表

```sql
CREATE TABLE IF NOT EXISTS device_samples (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device TEXT NOT NULL,
    sample_time INTEGER NOT NULL,
    temperature REAL,
    humidity REAL,
    alcohol_raw INTEGER,
    alcohol_level INTEGER,
    alcohol_alarm INTEGER,
    accel_x INTEGER,
    accel_y INTEGER,
    accel_z INTEGER,
    motion_state INTEGER,
    motion_alarm INTEGER,
    flame_status INTEGER,
    flame_valid INTEGER,
    dht11_temperature INTEGER,
    dht11_humidity INTEGER,
    dht11_valid INTEGER,
    gps_lat REAL,
    gps_lon REAL,
    gps_speed REAL,
    gps_satellites INTEGER,
    gps_fence_alarm INTEGER,
    raw_payload TEXT
);
```

索引：

```sql
CREATE INDEX IF NOT EXISTS idx_device_samples_time
ON device_samples(device, sample_time);
```

### 7.2 曲线查询表

```sql
CREATE TABLE IF NOT EXISTS sensor_samples_flat (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device TEXT NOT NULL,
    sample_time INTEGER NOT NULL,
    sensor_name TEXT NOT NULL,
    value REAL NOT NULL
);
```

索引：

```sql
CREATE INDEX IF NOT EXISTS idx_flat_sensor_time
ON sensor_samples_flat(sensor_name, sample_time);
```

每次写入采样后，当前会展开这些曲线字段：

```text
temperature
humidity
alcohol_raw
alcohol_level
alcohol_alarm
accel_x
accel_y
accel_z
motion_state
motion_alarm
flame_status
flame_valid
dht11_temperature
dht11_humidity
dht11_valid
gps_lat
gps_lon
gps_speed
gps_satellites
gps_fence_alarm
```

## 八、前端实现

`www/assets/js/app.js` 负责所有页面的数据加载和演示登录。

### 8.1 登录模型

```text
AUTH_KEY      sensor-web-auth
DEFAULT_USER  admin
DEFAULT_PASS  admin123
```

登录成功后写入：

```javascript
localStorage.setItem('sensor-web-auth', 'ok')
```

页面保护只发生在浏览器端：

- 未登录访问业务页面会跳到 `/login.html`。
- 已登录访问登录页会跳到 `/index.html`。
- 退出登录会删除 localStorage key。

后端不检查 Cookie 或 Session。设置 `SENSOR_WEB_TOKEN` 后会检查 `Authorization: Bearer <token>` 或 `X-Device-Token: <token>`；未设置时接口保持开放，便于本地联调但不适合公网部署。

### 8.2 页面行为

- 实时页每 2 秒请求 `/api/realtime`，并刷新最近告警。
- 历史页请求 `/api/history?sensor=<select>&limit=100`。
- 告警页请求 `/api/alarms?limit=30`。
- 配置页请求 `GET /api/config`，保存时调用 `PUT /api/config`。
- 前端主要使用 `textContent`、`createTextNode()` 和 DOM 节点创建来渲染 API 数据，避免把设备上报值直接拼进 HTML。

## 九、设备端与 MQTT/OneNET 对齐

父级工程 `project/app` 当前已包含设备端采集和 MQTT 上报：

- `project/app/src/core/main.c` 注册 `collect_thread`、`weather_thread`、`lvgl_thread`、`sqlite_thread`、`mqtt_thread`、`can_thread`、`gps_thread`。
- `project/app/include/core/linkqueue.h` 的 `data_t` 包含温湿度、酒精、加速度、天气、CAN 远端 DHT11/火焰、GPS 和电子围栏字段。
- `project/app/src/comm/mqtt.c` 使用 OneNET 属性上报主题：

```text
$sys/<product_id>/<device_name>/thing/property/post
```

设备端上报的主要属性 key 与 Web 端 parser 对齐：

```text
tempval, humval, aclval,
accx, accy, accz, motionst,
flamest, dht11hum, dht11temp,
gpslat, gpslon, gpsalt, gpsspd, gpssat, gpsfence
```

其中 `gpsalt` 当前 Web 端暂未解析。

## 十、MQTT / OneNET 接入方式

### 10.1 自建 Mosquitto

设备端把 Broker 改为 Ubuntu：

```c
#define MQTT_BROKER "tcp://Ubuntu服务器IP:1883"
```

Ubuntu 安装 Broker：

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

验证订阅：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'sensor/+/telemetry' -v
```

转发到 Web：

```bash
./scripts/mqtt_forward.sh 127.0.0.1 1883 'sensor/+/telemetry' http://127.0.0.1:8080/ingest/telemetry
```

注意：`mqtt_forward.sh` 期望 `mosquitto_sub` 输出每行都是纯 JSON payload；调试时可以用 `-v` 看 topic，转发脚本不要使用 `-v`。如果 Web 后端设置了 `SENSOR_WEB_TOKEN`，转发脚本同样要设置这个环境变量，脚本会自动加入 `X-Device-Token`。

### 10.2 OneNET 数据流转

如果设备继续上报 OneNET，推荐在平台侧配置数据流转，把属性上报推送到：

```text
POST http://Ubuntu服务器IP:8080/ingest/onenet
```

如果平台权限支持应用侧订阅，也可以用 OneNET 官方 topic 规则订阅后，把收到的 JSON payload 原样转发到 `/ingest/onenet`。

## 十一、Ubuntu 部署

### 11.1 依赖

```bash
sudo apt update
sudo apt install -y build-essential make libsqlite3-dev sqlite3 curl mosquitto-clients
```

如需本机 Broker：

```bash
sudo apt install -y mosquitto
sudo systemctl enable --now mosquitto
```

### 11.2 编译与测试

```bash
cd web_project
make clean
make
make test
```

Makefile 当前链接：

```text
-lsqlite3 -pthread
```

没有链接 `-lpaho-mqtt3c`。

### 11.3 本地运行

```bash
./bin/webserver -p 8080 -r ./www --db ./sensor_history.db --config ./web_config.json
```

或：

```bash
make run
```

访问：

```text
http://Ubuntu服务器IP:8080/login.html
```

默认只监听 `127.0.0.1`。需要从局域网浏览器访问时显式设置：

```bash
export SENSOR_WEB_BIND=0.0.0.0
export SENSOR_WEB_TOKEN='换成一段长随机字符串'
```

### 11.4 systemd 示例

```bash
sudo mkdir -p /opt/sensor-web
sudo cp -r bin www scripts /opt/sensor-web/
```

`/etc/systemd/system/sensor-web.service`：

```ini
[Unit]
Description=Sensor Monitor Web Server
After=network-online.target
Wants=network-online.target

[Service]
WorkingDirectory=/opt/sensor-web
Environment=SENSOR_WEB_TOKEN=change-this-long-random-token
Environment=SENSOR_WEB_CORS_ORIGIN=http://Ubuntu服务器IP:8080
Environment=SENSOR_WEB_BIND=0.0.0.0
ExecStart=/opt/sensor-web/bin/webserver -p 8080 -r /opt/sensor-web/www --db /opt/sensor-web/sensor_history.db --config /opt/sensor-web/web_config.json
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

启动：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now sensor-web
sudo systemctl status sensor-web
```

可选 MQTT 转发服务：

```ini
[Unit]
Description=Forward MQTT telemetry to Sensor Web
After=network-online.target sensor-web.service mosquitto.service
Wants=network-online.target

[Service]
WorkingDirectory=/opt/sensor-web
Environment=SENSOR_WEB_TOKEN=change-this-long-random-token
ExecStart=/opt/sensor-web/scripts/mqtt_forward.sh 127.0.0.1 1883 sensor/+/telemetry http://127.0.0.1:8080/ingest/telemetry
Restart=always

[Install]
WantedBy=multi-user.target
```

## 十二、测试与验证命令

### 12.1 单元测试

```bash
make test
```

预期输出：

```text
payload/cache tests passed
web config tests passed
```

### 12.2 手工接口测试

```bash
curl -X POST http://127.0.0.1:8080/ingest/telemetry \
  -H 'Content-Type: application/json' \
  -d '{"device":"device_01","timestamp":1719849600,"temperature":25.6,"humidity":55,"alcohol_raw":980,"alcohol_level":0,"alcohol_alarm":false,"accel_x":12,"accel_y":-8,"accel_z":981,"motion_state":0,"motion_alarm":false,"flame_status":0,"flame_valid":true,"dht11_temperature":26,"dht11_humidity":55,"dht11_valid":true,"gps_lat":34.2614,"gps_lon":108.9404,"gps_speed":12.5,"gps_satellites":7,"gps_fence_alarm":false}'

curl http://127.0.0.1:8080/api/realtime
curl http://127.0.0.1:8080/api/status
curl "http://127.0.0.1:8080/api/history?sensor=temperature&limit=20"
curl "http://127.0.0.1:8080/api/export?sensor=temperature&limit=100"
curl "http://127.0.0.1:8080/api/alarms?limit=20"
curl http://127.0.0.1:8080/api/config
curl -X PUT http://127.0.0.1:8080/api/config \
  -H 'Content-Type: application/json' \
  -d '{"collect_interval":5,"alarm_thresholds":{"alcohol_high":2200,"motion_delta":300}}'
```

### 12.3 SQLite 验证

```bash
sqlite3 sensor_history.db ".tables"
sqlite3 sensor_history.db "select device,sample_time,temperature,humidity from device_samples order by id desc limit 5;"
sqlite3 sensor_history.db "select sensor_name,sample_time,value from sensor_samples_flat order by id desc limit 10;"
```

### 12.4 基础安全探测

```bash
curl "http://127.0.0.1:8080/../../../etc/passwd"
curl -X POST http://127.0.0.1:8080/ingest/telemetry -d 'not json'
curl -X GET http://127.0.0.1:8080/ingest/telemetry
curl "http://127.0.0.1:8080/api/history?sensor=temp' OR '1'='1&limit=5"
```

预期：

- 路径穿越返回 403 或 404。
- 非 JSON ingest 返回 400。
- 非 POST ingest 返回 405。
- SQL 注入字符串只作为 `sensor` 参数绑定，不应改变查询结构。

## 十三、安全审计结论

### 13.1 当前已有防护

- SQLite 写入和查询使用 `sqlite3_prepare_v2()` 与 bind 参数。
- JSON 输出中的设备名、sensor 名会进行字符串转义。
- 静态路径包含 `..`、控制字符或反斜杠时拒绝访问，并使用 `realpath()` 校验目标仍位于 `www_root` 内。
- 请求体长度受 `REQ_BUF_SIZE=65536` 限制，过大返回 413。
- `Content-Length`、请求头长度、HTTP 方法、chunked/Transfer-Encoding 都有基础校验。
- 设置 `SENSOR_WEB_TOKEN` 后，API 和 ingest 接口具备最小 token 鉴权。
- 客户端 socket 设置 2 秒读写超时。
- 前端 API 数据通过 DOM 安全渲染，不直接拼接 HTML。

### 13.2 当前风险

- Token 鉴权默认不启用；未设置 `SENSOR_WEB_TOKEN` 时，登录只保护浏览器页面，不保护接口。
- 服务默认只绑定 `127.0.0.1`；如果设置 `SENSOR_WEB_BIND=0.0.0.0` 且未启用 token 或网关控制，任何能访问端口的人都能向 `/ingest/*` 伪造遥测并写库。
- JSON 响应默认使用 `Access-Control-Allow-Origin: *`；可用 `SENSOR_WEB_CORS_ORIGIN` 收紧。
- 没有 HTTPS，公网传输会泄露凭据和数据。
- 没有速率限制、IP 黑白名单、请求签名或防重放。
- 轻量 JSON 解析器不是完整 JSON 库，适合固定字段演示，不适合作为公网不可信输入边界。
- 当前部分遥测缺字段时，缺失字段会按 0/default 写入实时缓存和历史表；长期运行应引入字段 presence bitmask 或合并最近完整样本。
- 静态文件已经做 `realpath()` 根目录校验；但静态根目录本身仍不应允许非可信用户写入。
- 无访问日志和安全审计日志。
- 设备端 OneNET `MQTT_PASSWORD` 等凭据在配置头文件中，公开或生产环境应改为外部密钥并轮换。

### 13.3 上线前建议

1. 用 Nginx 反向代理加 HTTPS。
2. 在 Nginx 或后端增加 Basic Auth、Token 或会话鉴权。
3. 给 `/ingest/*` 增加设备签名、共享密钥或内网访问限制。
4. 收紧 CORS，只允许可信域名。
5. 增加速率限制和请求体大小策略。
6. 将 OneNET 产品 ID、设备名、Token/Password 移出源码。
7. 使用成熟 JSON 库替代字符串解析。
8. 增加访问日志、错误日志和安全告警。

## 十四、已知限制

- 不支持内置 MQTT 订阅；需要 `scripts/mqtt_forward.sh` 或 OneNET 数据流转。
- `PUT /api/config` 只保存 Ubuntu 本地配置，不支持 Web 下发设备参数。
- 不支持 WebSocket 推送，前端采用 2 秒轮询。
- 不支持 HTTPS、速率限制、HTTP keep-alive、chunked request、daemon 模式、日志文件参数。
- 当前测试覆盖 payload/cache 和 web_config，没有覆盖 HTTP 路由、SQLite、前端和端到端链路。
- 当前 Web 端不解析天气字段和 GPS 海拔字段。

## 十五、后续开发建议

按优先级建议：

1. 后端鉴权和 `/ingest/*` 设备认证。
2. Nginx HTTPS 部署模板。
3. 内置 MQTT 客户端或稳定的 systemd MQTT 转发服务。
4. 在当前本地配置基础上增加设备端 MQTT 配置下发。
5. 使用 cJSON/Jansson 替换手写字段解析。
6. 增加 HTTP/SQLite/端到端测试。
7. 增加访问日志和更细的健康检查指标。

---

**版本**: v1.4  
**审计日期**: 2026-07-06  
**适用目录**: `D:\桌面\linux_projects\web_project`  
