# 研究发现与知识要点

## MPU6050 芯片关键信息
- 6轴 IMU：3轴加速度 + 3轴陀螺仪
- I2C 从机地址：0x68 (AD0=0) 或 0x69 (AD0=1)
- WHO_AM_I 寄存器：0x75，返回值 0x68
- 传感器数据从 0x3B 开始，共 14 字节（大端序）
- 上电默认进入 SLEEP 模式，需要写 PWR_MGMT_1=0 唤醒

## 寄存器地址速查
| 寄存器 | 地址 | 作用 |
|--------|------|------|
| WHO_AM_I | 0x75 | 芯片ID |
| PWR_MGMT_1 | 0x6B | 电源管理 |
| SMPLRT_DIV | 0x19 | 采样率分频 |
| CONFIG | 0x1A | 数字低通滤波 |
| GYRO_CONFIG | 0x1B | 陀螺仪量程 |
| ACCEL_CONFIG | 0x1C | 加速度计量程 |
| ACCEL_XOUT_H | 0x3B | 数据起始地址 |

## 数据寄存器布局（大端序）
| 偏移 | 数据 | 寄存器 |
|------|------|--------|
| 0-1 | ACCEL_X | 0x3B-0x3C |
| 2-3 | ACCEL_Y | 0x3D-0x3E |
| 4-5 | ACCEL_Z | 0x3F-0x40 |
| 6-7 | TEMP | 0x41-0x42 |
| 8-9 | GYRO_X | 0x43-0x44 |
| 10-11 | GYRO_Y | 0x45-0x46 |
| 12-13 | GYRO_Z | 0x47-0x48 |

## Linux I2C 驱动核心概念
- i2c_driver：驱动对象，包含 probe/remove 和匹配表
- i2c_client：代表总线上的一个 I2C 设备实例
- of_device_id：设备树匹配
- i2c_device_id：传统 ID 表匹配（兜底）
