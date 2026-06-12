#!/usr/bin/env python3
"""
CAN 协议解析脚本
用法: python parse_candump.py <candump日志文件>
"""

import sys
import re
from datetime import datetime

# 项目 CAN 协议定义
NODE_IDS = {
    0x01: "F103",
    0x02: "F407",
}

FRAME_TYPES = {
    "heartbeat": (0x100, 0x1FF),  # 0x100 + node_id
    "response":  (0x200, 0x2FF),  # 0x200 + node_id
    "command":   (0x300, 0x3FF),  # 0x300 + node_id
}

COMMANDS = {
    0x01: "PING",
    0x02: "READ_STATUS",
    0x03: "READ_DATA",
    0x04: "SET_CONFIG",
    0x05: "RESET",
}

STATUSES = {
    0x00: "OK",
    0x01: "ERROR",
    0x02: "BUSY",
    0x03: "TIMEOUT",
}


def parse_frame_id(can_id):
    """解析 CAN 帧 ID"""
    for frame_type, (start, end) in FRAME_TYPES.items():
        if start <= can_id <= end:
            node_id = can_id - start
            node_name = NODE_IDS.get(node_id, f"Unknown(0x{node_id:02X})")
            return frame_type, node_id, node_name
    return "unknown", can_id, "Unknown"


def parse_heartbeat(data):
    """解析心跳帧"""
    if len(data) < 8:
        return f"数据长度错误: {len(data)}"
    status = data[0]
    counter = data[7]
    status_str = STATUSES.get(status, f"Unknown(0x{status:02X})")
    return f"心跳: status={status_str}, counter={counter}"


def parse_response(data):
    """解析应答帧"""
    if len(data) < 8:
        return f"数据长度错误: {len(data)}"
    cmd = data[0]
    status = data[1]
    cmd_str = COMMANDS.get(cmd, f"Unknown(0x{cmd:02X})")
    status_str = STATUSES.get(status, f"Unknown(0x{status:02X})")
    return f"应答: cmd={cmd_str}, status={status_str}, data={data[2:6].hex()}"


def parse_command(data):
    """解析命令帧"""
    if len(data) < 8:
        return f"数据长度错误: {len(data)}"
    cmd = data[0]
    param1 = data[1]
    param2 = data[2]
    cmd_str = COMMANDS.get(cmd, f"Unknown(0x{cmd:02X})")
    return f"命令: cmd={cmd_str}, param1=0x{param1:02X}, param2=0x{param2:02X}"


def parse_can_line(line):
    """解析单行 candump 输出"""
    # candump 格式: (timestamp) interface can_id [length] data
    # 例如: (1234.567) can0 1FF [8] 01 02 03 04 05 06 07 08

    # 匹配模式
    pattern = r'\((\d+\.\d+)\)\s+(\w+)\s+([0-9A-Fa-f]+)\s+\[(\d+)\]\s+([0-9A-Fa-f\s]+)'
    match = re.match(pattern, line.strip())

    if not match:
        # 尝试其他格式
        pattern2 = r'(\w+)\s+([0-9A-Fa-f]+)\s+\[(\d+)\]\s+([0-9A-Fa-f\s]+)'
        match = re.match(pattern2, line.strip())
        if match:
            timestamp = "N/A"
            interface = "can0"
            can_id_str = match.group(1)
            length = int(match.group(2))
            data_str = match.group(3)
        else:
            return None
    else:
        timestamp = match.group(1)
        interface = match.group(2)
        can_id_str = match.group(3)
        length = int(match.group(4))
        data_str = match.group(5)

    # 解析 CAN ID
    try:
        can_id = int(can_id_str, 16)
    except ValueError:
        return None

    # 解析数据
    try:
        data = [int(x, 16) for x in data_str.split()]
    except ValueError:
        data = []

    # 解析帧类型
    frame_type, node_id, node_name = parse_frame_id(can_id)

    # 解析数据内容
    if frame_type == "heartbeat":
        detail = parse_heartbeat(data)
    elif frame_type == "response":
        detail = parse_response(data)
    elif frame_type == "command":
        detail = parse_command(data)
    else:
        detail = f"未知帧类型: data={data_str}"

    return {
        "timestamp": timestamp,
        "interface": interface,
        "can_id": can_id,
        "can_id_hex": f"0x{can_id:03X}",
        "node_id": node_id,
        "node_name": node_name,
        "frame_type": frame_type,
        "length": length,
        "data": data,
        "detail": detail,
    }


def validate_frame(frame):
    """验证帧是否符合协议规范"""
    warnings = []

    # 检查数据长度
    if frame["length"] != 8:
        warnings.append(f"数据长度不是 8 字节: {frame['length']}")

    # 检查帧类型
    if frame["frame_type"] == "unknown":
        warnings.append(f"帧 ID 不符合规范: {frame['can_id_hex']}")

    # 检查节点 ID
    if frame["node_id"] not in NODE_IDS and frame["frame_type"] != "unknown":
        warnings.append(f"未知节点 ID: 0x{frame['node_id']:02X}")

    return warnings


def main():
    if len(sys.argv) < 2:
        print("用法: python parse_candump.py <candump日志文件>")
        print("示例: python parse_candump.py can_log.txt")
        sys.exit(1)

    log_file = sys.argv[1]

    try:
        with open(log_file, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"错误: 文件不存在 {log_file}")
        sys.exit(1)

    print(f"解析文件: {log_file}")
    print(f"共 {len(lines)} 行")
    print("-" * 80)

    stats = {
        "total": 0,
        "heartbeat": 0,
        "command": 0,
        "response": 0,
        "unknown": 0,
        "warnings": 0,
    }

    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue

        frame = parse_can_line(line)
        if frame is None:
            print(f"[{line_num}] 无法解析: {line}")
            continue

        stats["total"] += 1
        stats[frame["frame_type"]] = stats.get(frame["frame_type"], 0) + 1

        # 验证帧
        warnings = validate_frame(frame)
        if warnings:
            stats["warnings"] += len(warnings)

        # 输出解析结果
        print(f"[{frame['timestamp']}] {frame['interface']} {frame['can_id_hex']} "
              f"[{frame['length']}] {' '.join(f'{x:02X}' for x in frame['data'])}")
        print(f"  -> {frame['detail']}")

        if warnings:
            for w in warnings:
                print(f"  ⚠️  {w}")

        print()

    # 输出统计
    print("=" * 80)
    print("统计:")
    print(f"  总帧数: {stats['total']}")
    print(f"  心跳帧: {stats['heartbeat']}")
    print(f"  命令帧: {stats['command']}")
    print(f"  应答帧: {stats['response']}")
    print(f"  未知帧: {stats['unknown']}")
    print(f"  警告数: {stats['warnings']}")


if __name__ == "__main__":
    main()
