# Phase 8 — 高级功能

> **预估周期**：长期
> **进入门槛**：Phase 7 完成
> **退出标志**：对应模块的功能测试通过

本章是**可选模块集合**，按需启用。每个模块都是独立的子项目，先想清楚是否真的需要。

## 8.1 文件系统（FatFS）

**适用场景**：记录日志、保存配置参数、地图数据。

### 集成

```c
// 选 [FatFS](http://elm-chan.org/fsw/ff/)（单文件、领域标准）
// third_party/fatfs/
```

### 包装成 VFS

```c
// include/fs/vfs.h
struct file;
struct dir;

int  vfs_open(const char *path, int flags, struct file **f);
int  vfs_read(struct file *f, void *buf, size_t len);
int  vfs_write(struct file *f, const void *buf, size_t len);
int  vfs_close(struct file *f);
int  vfs_stat(const char *path, struct stat *st);

// mount
int  vfs_mount(const char *mount_point, const char *dev_name);
int  vfs_umount(const char *mount_point);
```

注册块设备：

```c
// drivers/storage/sdcard.c
static int sd_read_blocks(struct device *dev, uint32_t sector, void *buf, size_t count);
static int sd_write_blocks(struct device *dev, uint32_t sector, const void *buf, size_t count);

static const struct block_ops sd_ops = {
    .read  = sd_read_blocks,
    .write = sd_write_blocks,
};
BLOCK_DEVICE_REGISTER("sd0", &sd_ops, &sd_cfg);
```

### 实际使用

```c
// 写日志
struct file *f;
vfs_open("/fs/sd0/log.txt", O_WRONLY | O_APPEND | O_CREAT, &f);
vfs_write(f, "imu_calibrated\n", 15);
vfs_close(f);
```

**注意**：
- 实时路径不要阻塞写文件；用后台任务异步刷盘
- SD 卡有寿命：用环形日志 / 按需写盘

## 8.2 网络协议栈（lwIP）

**适用场景**：上位机通信、OTA 升级、远程调试。

### 集成

```c
// third_party/lwip/
// port/sys_arch.c 实现 lwIP 的系统层适配
```

`sys_arch.c` 至少实现：

```c
// 互斥
sys_mutex_t sys_mutex_new(void);
void sys_mutex_lock(sys_mutex_t m);
void sys_mutex_unlock(sys_mutex_t m);
void sys_mutex_free(sys_mutex_t m);

// 信号量（lwIP 的 tcpip_thread 依赖）
sys_sem_t sys_sem_new(void);
void sys_sem_signal(sys_sem_t s);
void sys_arch_sem_wait(sys_sem_t s, uint32_t timeout);

// 邮箱
sys_mbox_t sys_mbox_new(void);
void sys_mbox_post(sys_mbox_t m, void *msg);
void sys_arch_mbox_fetch(sys_mbox_t m, void **msg, uint32_t timeout);
```

### tcpip_thread 启动

```c
void lwip_init_and_run(void) {
    lwip_init();
    // 启动 tcpip_thread
    tcpip_init(NULL, NULL);
    // 启动网卡轮询
    task_create("eth_rx", ethernetif_input_task, NULL, 1024, 6);
    task_create("eth_tx", low_level_output_task, NULL, 1024, 6);
}
```

### 性能目标

| 指标 | 目标 |
|---|---|
| TCP 吞吐（100Mbps） | > 50 Mbps |
| UDP 抖动 | < 100 μs |
| 并发 socket | ≥ 8 |
| RAM 占用 | < 32 KB（PBUF + TCP 控制块） |

**不做什么**：
- ❌ 不实现 TLS（用 mbedTLS，集成成本高）
- ❌ 不实现 HTTP Server（用 mongoose / embeddable web server）
- ❌ 不实现多网卡绑定

## 8.3 GUI（LVGL）

**适用场景**：带屏幕的机器人（如服务机器人示教器）。

### 集成

```c
// third_party/lvgl/
// drivers/display/...
```

### 显示设备抽象

```c
struct display_ops {
    int  (*init)(struct device *dev);
    int  (*flush)(struct device *dev, const void *framebuf, struct display_rect *rect);
    int  (*set_brightness)(struct device *dev, uint8_t level);
};

int display_register(struct device *dev);
```

### 触摸输入

```c
struct input_ops {
    int (*init)(struct device *dev);
    int (*read)(struct device *dev, struct input_event *ev);  // 阻塞或非阻塞
    int (*set_cb)(struct device *dev, input_event_cb_t cb, void *arg);
};
```

### RTOS 集成要点

```c
// LVGL 需要 1ms 周期 tick
void lvgl_tick_task(void *arg) {
    tick_t last = tick_get();
    while (1) {
        task_sleep_until(&last, 1);
        lv_tick_inc(1);
    }
}

// LVGL task handler 放低优先级
void lvgl_task(void *arg) {
    while (1) {
        lv_task_handler();
        task_sleep(10);
    }
}
```

**性能目标**：
- 480×320 单缓冲 60Hz 满屏刷新 < 16ms（需 SPI ≥ 40MHz）
- 双缓冲可消除撕裂，但 RAM 翻倍

**注意**：
- LVGL 自带对象系统，会消耗 20–50KB RAM
- 复杂动画放单独任务，避免影响控制环

## 8.4 Shell（命令行）

**强烈建议**：所有 RTOS 都该有。

```c
// lib/shell/shell.c
void shell_run(struct device *uart) {
    printk("Robot RTOS Shell v0.1\n");
    printk("> ");
    char line[128];
    int  pos = 0;
    while (1) {
        char c;
        if (uart_read(uart, &c, 1, TIMEOUT_NEVER) != 1) continue;
        if (c == '\r' || c == '\n') {
            line[pos] = '\0';
            shell_exec(line);
            pos = 0;
            printk("> ");
        } else if (c == 127 || c == '\b') {  // backspace
            if (pos > 0) { pos--; uart_write(uart, "\b \b", 3); }
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = c;
            uart_write(uart, &c, 1);
        }
    }
}
```

**命令示例**：

| 命令 | 作用 |
|---|---|
| `ps` | 列出所有任务与状态 |
| `mem` | 内存池统计 |
| `top` | 任务 CPU 占用（基于 tick 统计） |
| `reboot` | 系统复位 |
| `log set <level>` | 调整日志级别 |
| `drv list` | 列出所有注册设备 |

## 8.5 日志系统

```c
// include/kernel/log.h
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

#define LOG_D(tag, fmt, ...)  log_impl(LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...)  log_impl(LOG_INFO,  tag, fmt, ##__VA_ARGS__)
// ...

void log_impl(log_level_t lvl, const char *tag, const char *fmt, ...);
```

**特性**：
- 级别过滤：编译期 + 运行时
- 异步输出：日志先入 ring buffer，独立任务刷到 UART/SD
- 时间戳：每条带 tick / 上位机 ROS time

## 8.6 升级与安全

### OTA 升级

```c
// lib/ota/ota.c
// 1. 上位机推送固件（micro-ROS service 或 HTTP）
// 2. 写到外部 flash 备份区
// 3. 校验签名（ECDSA 或 SHA256 + RSA）
// 4. 切换启动区，重启
```

### 签名校验

```c
// 用 mbedTLS 验签
// 烧录时把公钥 hash 写入 OTP / protected flash
// 启动时校验固件签名，否则拒绝启动
```

> **重要**：在量产固件中**必须**做签名校验，否则攻击者可刷恶意固件。

## 8.7 不在本期范围

明确划出**不做**的边界：

- ❌ **TLS 1.3**：太重，建议用 WireGuard / 自定义加密
- ❌ **完整 USB 协议栈**：先做 CDC-ACM（虚拟串口）够用
- ❌ **图形加速器驱动**：除非目标平台有 GPU
- ❌ **音频 / 视频处理**：机器人场景优先业务逻辑

## 验证标准（按子模块）

### 文件系统
- [ ] 写 10MB 文件速度 > 5MB/s
- [ ] 意外断电不破坏已有数据
- [ ] 多任务并发打开不同文件不冲突

### 网络
- [ ] `ping` 通
- [ ] TCP echo 回环测试
- [ ] 与上位机 iperf 测速达 50Mbps

### GUI
- [ ] 60Hz 满屏刷新 < 16ms
- [ ] 触摸响应延迟 < 50ms

### Shell
- [ ] 全部命令在串口终端可用
- [ ] 历史命令支持上箭头（可选）

## 接下来

进入 [Phase 9 — 质量保障](phase-9-quality.md)，把以上所有功能变成可发布的稳定版本。
