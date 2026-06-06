# Phase 7 — micro-ROS 集成

> **预估周期**：1–2 周
> **进入门槛**：Phase 6 完成
> **退出标志**：在实物板子发布 `Imu` 消息，上位机 `ros2 topic echo` 能看到数据

**不要自己实现 DDS**——直接集成 [micro-ROS](https://micro.ros.org/)，它是 ROS 2 官方 MCU 版本。

## 7.1 micro-ROS 架构概览

```
┌─────────────────────────────────────────┐
│   你的 RTOS 应用                         │
│   └─ uros_publisher_create(...)         │
├─────────────────────────────────────────┤
│   rclc / rcl                            │  ROS Client Library for C
├─────────────────────────────────────────┤
│   rmw_microxrcedds                      │  Real-time Middleware
├─────────────────────────────────────────┤
│   xrce-dds (Micro XRCE-DDS)             │  客户端协议
├─────────────────────────────────────────┤
│   transport (UART / UDP / TCP)          │  ← 你要实现的适配层
└─────────────────────────────────────────┘
        ↕ 串口 / 网络
┌─────────────────────────────────────────┐
│   上位机 (Linux)                          │
│   └─ Micro XRCE-DDS Agent               │
│   └─ ros2 topic echo / RViz             │
└─────────────────────────────────────────┘
```

## 7.2 集成准备

### 7.2.1 上位机 Agent

```bash
# Linux 上
sudo apt install ros-humble-micro-ros-agent
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
# 或串口版
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyUSB0 -b 115200
```

### 7.2.2 拉取 micro-ROS 库

作为第三方模块加入仓库（保留各自许可）：

```bash
cd third_party/
git clone -b humble https://github.com/micro-ROS/micro_ros_arduino.git micro_ros  # 或对应平台
```

或使用 [micro_ros_setup](https://github.com/micro-ROS/micro_ros_setup) 通过 colcon 静态构建（推荐用于自定义 RTOS）：

```bash
# 在 Linux 主机上
ros2 run micro_ros_setup create_firmware_ws.sh \
    ~/uros_ws ~/robot_rtos
ros2 run micro_ros_setup build_firmware.sh \
    ~/uros_ws ~/robot_rtos
```

生成 `libmicroros.a` 静态库后嵌入到 RTOS 构建。

## 7.3 适配层实现（关键）

micro-ROS 通过 `uros_transport` 抽象与硬件通信。**你只需实现一个 transport**：

```c
// third_party/micro_ros/transport_our_rtos.c
#include <uxr/client/profile/transport/custom/custom_transport.h>

#include <kernel/printk.h>
#include <drivers/uart.h>

static struct device *g_uart;
static uint8_t g_rx_byte;
static bool g_session_open = false;

// 同步打开 transport（一般 UDP 走异步、UART 走同步）
bool my_custom_transport_open(struct uxrCustomTransport *transport) {
    (void)transport;
    g_session_open = true;
    return true;
}

bool my_custom_transport_close(struct uxrCustomTransport *transport) {
    (void)transport;
    g_session_open = false;
    return true;
}

// 读一个字节，timeout 单位 ms
//  - 返回 0：超时
//  - 返回 1：读到
size_t my_custom_transport_read(struct uxrCustomTransport *transport,
                                uint8_t *buf, size_t len, int timeout_ms) {
    (void)transport;
    size_t got = 0;
    tick_t deadline = tick_get() + timeout_ms;
    while (got < len) {
        if (uart_read(g_uart, &buf[got], 1, 0) == 0) {
            if (tick_get() >= deadline) return got;  // 超时
            task_sleep(1);                            // 退让
            continue;
        }
        got++;
    }
    return got;
}

size_t my_custom_transport_write(struct uxrCustomTransport *transport,
                                 const uint8_t *buf, size_t len, int timeout_ms) {
    (void)transport;
    int n = uart_write(g_uart, buf, len);
    return (n < 0) ? 0 : (size_t)n;
}
```

## 7.4 客户端初始化

```c
// include/uros/uros.h
#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <std_msgs/msg/int32.h>
#include <sensor_msgs/msg/imu.h>

typedef struct {
    rcl_node_t     node;
    rclc_executor_t executor;
    rcl_allocator_t allocator;
    rclc_support_t support;
} uros_ctx_t;

int  uros_init(uros_ctx_t *ctx, const char *node_name, const char *ns);
int  uros_create_publisher(uros_ctx_t *ctx, const rosidl_message_type_support_t *ts,
                           const char *topic, rcl_publisher_t *pub);
int  uros_create_subscription(uros_ctx_t *ctx, ...,
                              const char *topic, rcl_subscription_t *sub,
                              rclc_subscription_callback_t cb);
void uros_spin_period(uros_ctx_t *ctx, tick_t period_ms);
```

### 7.4.1 初始化

```c
uros_ctx_t uros;
rcl_publisher_t imu_pub;
rcl_publisher_t cmd_vel_pub;
rcl_subscription_t vel_sub;

int main(void) {
    board_init();

    if (uros_init(&uros, "robot_node", "") != 0) {
        printk("uros init failed\n");
        return -1;
    }

    uros_create_publisher(&uros,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu),
        "/imu/data", &imu_pub);

    uros_create_publisher(&uros,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
        "/cmd_vel", &cmd_vel_pub);

    uros_create_subscription(&uros,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
        "/cmd_vel_raw", &vel_sub, &on_vel_cmd);

    // 把 spin 放到独立任务中
    task_create("uros_spin", uros_spin_task, &uros, 4096, 4);
    task_create("publish_imu", publish_imu_task, &imu_pub, 2048, 3);

    sched_start();
}
```

### 7.4.2 发布

```c
void publish_imu_task(void *arg) {
    rcl_publisher_t *pub = arg;
    tick_t last = tick_get();
    while (1) {
        task_sleep_until(&last, 10);  // 100Hz

        sensor_msgs__msg__Imu msg = {0};
        imu_data_t raw;
        imu_read(imu_dev, &raw);
        msg.linear_acceleration.x = raw.ax * G_PER_LSB;
        msg.linear_acceleration.y = raw.ay * G_PER_LSB;
        msg.linear_acceleration.z = raw.az * G_PER_LSB;
        msg.angular_velocity.x    = raw.gx * DEG_PER_LSB;
        msg.angular_velocity.y    = raw.gy * DEG_PER_LSB;
        msg.angular_velocity.z    = raw.gz * DEG_PER_LSB;
        msg.header.stamp.sec      = ...;
        msg.header.stamp.nanosec  = ...;

        rcl_publish(pub, &msg, NULL);
    }
}
```

### 7.4.3 订阅回调

```c
void on_vel_cmd(const void *msg_in, void *ctx) {
    const geometry_msgs__msg__Twist *m = msg_in;
    // 转换为左右轮速度 → 写入控制任务队列
    cmd_queue_push(m->linear.x, m->angular.z);
}

void uros_spin_task(void *arg) {
    uros_ctx_t *ctx = arg;
    while (1) {
        rclc_executor_spin_some(&ctx->executor, RCL_MS_TO_NS(10));
    }
}
```

## 7.5 UDP 传输（性能更好）

如果板子带以太网（推荐机器人主控），改用 UDP：

```c
// 用 lwIP 的 raw API 或 netconn
static int g_agent_sock = -1;

bool udp_transport_open(struct uxrCustomTransport *t) {
    g_agent_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(8888),
        .sin_addr.s_addr = inet_addr("192.168.1.100"),
    };
    connect(g_agent_sock, (struct sockaddr*)&addr, sizeof(addr));
    return true;
}
```

> micro-ROS UDP transport 在 100Mbps 网络下延迟 < 1ms，远优于 UART。

## 7.6 启动 / 关闭流程

```
MCU                                   上位机
 │                                       │
 │  ◄────────── XRCE Discovery ──────────► │
 │            (UDP / Serial)              │
 │                                       │
 │  ──────────── CREATE Participant ────► │
 │  ◄─────────── (session ID) ────────────│
 │                                       │
 │  ──────────── CREATE Topic ──────────► │
 │  ──────────── CREATE Publisher ──────► │
 │  ◄─────────── (data objects) ─────────│
 │                                       │
 │  ──────────── DATA(Imu) ──────────────►│
 │  ──────────── DATA(Imu) ──────────────►│
 │  ...                                   │
```

注意：micro-ROS 在每次发布前需要与 Agent 完成 XRCE 会话握手（约 100ms–2s）。**如果 Agent 还没起来，初始化会失败**——设计一个重试循环。

## 7.7 实时性配置

micro-ROS 默认 QoS 是 `RELIABLE` + `KEEP_LAST(10)`。机器人场景通常需要 `BEST_EFFORT` + `KEEP_LAST(1)`：

```c
rmw_qos_profile_t qos = rmw_qos_profile_sensor_data;  // 预定义的传感器 QoS
rcl_publisher_options_t opts = rcl_publisher_get_default_options();
opts.qos = qos;
```

## 7.8 调试清单

| 问题 | 排查 |
|---|---|
| Agent 收不到数据 | 检查 transport 的 `read/write` 字节数是否对得上 |
| 客户端超时 | 确认 Agent 已起来；串口版检查波特率 |
| Topic 看不到 | 检查 `ros2 topic list` 与 `ros2 node list` |
| 消息乱码 | 确认大小端（Micro XRCE-DDS 默认 little-endian） |
| 内存爆 | 默认 micro-ROS 用静态分配，预留 50KB RAM 缓冲 |

## 验证标准

- [ ] `ros2 topic list` 看到 `/imu/data`
- [ ] `ros2 topic hz /imu/data` 显示稳定 100Hz
- [ ] `ros2 topic echo /imu/data` 数据正常
- [ ] 上位机发 `ros2 topic pub /cmd_vel_raw ...`，板子上回调被触发
- [ ] 串口断连 / 重连后 micro-ROS 能自动重连
- [ ] CPU 占用 < 30%（500kHz 控制环 + 100Hz publish）

## 常见坑

- ⚠️ micro-ROS 的内存模型默认静态分配，**先预留 30–50KB RAM**
- ⚠️ rclc executor 的 `spin_some` 必须周期性调用，否则订阅不响应
- ⚠️ `rosidl_message_type_support_t` 宏包含的 `.h` 必须正确链接到库
- ⚠️ 如果多任务同时调 `rcl_publish`，需要加互斥锁（micro-ROS 内部非线程安全）
- ⚠️ Agent 关闭后 micro-ROS 会持续重试；如需优雅退出，单独处理

## 接下来

进入 [Phase 8 — 高级功能](phase-8-advanced.md)，按需扩展文件系统 / 网络 / GUI。
