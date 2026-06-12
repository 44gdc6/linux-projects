#!/usr/bin/env python3
"""
CAN 协议一致性验证脚本
用法: python verify_can.py <节点ID> [candump日志文件]
"""

import sys
import os
import re
import subprocess

# 项目 CAN 协议定义
NODE_IDS = {
    0x01: {"name": "F103", "platform": "STM32F103C8T6"},
    0x02: {"name": "F407", "platform": "STM32F407ZGTx"},
}

FRAME_TYPES = {
    "heartbeat": (0x100, 0x1FF),
    "response":  (0x200, 0x2FF),
    "command":   (0x300, 0x3FF),
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

# 测试文件路径
TEST_PATHS = [
    "stm32-firmware/F407/tests/",
    "stm32-firmware/UART/tests/",
]


def find_test_files(node_id):
    """查找节点对应的测试文件"""
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

    if node_id == 0x01:
        test_dir = os.path.join(project_root, "stm32-firmware/UART/tests")
    elif node_id == 0x02:
        test_dir = os.path.join(project_root, "stm32-firmware/F407/tests")
    else:
        return []

    if not os.path.exists(test_dir):
        return []

    test_files = []
    for f in os.listdir(test_dir):
        if f.startswith("test_") and f.endswith(".py"):
            test_files.append(os.path.join(test_dir, f))

    return test_files


def run_contract_tests(node_id):
    """运行 Python 契约测试"""
    project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

    print("运行 Python 契约测试...")
    print("-" * 60)

    if node_id == 0x01:
        test_module = "tests.test_can_source_contracts"
        test_dir = os.path.join(project_root, "stm32-firmware/UART")
    elif node_id == 0x02:
        test_module = "tests.test_can_multinode_source_contracts"
        test_dir = os.path.join(project_root, "stm32-firmware/F407")
    else:
        print(f"错误: 未知节点 ID 0x{node_id:02X}")
        return False

    if not os.path.exists(test_dir):
        print(f"警告: 测试目录不存在 {test_dir}")
        return False

    cmd = f"cd {test_dir} && python -m unittest {test_module} -v"
    print(f"执行命令: {cmd}")
    print()

    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=60)
        print(result.stdout)
        if result.stderr:
            print("STDERR:")
            print(result.stderr)
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        print("错误: 测试超时 (60秒)")
        return False
    except Exception as e:
        print(f"错误: {e}")
        return False


def parse_candump_line(line):
    """解析 candump 行"""
    pattern = r'\((\d+\.\d+)\)\s+(\w+)\s+([0-9A-Fa-f]+)\s+\[(\d+)\]\s+([0-9A-Fa-f\s]+)'
    match = re.match(pattern, line.strip())
    if match:
        return {
            "timestamp": float(match.group(1)),
            "can_id": int(match.group(3), 16),
            "length": int(match.group(4)),
            "data": [int(x, 16) for x in match.group(5).split()],
        }
    return None


def verify_heartbeat_interval(frames, node_id, expected_interval=1.0, tolerance=0.1):
    """验证心跳间隔"""
    print("\n验证心跳间隔...")
    print("-" * 60)

    heartbeat_frames = []
    for frame in frames:
        frame_type = None
        for ft, (start, end) in FRAME_TYPES.items():
            if start <= frame["can_id"] <= end:
                frame_type = ft
                break

        if frame_type == "heartbeat":
            extracted_node_id = frame["can_id"] - 0x100
            if extracted_node_id == node_id:
                heartbeat_frames.append(frame)

    if len(heartbeat_frames) < 2:
        print(f"⚠️  心跳帧不足 2 个，无法验证间隔 (收到 {len(heartbeat_frames)} 个)")
        return True

    intervals = []
    for i in range(1, len(heartbeat_frames)):
        interval = heartbeat_frames[i]["timestamp"] - heartbeat_frames[i-1]["timestamp"]
        intervals.append(interval)

    avg_interval = sum(intervals) / len(intervals)
    min_interval = min(intervals)
    max_interval = max(intervals)

    print(f"心跳帧数量: {len(heartbeat_frames)}")
    print(f"平均间隔: {avg_interval:.3f}s")
    print(f"最小间隔: {min_interval:.3f}s")
    print(f"最大间隔: {max_interval:.3f}s")
    print(f"期望间隔: {expected_interval:.3f}s ± {tolerance:.3f}s")

    # 检查是否在容差范围内
    all_valid = all(abs(i - expected_interval) <= tolerance for i in intervals)
    if all_valid:
        print("✅ 心跳间隔正常")
    else:
        print("❌ 心跳间隔异常")
        for i, interval in enumerate(intervals):
            if abs(interval - expected_interval) > tolerance:
                print(f"  第 {i+1} 个间隔 {interval:.3f}s 超出范围")

    return all_valid


def verify_response_for_command(frames, node_id):
    """验证应答帧是否对应命令帧"""
    print("\n验证命令-应答对应关系...")
    print("-" * 60)

    commands = []
    responses = []

    for frame in frames:
        frame_type = None
        for ft, (start, end) in FRAME_TYPES.items():
            if start <= frame["can_id"] <= end:
                frame_type = ft
                break

        extracted_node_id = frame["can_id"] & 0x0FF
        if extracted_node_id != node_id:
            continue

        if frame_type == "command":
            commands.append(frame)
        elif frame_type == "response":
            responses.append(frame)

    print(f"命令帧数量: {len(commands)}")
    print(f"应答帧数量: {len(responses)}")

    if len(commands) == 0:
        print("⚠️  没有命令帧，跳过验证")
        return True

    if len(responses) == 0:
        print("❌ 没有收到应答帧")
        return False

    # 检查每个命令是否都有应答
    matched = 0
    for cmd in commands:
        cmd_data = cmd["data"]
        if len(cmd_data) > 0:
            cmd_type = cmd_data[0]
            cmd_str = COMMANDS.get(cmd_type, f"0x{cmd_type:02X}")

            # 查找对应的应答 (在命令之后)
            for resp in responses:
                if resp["timestamp"] > cmd["timestamp"]:
                    resp_data = resp["data"]
                    if len(resp_data) > 0:
                        resp_cmd = resp_data[0]
                        if resp_cmd == cmd_type:
                            matched += 1
                            break

    print(f"匹配的命令-应答: {matched}/{len(commands)}")

    if matched == len(commands):
        print("✅ 所有命令都收到应答")
        return True
    else:
        print("❌ 部分命令未收到应答")
        return False


def verify_frame_id_range(frames, node_id):
    """验证帧 ID 范围"""
    print("\n验证帧 ID 范围...")
    print("-" * 60)

    errors = []

    for frame in frames:
        can_id = frame["can_id"]
        extracted_node_id = can_id & 0x0FF

        # 检查是否是当前节点的帧
        if extracted_node_id != node_id:
            continue

        # 检查帧类型范围
        frame_type = None
        for ft, (start, end) in FRAME_TYPES.items():
            if start <= can_id <= end:
                frame_type = ft
                break

        if frame_type is None:
            errors.append(f"帧 ID 0x{can_id:03X} 不符合规范")

    if errors:
        print("❌ 帧 ID 范围错误:")
        for e in errors:
            print(f"  - {e}")
        return False
    else:
        print("✅ 帧 ID 范围正确")
        return True


def verify_data_length(frames, node_id):
    """验证数据长度"""
    print("\n验证数据长度...")
    print("-" * 60)

    errors = []

    for frame in frames:
        can_id = frame["can_id"]
        extracted_node_id = can_id & 0x0FF

        if extracted_node_id != node_id:
            continue

        if frame["length"] != 8:
            errors.append(f"帧 0x{can_id:03X} 数据长度 {frame['length']} != 8")

    if errors:
        print("❌ 数据长度错误:")
        for e in errors:
            print(f"  - {e}")
        return False
    else:
        print("✅ 数据长度正确 (全部为 8 字节)")
        return True


def main():
    if len(sys.argv) < 2:
        print("用法: python verify_can.py <节点ID> [candump日志文件]")
        print("示例: python verify_can.py 0x01 can_log.txt")
        print("      python verify_can.py 0x02")
        sys.exit(1)

    # 解析节点 ID
    node_id_str = sys.argv[1]
    try:
        if node_id_str.startswith("0x"):
            node_id = int(node_id_str, 16)
        else:
            node_id = int(node_id_str)
    except ValueError:
        print(f"错误: 无效的节点 ID {node_id_str}")
        sys.exit(1)

    if node_id not in NODE_IDS:
        print(f"错误: 未知节点 ID 0x{node_id:02X}")
        print(f"支持的节点: {', '.join(f'0x{k:02X}' for k in NODE_IDS.keys())}")
        sys.exit(1)

    node_info = NODE_IDS[node_id]
    print(f"验证节点: {node_info['name']} (0x{node_id:02X})")
    print(f"平台: {node_info['platform']}")
    print("=" * 60)

    # 运行契约测试
    print("\n[1/5] 运行源码契约测试")
    test_result = run_contract_tests(node_id)

    # 如果提供了日志文件，进行额外验证
    if len(sys.argv) >= 3:
        log_file = sys.argv[2]

        if not os.path.exists(log_file):
            print(f"错误: 日志文件不存在 {log_file}")
            sys.exit(1)

        print(f"\n[2/5] 解析日志文件: {log_file}")
        with open(log_file, 'r') as f:
            lines = f.readlines()

        frames = []
        for line in lines:
            frame = parse_candump_line(line)
            if frame:
                frames.append(frame)

        print(f"解析到 {len(frames)} 帧")

        # 验证帧 ID 范围
        print("\n[3/5] 验证帧 ID 范围")
        id_result = verify_frame_id_range(frames, node_id)

        # 验证数据长度
        print("\n[4/5] 验证数据长度")
        length_result = verify_data_length(frames, node_id)

        # 验证心跳间隔
        print("\n[5/5] 验证心跳间隔")
        heartbeat_result = verify_heartbeat_interval(frames, node_id)

        # 汇总结果
        print("\n" + "=" * 60)
        print("验证结果汇总:")
        print(f"  契约测试: {'✅ 通过' if test_result else '❌ 失败'}")
        print(f"  帧 ID 范围: {'✅ 通过' if id_result else '❌ 失败'}")
        print(f"  数据长度: {'✅ 通过' if length_result else '❌ 失败'}")
        print(f"  心跳间隔: {'✅ 通过' if heartbeat_result else '❌ 失败'}")

        all_pass = test_result and id_result and length_result and heartbeat_result
    else:
        print("\n" + "=" * 60)
        print("验证结果汇总:")
        print(f"  契约测试: {'✅ 通过' if test_result else '❌ 失败'}")
        print("  (未提供日志文件，跳过其他验证)")
        all_pass = test_result

    print("\n" + "=" * 60)
    if all_pass:
        print("✅ 总体验证通过")
        sys.exit(0)
    else:
        print("❌ 验证失败")
        sys.exit(1)


if __name__ == "__main__":
    main()
