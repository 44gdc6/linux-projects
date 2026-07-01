# MPU6050 传感器驱动从零开发 - 教学计划

## 目标
从零开始编写 Linux I2C 字符设备驱动 + 用户空间应用程序，教会用户理解每一行代码。

## 开发环境
- **目标板**: i.MX6ULL (ARM Cortex-A7)
- **内核版本**: Linux 4.1.15
- **工具链**: arm-linux-gnueabihf-gcc
- **部署方式**: NFS 根文件系统 (~/nfs/rootfs/)

## 教学阶段

| 阶段 | 内容 | 状态 |
|------|------|------|
| 阶段1 | 基础知识：MPU6050 芯片与 Linux I2C 驱动架构 | ✅ complete |
| 阶段2 | 搭建驱动骨架：头文件、模块入口 | ✅ complete |
| 阶段3 | I2C 驱动框架：probe/remove、设备匹配表 | ✅ complete |
| 阶段4 | 寄存器读写：I2C SMBus 通信函数 | ✅ complete |
| 阶段5 | 芯片初始化：ID验证、唤醒、配置传感器参数 | ✅ complete |
| 阶段6 | 数据采集：突发读取14字节、大端序解析 | ✅ complete |
| 阶段7 | 用户接口：misc 设备注册、file_operations | ✅ complete |
| 阶段8 | 应用层：打开设备、读取数据、转换为物理量 | ✅ complete |

## 文件清单

| 文件 | 行数 | 说明 |
|------|------|------|
| `mpu6050_drv/mpu6050_drv.c` | 419 行 | 内核驱动（完整） |
| `mpu6050_app/mpu6050_app.c` | ~110 行 | 用户空间应用（完整） |
| `mpu6050_drv/Makefile` | - | Kbuild 模块编译 |
| `mpu6050_app/Makefile` | - | 交叉编译 |
| `Makefile` | - | 顶层串联 |

## 关键决策
- 使用 miscdevice 而非手动创建设备节点（更简洁）
- 使用 i2c_smbus 系列函数而非原始 i2c_transfer（更易用）
- 数据以原始值传出，转换由应用层完成（符合 Unix 哲学）
- 全局变量 g_client 保存 i2c_client（简单场景可行，多芯片需改用 i2c_set_clientdata）
