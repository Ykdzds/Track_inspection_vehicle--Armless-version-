"""仅把指定串口的RTU字节转发到本机R2模拟器；不连接主控运动串口。"""
import argparse
import socket
import time


def main():
    parser = argparse.ArgumentParser(description="实屏与本机R2模拟从站的串口桥")
    parser.add_argument("--port", required=True, help="明确指定连接屏的串口，不自动枚举或打开")
    parser.add_argument("--tcp-port", type=int, default=15013)
    parser.add_argument("--fault", choices=("none", "crc", "short", "truncate"), default="none", help="仅对03H模拟响应注入，供实屏拒收验收")
    args = parser.parse_args()
    import serial
    with socket.create_connection(("127.0.0.1", args.tcp_port), timeout=2) as stream:
        stream.settimeout(0.2)
        with serial.Serial(args.port, 115200, timeout=0.002, bytesize=8, parity="N", stopbits=1) as uart:
            pending = bytearray()
            last_received = time.monotonic()
            while True:
                chunk = uart.read(256)
                if chunk:
                    pending.extend(chunk)
                    last_received = time.monotonic()
                if pending and time.monotonic() - last_received >= 0.002:
                    request = bytes(pending)
                    pending.clear()
                    print(f"{time.monotonic():.6f} RX {request.hex(' ')}", flush=True)
                    stream.sendall(request)
                    response = bytearray()
                    try:
                        while True:
                            response.extend(stream.recv(256))
                            if len(response) >= 3:
                                size = 5 if response[1] & 0x80 else 5 + response[2] if response[1] == 3 else 8
                                if len(response) >= size:
                                    break
                    except socket.timeout:
                        continue
                    if response:
                        if response[1] == 3 and args.fault != "none":
                            if args.fault == "crc":
                                response[-1] ^= 1
                            elif args.fault == "truncate":
                                response = response[:-3]
                            elif response[2] >= 2:
                                response = response[:-4]
                                response[2] -= 2
                                crc = 65535
                                for value in response:
                                    crc ^= value
                                    for bit in range(8):
                                        crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
                                response.extend((crc & 255, crc >> 8))
                        uart.write(response)
                        uart.flush()
                        print(f"{time.monotonic():.6f} TX {response.hex(' ')}", flush=True)


if __name__ == "__main__":
    main()
