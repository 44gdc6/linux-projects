# 危险品运输安全监控 Ubuntu Web 端

这是 `web_project` 目录下已经实现的 Ubuntu Web 管理端。它不是板端程序的一部分，而是在 Ubuntu 上独立运行：接收设备或平台转发来的遥测 JSON，更新内存中的最新状态，写入 SQLite，并提供浏览器页面和 HTTP API。

当前实现重点是可演示、可联调、可落库。C 后端本身没有内置 MQTT 客户端；MQTT 数据通过 `scripts/mqtt_forward.sh` 使用 `mosquitto_sub + curl` 转发到 HTTP 接口。OneNET 建议用平台数据流转或应用侧转发，把属性上报推送到 Web 后端。

## 当前已实现

- C Web 后端：单进程 epoll 监听，静态文件服务，`sendfile()` 发送静态资源，JSON API 响应。
- Web 页面：`login.html`、`index.html`、`history.html`、`alarms.html`、`config.html`。
- 演示登录：默认账号 `admin`，默认密码 `admin123`，前端用 `localStorage` 控制页面跳转；如设置 `SENSOR_WEB_TOKEN`，后端会校验 `/api/*` 和 `/ingest/*` 请求的 Bearer token 或 `X-Device-Token`。
- 数据接入：`POST /ingest/telemetry` 和 `POST /ingest/onenet`，两者当前走同一套 JSON 解析和落库逻辑。
- 实时缓存：最近一次有效遥测进入内存缓存，供 `/api/realtime` 返回。
- SQLite 落库：自动创建 `device_samples` 原始采样表和 `sensor_samples_flat` 曲线查询表。
- 运行状态：`GET /api/status` 返回监听地址、鉴权状态、路径、最近遥测和本地配置。
- 告警查询：`/api/alarms` 查询 `alcohol_alarm`、`motion_alarm`、`gps_fence_alarm` 或 `flame_status` 非零的记录。
- 历史导出：`GET /api/export` 可导出 CSV。
- 本地配置：`GET/PUT /api/config` 读取和保存 Ubuntu 本地配置文件。
- MQTT 转发脚本：`scripts/mqtt_forward.sh` 从 MQTT 主题读取 payload 并 POST 到 Web 后端。
- 单元测试：覆盖扁平遥测、OneNET 属性 payload、实时缓存 JSON 和 Web 配置保存/加载。

## 目录结构

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
├── scripts/mqtt_forward.sh
├── tests/test_payload_cache.c
├── tests/test_web_config.c
└── www/
    ├── login.html
    ├── index.html
    ├── history.html
    ├── alarms.html
    ├── config.html
    └── assets/
```

## Ubuntu 准备

```bash
sudo apt update
sudo apt install -y build-essential make libsqlite3-dev sqlite3 curl mosquitto-clients
```

如果设备直接发到 Ubuntu 自建 MQTT Broker，再安装 Mosquitto：

```bash
sudo apt install -y mosquitto
sudo systemctl enable --now mosquitto
```

## 编译、测试、运行

```bash
cd web_project
make clean
make
make test
./bin/webserver -p 8080 -r ./www --db ./sensor_history.db --config ./web_config.json
```

也可以直接使用 Makefile 的运行目标：

```bash
make run
```

常用启动参数：

```text
-p     HTTP 端口，默认 8080
-r     静态页面目录，默认 ./www
--db   SQLite 数据库路径，默认 ./sensor_history.db
--config Web 本地配置文件路径，默认 ./web_config.json
```

当前程序没有 `-d`、`-l`、`--mqtt-host`、`--mqtt-topic` 等参数；MQTT 接入由外部脚本完成。

默认只监听 `127.0.0.1`。需要从局域网浏览器访问时显式设置：

```bash
export SENSOR_WEB_BIND=0.0.0.0
export SENSOR_WEB_TOKEN='换成一段长随机字符串'
```

浏览器访问：

```text
http://Ubuntu服务器IP:8080/login.html
```

默认登录凭据：

```text
用户名：admin
密码：admin123
```

## 页面说明

- `/login.html`：前端演示登录页。
- `/index.html` 或 `/`：实时监控，显示设备、最后上报时间、风险状态、温湿度、酒精值、三轴加速度、GPS、CAN 节点和最近告警。
- `/history.html`：按传感器查询最近历史记录，并用前端柱状图展示趋势。
- `/alarms.html`：查看酒精、振动、电子围栏、火焰相关告警记录。
- `/config.html`：查看并保存 Ubuntu 本地 Web 配置，查看运行状态，更新当前浏览器保存的接口 Token；设备参数 MQTT 下发仍未启用。

## API 列表

```text
GET  /api/realtime
     返回最近一次遥测；无数据时返回 {"online":false,"message":"no telemetry received"}。

GET  /api/status
     返回监听地址、端口、路径、鉴权/CORS 状态、最近遥测和当前本地配置。

GET  /api/history?sensor=temperature&limit=100&start=1719840000&end=1719849600
     查询 sensor_samples_flat。sensor 常用值：
     temperature、humidity、alcohol_raw、alcohol_alarm、accel_x、accel_y、
     accel_z、motion_alarm、flame_status、dht11_temperature、dht11_humidity、
     gps_lat、gps_lon、gps_speed、gps_satellites、gps_fence_alarm。
     start/end 为可选 Unix 秒级时间戳。limit <= 0 或 > 1000 时回退为 100。

GET  /api/export?sensor=temperature&limit=1000
     导出 CSV。sensor/start/end 可选，limit 最大按后端响应缓冲控制。

GET  /api/alarms?limit=50
     查询酒精、振动、电子围栏、火焰告警。limit <= 0 或 > 500 时回退为 50。

GET  /api/config
     返回当前 Web 本地配置。

PUT  /api/config
     保存 collect_interval、alcohol_high、motion_delta 到 --config 指定的 JSON 文件。
     这只更新 Ubuntu 本地配置，不会下发到设备端。

POST /ingest/telemetry
     写入普通扁平 JSON 遥测。

POST /ingest/onenet
     写入 OneNET 属性格式 JSON。当前和 /ingest/telemetry 共用同一个解析器。
```

说明：除 `PUT /api/config` 外，`/api/*` 只允许 GET；`/ingest/*` 只允许 POST。其他方法返回 405。

如果启用了后端 Token：

```bash
export SENSOR_WEB_TOKEN='换成一段长随机字符串'
curl -H "Authorization: Bearer $SENSOR_WEB_TOKEN" http://127.0.0.1:8080/api/realtime
curl -X POST http://127.0.0.1:8080/ingest/telemetry \
  -H "X-Device-Token: $SENSOR_WEB_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"temperature":25.6}'
```

## 写入测试数据

服务运行后，在另一个终端写入一条普通遥测：

```bash
curl -X POST http://127.0.0.1:8080/ingest/telemetry \
  -H 'Content-Type: application/json' \
  -d '{"device":"device_01","timestamp":1719849600,"temperature":25.6,"humidity":55,"alcohol_raw":980,"alcohol_level":0,"alcohol_alarm":false,"accel_x":12,"accel_y":-8,"accel_z":981,"motion_state":0,"motion_alarm":false,"flame_status":0,"flame_valid":true,"dht11_temperature":26,"dht11_humidity":55,"dht11_valid":true,"gps_lat":34.2614,"gps_lon":108.9404,"gps_speed":12.5,"gps_satellites":7,"gps_fence_alarm":false}'
```

写入一条 OneNET 属性格式测试数据：

```bash
curl -X POST http://127.0.0.1:8080/ingest/onenet \
  -H 'Content-Type: application/json' \
  -d '{"id":"10001","params":{"tempval":{"value":26},"humval":{"value":58},"aclval":{"value":1100},"accx":{"value":4},"accy":{"value":5},"accz":{"value":980},"motionst":{"value":1},"flamest":{"value":0},"dht11hum":{"value":61},"dht11temp":{"value":27},"gpslat":{"value":34.2614},"gpslon":{"value":108.9404},"gpsspd":{"value":12.5},"gpssat":{"value":7},"gpsfence":{"value":0}}}'
```

检查实时状态、历史数据和告警：

```bash
curl http://127.0.0.1:8080/api/realtime
curl http://127.0.0.1:8080/api/status
curl "http://127.0.0.1:8080/api/history?sensor=temperature&limit=20"
curl "http://127.0.0.1:8080/api/alarms?limit=20"
```

## Payload 字段

普通扁平 JSON 当前支持这些字段或别名：

```text
device, timestamp,
temperature 或 temp,
humidity 或 hum,
alcohol_raw 或 alcohol,
alcohol_level, alcohol_alarm,
accel_x, accel_y, accel_z,
motion_state, motion_alarm,
flame_status, flame_valid,
dht11_temperature, dht11_humidity, dht11_valid,
gps_lat 或 lat, gps_lon 或 lon,
gps_speed, gps_satellites, gps_fence_alarm
```

OneNET 属性格式当前支持这些属性名：

```text
tempval, humval, aclval,
accx, accy, accz, motionst,
flamest, dht11hum, dht11temp,
gpslat, gpslon, gpsspd, gpssat, gpsfence
```

注意：当前 Web 后端不会根据 `aclval` 或 `motionst` 自动推导告警；告警列表依赖入库记录里的 `alcohol_alarm`、`motion_alarm`、`gps_fence_alarm` 或 `flame_status`。

## MQTT / OneNET 接入

### 方式 A：设备发到 Ubuntu Mosquitto

设备端把 MQTT Broker 改为 Ubuntu 服务器：

```c
#define MQTT_BROKER "tcp://Ubuntu服务器IP:1883"
```

建议遥测主题：

```text
sensor/device_01/telemetry
```

Ubuntu 上先确认能收到 MQTT 数据：

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t 'sensor/+/telemetry' -v
```

再启动转发脚本。转发脚本读取的每一行必须是 JSON payload，因此这里不要给脚本内部的 `mosquitto_sub` 加 `-v`：

```bash
chmod +x scripts/mqtt_forward.sh
./scripts/mqtt_forward.sh 127.0.0.1 1883 'sensor/+/telemetry' http://127.0.0.1:8080/ingest/telemetry
```

如果 Web 后端设置了 `SENSOR_WEB_TOKEN`，转发脚本也要在同一环境里设置它，脚本会自动加入 `X-Device-Token`：

```bash
SENSOR_WEB_TOKEN='换成同一段 token' ./scripts/mqtt_forward.sh 127.0.0.1 1883 'sensor/+/telemetry' http://127.0.0.1:8080/ingest/telemetry
```

### 方式 B：继续使用 OneNET

现有设备端配置使用 OneNET MQTT 属性上报，主题形如：

```text
$sys/<product_id>/<device_name>/thing/property/post
```

如果设备继续上报 OneNET，不要假设 Ubuntu 可以像普通 Mosquitto 一样直接订阅所有设备上报主题。优先在 OneNET 平台配置数据流转，把属性上报推送到：

```text
POST http://Ubuntu服务器IP:8080/ingest/onenet
```

如果 OneNET 产品权限支持应用侧订阅，也可以按 OneNET 官方主题规则订阅后，把收到的 JSON payload 原样转发到 `/ingest/onenet`。

## Ubuntu systemd 部署示例

假设部署到 `/opt/sensor-web`：

```bash
sudo mkdir -p /opt/sensor-web
sudo cp -r bin www scripts /opt/sensor-web/
```

创建 `/etc/systemd/system/sensor-web.service`：

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

启动服务：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now sensor-web
sudo systemctl status sensor-web
```

如果还需要把本机 Mosquitto 的消息转发到 Web 后端，可另外创建 `/etc/systemd/system/sensor-mqtt-forward.service`：

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

## SQLite 查看

```bash
sqlite3 sensor_history.db ".tables"
sqlite3 sensor_history.db "select device,sample_time,temperature,humidity from device_samples order by id desc limit 5;"
sqlite3 sensor_history.db "select sensor_name,sample_time,value from sensor_samples_flat order by id desc limit 10;"
```

## 安全 caveats

- 登录页仍是前端演示登录：默认账号密码写在 `www/assets/js/app.js`，浏览器通过 `localStorage` 判断是否已登录。
- 后端 Token 鉴权是可选的：未设置 `SENSOR_WEB_TOKEN` 时，`/api/*` 与 `/ingest/*` 不做鉴权。默认只监听 `127.0.0.1`；如果设置 `SENSOR_WEB_BIND=0.0.0.0`，生产部署必须同时设置长随机 token 或放到 Nginx/内网网关后面。
- JSON API 默认响应 `Access-Control-Allow-Origin: *`；可通过 `SENSOR_WEB_CORS_ORIGIN` 收紧。
- 没有 HTTPS、CSRF 防护、速率限制、请求签名或防重放，公开暴露 `/ingest/*` 仍有伪造数据和数据库膨胀风险。
- JSON 解析器是轻量字符串解析器，不是完整 JSON 库；适合当前固定字段联调，不适合作为公网不可信输入边界。
- 静态文件服务会拒绝路径中包含 `..`、控制字符或反斜杠的请求，并用 `realpath()` 校验目标仍在静态根目录内；静态根目录本身仍不应允许非可信用户写入。
- 设备端 OneNET `MQTT_PASSWORD` 属于敏感凭据；生产或公开仓库中应改为环境变量、配置文件或密钥管理，并及时轮换已暴露凭据。

## 已知限制

- C 后端没有内置 MQTT 客户端，`scripts/mqtt_forward.sh` 需要单独运行。
- `/api/config` 只保存 Ubuntu 本地配置；设备端 MQTT 配置下发尚未实现。
- 当前部分遥测缺少字段时，缺失字段会按 0/default 写入；长期运行建议改成字段 presence bitmask 或合并最近完整样本。
- 当前 Web 端不解析天气字段和 GPS 海拔字段。
- 不支持 WebSocket、HTTPS、访问日志文件、速率限制、后台 daemon 参数、HTTP keep-alive 或 chunked body。
- 当前已有 payload/cache 和 web_config 单元测试，尚无 HTTP 路由、SQLite 存储、端到端浏览器测试。
