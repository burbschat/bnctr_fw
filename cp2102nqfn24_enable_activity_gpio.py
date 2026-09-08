#!/usr/bin/env python

import argparse
import os
import usb.core

VID = 0x10C4
PID = 0xEA60
SIZE = 678
BACKUP = "cp2102n-backup.bin"


def fletcher16(data):
    a = b = 0xFF

    for i in range(0, len(data), 20):
        for x in data[i : i + 20]:
            a += x
            b += a
        a = (a & 0xFF) + (a >> 8)
        b = (b & 0xFF) + (b >> 8)

    a = (a & 0xFF) + (a >> 8)
    b = (b & 0xFF) + (b >> 8)

    return (b << 8) | a


def read_config(dev):
    return bytearray(dev.ctrl_transfer(0xC0, 0xFF, 0x000E, 0, SIZE))  # vendor IN  # READ_2NCONFIG


def write_config(dev, data):
    dev.ctrl_transfer(0x40, 0xFF, 0x370F, 0, data)  # vendor OUT  # SET_CONFIG


def check_config(data):
    if len(data) != SIZE:
        raise RuntimeError(f"Expected {SIZE} bytes")

    stored = (data[676] << 8) | data[677]
    calculated = fletcher16(data[:676])

    if stored != calculated:
        raise RuntimeError(f"Bad checksum: {stored:04x} != {calculated:04x}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["enable", "restore"])
    args = parser.parse_args()

    dev = usb.core.find(idVendor=VID, idProduct=PID)

    if dev is None:
        raise RuntimeError("CP2102N not found")

    if args.action == "enable":
        data = read_config(dev)
        check_config(data)

        print(f"GPIO_CONTROL: 0x{data[600]:02x}")

        if not os.path.exists(BACKUP):
            with open(BACKUP, "wb") as f:
                f.write(data)
            print(f"Backup saved: {BACKUP}")
        else:
            print(f"Using existing backup: {BACKUP}")

        # QFN24:
        # GPIO.0 = TXLED (bit 2)
        # GPIO.1 = RXLED (bit 3)
        data[600] |= 0x0C

        checksum = fletcher16(data[:676])
        data[676] = checksum >> 8
        data[677] = checksum & 0xFF

        print(f"New GPIO_CONTROL: 0x{data[600]:02x}")
        print(f"New checksum:     0x{checksum:04x}")

        write_config(dev, data)

        print("Configuration written.")
        print("Power-cycle the CP2102N.")

    else:
        if not os.path.exists(BACKUP):
            raise RuntimeError(f"{BACKUP} not found")

        with open(BACKUP, "rb") as f:
            data = bytearray(f.read())

        check_config(data)

        print(f"Restoring GPIO_CONTROL: " f"0x{data[600]:02x}")

        write_config(dev, data)

        print("Original configuration restored.")
        print("Power-cycle the CP2102N.")


if __name__ == "__main__":
    main()
