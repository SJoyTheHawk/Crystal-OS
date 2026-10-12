# Crystal OS recovery tools

## USB recovery

The recovery tool uses the ESP-IDF build metadata in `build/flasher_args.json`,
so it follows the offsets generated for the current partition table. Build the
firmware first, then connect the board over USB and identify its serial port.

Install the only external dependency with:

```bash
python3 -m pip install esptool
```

Preserve mode restores the bootloader, partition table, OTA metadata, and
application image. It leaves NVS settings, the SPIFFS storage partition, and
the coredump partition untouched:

```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build --preserve
```

Factory reset asks for two confirmations, erases the complete flash, and
flashes every image listed by ESP-IDF:

```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build --factory-reset
```

Extract the coredump partition without changing the device. The partition
offset and size are read from `partition-table.bin`:

```bash
./tools/crystal_recovery.py --port /dev/cu.usbserial-0001 --firmware build \
  --extract-coredump /tmp/crystal-coredump.bin
esp-coredump.py info_corefile -t raw build/crystal_os.elf /tmp/crystal-coredump.bin
```

Use `CRYSTAL_ESPTOOL` to select a particular executable or wrapper, for
example `CRYSTAL_ESPTOOL="python3 -m esptool"`. The tool streams esptool's
progress and returns a non-zero status when a confirmation, validation, or
flash operation fails.
