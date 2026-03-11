#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
OTA 升级测试工具
用于验证 Bootloader 和 OTA 功能的测试脚本
"""

import struct
import zlib
import sys

def calculate_crc32(data):
    """计算数据的 CRC32 值"""
    return zlib.crc32(data) & 0xFFFFFFFF

def create_app_header(app_size, version=1):
    """
    创建应用程序头

    Args:
        app_size: 应用程序大小（不包括头）
        version: 版本号

    Returns:
        bytes: 256 字节的应用程序头
    """
    header = bytearray(256)

    # 魔数
    struct.pack_into('<I', header, 0, 0x12345678)

    # 版本号
    struct.pack_into('<I', header, 4, version)

    # 应用程序大小
    struct.pack_into('<I', header, 8, app_size)

    # CRC32（先填 0，由烧写工具计算）
    struct.pack_into('<I', header, 12, 0)

    return bytes(header)

def add_app_header_to_elf(elf_file, output_file, version=1):
    """
    将应用程序头添加到 ELF 文件

    Args:
        elf_file: 输入的 .axf 或 .elf 文件
        output_file: 输出的二进制文件
        version: 版本号
    """
    try:
        with open(elf_file, 'rb') as f:
            elf_data = f.read()

        # 计算 CRC32
        crc = calculate_crc32(elf_data)

        # 创建头部
        header = bytearray(256)
        struct.pack_into('<I', header, 0, 0x12345678)  # 魔数
        struct.pack_into('<I', header, 4, version)      # 版本号
        struct.pack_into('<I', header, 8, len(elf_data)) # 大小
        struct.pack_into('<I', header, 12, crc)          # CRC32

        # 合并头部和应用程序
        output_data = bytes(header) + elf_data

        with open(output_file, 'wb') as f:
            f.write(output_data)

        print(f"✓ 创建应用镜像: {output_file}")
        print(f"  - 版本: v{version}")
        print(f"  - 大小: {len(elf_data)} bytes")
        print(f"  - CRC32: 0x{crc:08X}")
        print(f"  - 总大小（含头）: {len(output_data)} bytes")

        return True

    except Exception as e:
        print(f"✗ 错误: {e}")
        return False

def create_dummy_firmware(size, output_file):
    """
    创建虚拟固件用于测试

    Args:
        size: 固件大小
        output_file: 输出文件名
    """
    firmware = bytearray(size)

    # 填充测试数据（简单的递增模式）
    for i in range(size):
        firmware[i] = i & 0xFF

    # 添加应用程序头
    header = bytearray(256)
    crc = calculate_crc32(bytes(firmware))

    struct.pack_into('<I', header, 0, 0x12345678)  # 魔数
    struct.pack_into('<I', header, 4, 1)             # 版本
    struct.pack_into('<I', header, 8, size)          # 大小
    struct.pack_into('<I', header, 12, crc)          # CRC32

    # 写入文件
    with open(output_file, 'wb') as f:
        f.write(bytes(header) + bytes(firmware))

    print(f"✓ 创建虚拟固件: {output_file}")
    print(f"  - 固件大小: {size} bytes")
    print(f"  - CRC32: 0x{crc:08X}")

def verify_firmware_crc(firmware_file):
    """
    验证固件的 CRC32

    Args:
        firmware_file: 固件文件路径

    Returns:
        bool: CRC 验证是否通过
    """
    try:
        with open(firmware_file, 'rb') as f:
            data = f.read()

        # 读取应用程序头
        magic = struct.unpack('<I', data[0:4])[0]
        version = struct.unpack('<I', data[4:8])[0]
        app_size = struct.unpack('<I', data[8:12])[0]
        stored_crc = struct.unpack('<I', data[12:16])[0]

        if magic != 0x12345678:
            print(f"✗ 魔数无效: 0x{magic:08X}")
            return False

        # 计算应用程序的 CRC
        app_data = data[256:256+app_size]
        calculated_crc = calculate_crc32(app_data)

        print(f"固件检查:")
        print(f"  - 魔数: 0x{magic:08X} {'✓' if magic == 0x12345678 else '✗'}")
        print(f"  - 版本: {version}")
        print(f"  - 大小: {app_size} bytes")
        print(f"  - 存储 CRC: 0x{stored_crc:08X}")
        print(f"  - 计算 CRC: 0x{calculated_crc:08X}")
        print(f"  - CRC 验证: {'✓ 通过' if stored_crc == calculated_crc else '✗ 失败'}")

        return stored_crc == calculated_crc

    except Exception as e:
        print(f"✗ 错误: {e}")
        return False

def main():
    """主函数"""
    if len(sys.argv) < 2:
        print("OTA 升级工具使用方法:")
        print()
        print("1. 创建应用镜像（添加头部）:")
        print("   python ota_tool.py create_image <input.axf> <output.bin> [version]")
        print()
        print("2. 创建虚拟固件（用于测试）:")
        print("   python ota_tool.py create_dummy <size_in_bytes> <output.bin>")
        print()
        print("3. 验证固件 CRC：")
        print("   python ota_tool.py verify <firmware.bin>")
        print()
        print("示例：")
        print("   python ota_tool.py create_image app.axf app_with_header.bin 1")
        print("   python ota_tool.py create_dummy 50000 test_firmware.bin")
        print("   python ota_tool.py verify app_with_header.bin")
        return 1

    cmd = sys.argv[1]

    if cmd == 'create_image' and len(sys.argv) >= 4:
        version = int(sys.argv[4]) if len(sys.argv) > 4 else 1
        return 0 if add_app_header_to_elf(sys.argv[2], sys.argv[3], version) else 1

    elif cmd == 'create_dummy' and len(sys.argv) >= 4:
        size = int(sys.argv[2])
        create_dummy_firmware(size, sys.argv[3])
        return 0

    elif cmd == 'verify' and len(sys.argv) >= 3:
        return 0 if verify_firmware_crc(sys.argv[2]) else 1

    else:
        print("✗ 未知命令或参数不足")
        return 1

if __name__ == '__main__':
    sys.exit(main())
