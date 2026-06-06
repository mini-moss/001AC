# Phase 6 — 驱动框架

> **预估周期**：1–2 周
> **进入门槛**：Phase 5 完成
> **退出标志**：能在实物板子上控制电机 + 读编码器 + 读 IMU

## 6.1 统一驱动模型

参考 Linux 风格，引入 `device` 抽象：

```c
// include/kernel/device.h
struct device {
    const char           *name;
    const void           *config;     // 板级配置（来自 Kconfig / devicetree）
    const struct device_ops *ops;
    void                 *priv;       // 驱动私有数据
    uint32_t              open_count;
    bool                  initialized;
};

struct device_ops {
    int  (*init)(struct device *dev);
    int  (*open)(struct device *dev);
    int  (*close)(struct device *dev);
    int  (*read)(struct device *dev, void *buf, size_t len);
    int  (*write)(struct device *dev, const void *buf, size_t len);
    int  (*ioctl)(struct device *dev, int cmd, void *arg);
};

int  device_register(struct device *dev);
struct device *device_get(const char *name);

static inline int device_read(struct device *dev, void *buf, size_t len) {
    return dev->ops->read(dev, buf, len);
}
// write / ioctl 类似
```

**注册流程**（编译期 + 链接段）：

```c
// drivers/bus/uart.c
static struct device uart1_dev = {
    .name  = "uart1",
    .ops   = &uart_stm32_ops,
    .priv  = &uart1_data,
};
DEVICE_REGISTER(uart1_dev);   // 链接脚本把 DEVICE_REGISTER 实例放在 .device_array 段
```

## 6.2 公共总线驱动

### GPIO
```c
int gpio_config(int pin, gpio_mode_t mode);
int gpio_write(int pin, bool value);
bool gpio_read(int pin);

// 中断
typedef void (*gpio_irq_cb_t)(void *arg);
int gpio_irq_attach(int pin, gpio_edge_t edge, gpio_irq_cb_t cb, void *arg);
int gpio_irq_enable(int pin);
int gpio_irq_disable(int pin);
```

### UART（含 DMA）
```c
struct uart_cfg {
    uint32_t baud;
    uint8_t  data_bits;   // 7/8/9
    uint8_t  stop_bits;   // 1/2
    uint8_t  parity;      // none/even/odd
    bool     use_dma;
    size_t   rx_buf_size; // DMA 接收环形 buffer
};

int uart_open(struct device *dev, const struct uart_cfg *cfg);
int uart_write(struct device *dev, const void *buf, size_t len);
int uart_read(struct device *dev, void *buf, size_t len, tick_t timeout);  // 阻塞
// 中断 / 事件通知通过 callback
typedef void (*uart_rx_cb_t)(void *arg, size_t avail);
int uart_set_rx_cb(struct device *dev, uart_rx_cb_t cb, void *arg);
```

**DMA 接收流程**：
1. 启动 DMA 接收，到缓冲区满触发中断
2. 中断中交换缓冲区（双 buffer 切换）
3. 通知等待任务 / 调 rx callback

### SPI
```c
int spi_open(struct device *dev, const struct spi_cfg *cfg);
int spi_transfer(struct device *dev,
                 const void *tx, void *rx, size_t len);   // 全双工
int spi_transfer_async(struct device *dev, spi_done_cb_t cb, void *arg);
```

### I2C
```c
int i2c_open(struct device *dev, uint32_t khz);
int i2c_read(struct device *dev, uint8_t addr, void *buf, size_t len);
int i2c_write(struct device *dev, uint8_t addr, const void *buf, size_t len);
int i2c_write_read(struct device *dev, uint8_t addr,
                   const void *wbuf, size_t wlen,
                   void *rbuf, size_t rlen);   // 寄存器读通用模式
```

### CAN
```c
typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  len;
    uint8_t  format;   // 标准帧 / 扩展帧
} can_frame_t;

int can_send(struct device *dev, const can_frame_t *frame);
int can_set_filter(struct device *dev, uint32_t id, uint32_t mask);
typedef void (*can_rx_cb_t)(const can_frame_t *frame, void *arg);
int can_set_rx_cb(struct device *dev, can_rx_cb_t cb, void *arg);
```

## 6.3 机器人专用驱动

### PWM（电机调速 / 舵机）

```c
struct pwm_cfg {
    uint32_t freq_hz;
    uint16_t duty_pct;     // 0–10000 (万分比精度)
    bool     center_aligned;
};

int pwm_open(struct device *dev, int channel, const struct pwm_cfg *cfg);
int pwm_set_duty(struct device *dev, int channel, uint16_t duty_pct);
```

**常见需求**：
- 50Hz 舵机（20ms 周期，0.5–2.5ms 高电平）
- 25kHz 直流电机调速
- 多路同步（如机械臂多关节）

### 编码器（四倍频正交解码）

```c
int encoder_open(struct device *dev, encoder_mode_t mode);
// mode: X1 / X2 / X4

int32_t encoder_get_count(struct device *dev);
int     encoder_set_count(struct device *dev, int32_t v);
int     encoder_set_cb(struct device *dev,
                       encoder_cb_t cb, void *arg);  // 溢出中断回调
```

**实现**：
- 用定时器的编码器模式（硬件支持 X4 解码）
- 中断：上下溢出（32 位计数器）

### IMU（常见型号：MPU6050 / ICM-20948）

```c
typedef struct {
    int16_t ax, ay, az;   // 加速度 (原始值)
    int16_t gx, gy, gz;   // 角速度
    int16_t mx, my, mz;   // 磁力（可选）
    uint32_t timestamp_us;
} imu_data_t;

int imu_open(struct device *dev);
int imu_read(struct device *dev, imu_data_t *out);    // 阻塞 / 立即
int imu_set_cb(struct device *dev, imu_cb_t cb, void *arg);  // 数据就绪回调
```

**典型数据流**：
1. IMU 数据就绪引脚触发外部中断
2. 中断中 I2C 读 14 字节（加速度 + 角速度）
3. 放入静态内存池 buffer
4. 通知等待任务 / 触发 callback

### 里程计 / 运动学层

不属于内核，但建议在 `drivers/robot/` 下放参考实现：

```c
typedef struct {
    float wheel_radius;     // m
    float wheel_base;       // m（差速底盘轮距）
} diff_drive_param_t;

int diff_drive_init(const diff_drive_param_t *p);
void diff_drive_update(float left_enc_delta, float right_enc_delta,
                        float *dx, float *dtheta);  // 输出机体坐标系位移
```

## 6.4 设备配置：Kconfig

引入 Linux 风格的 Kconfig，避免硬编码：

```kconfig
# drivers/Kconfig
menuconfig DRIVERS
    bool "Enable driver framework"
    default y

config DRIVER_UART
    bool "UART driver"
    select SERIAL
    help
      Support for UART peripherals with optional DMA.

config DRIVER_I2C
    bool "I2C driver"
    help
      Master-mode I2C with bit-bang fallback.

config DRIVER_PWM
    bool "PWM output"
    help
      Used for DC motor speed control and servo position.

config DRIVER_ENCODER
    bool "Quadrature encoder"
    help
      4x decoding using hardware timer.

config DRIVER_IMU_MPU6050
    bool "MPU6050 IMU"
    select DRIVER_I2C
    depends on DRIVERS
```

**自动生成**：
- `config.h`：所有 `CONFIG_*` 宏
- 链接时：未启用的驱动不参与链接

## 6.5 第一个机器人 demo

```c
// app/robot_demo/main.c
static struct device *motor_l, *motor_r, *enc_l, *enc_r, *imu;

void control_task(void *arg) {
    tick_t last = tick_get();
    while (1) {
        // 5ms 控制周期
        task_sleep_until(&last, 5);

        int32_t enc_l_cnt = encoder_get_count(enc_l);
        int32_t enc_r_cnt = encoder_get_count(enc_r);

        imu_data_t im;
        imu_read(imu, &im);

        // 简化的差速底盘速度估计
        // ... 控制律 ...

        pwm_set_duty(motor_l, 0, duty_l);
        pwm_set_duty(motor_r, 0, duty_r);
    }
}

int main(void) {
    board_init();

    motor_l = device_get("pwm1");
    motor_r = device_get("pwm2");
    enc_l   = device_get("enc1");
    enc_r   = device_get("enc2");
    imu     = device_get("imu0");

    // 初始化 + 配置
    ...

    sched_init();
    idle_init();
    task_create("control", control_task, NULL, 2048, 5);
    sched_start();
}
```

## 验证标准

- [ ] 所有驱动单元测试通过（mock 总线）
- [ ] 实物板子上电机能按设定 PWM 输出（用示波器看波形）
- [ ] 编码器计数随电机转动单调递增，X4 解码方向正确
- [ ] IMU 数据能连续读出，无明显丢点
- [ ] 5ms 控制周期抖动 < 50 μs（在 1kHz 调度下）
- [ ] `printk` 调试命令能列出所有已注册设备

## 常见坑

- ⚠️ I2C 总线多个设备地址冲突：用 `i2c_scan()` 调试
- ⚠️ UART 接收用 DMA + 双 buffer，**不要**用单 buffer + 中断搬数据（CPU 占用过高）
- ⚠️ 编码器在掉电时会丢位置：上电时手动归零
- ⚠️ IMU 启动后必须等待 100ms 让 DMP / 内部校准稳定
- ⚠️ 多电机同步：用一个 timer 触发所有 PWM 通道，避免逐个触发的相位差

## 接下来

进入 [Phase 7 — micro-ROS 集成](phase-7-microros.md)，让机器人节点能加入 ROS 2 生态。
