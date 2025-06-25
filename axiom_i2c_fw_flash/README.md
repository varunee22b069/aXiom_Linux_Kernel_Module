# aXiom Linux Kernel Module - Firmware/Config Flashing

This directory contains the source code and test file for the aXiom touch controller Linux kernel module.

## Features

- Firmware update support (chunked download, bootloader entry, verification)
- Configuration update and CRC verification
- Character device interface for user-space tools
- IOCTL commands for firmware/configuration flashing and checksum retrieval
- Automatic device node creation in `/dev` (no manual `mknod`)
- Firmware and configuration files are loaded using the kernel's `request_firmware()` interface

## Directory Structure

- `axiom_core.c/h` – Core driver logic
- `axiom_i2c.c` – I2C communication
- `axiom_ioctl.c` – Character device and IOCTL interface
- `axiom_cfg.c` – Configuration update and CRC routines
- `axiom_fw.c` – Firmware update logic
- `Makefile` – For standalone out-of-tree kernel module build
- `test.c` – Example user-space test utility
- `README.md` – This file

## Building the Module

Ensure you have kernel headers installed for your running kernel.

To build the module:

```sh
make
```

This will produce a kernel module file named:

```
axiom_i2c_drv.ko
```

## Installing and Loading the Module

To install the module system-wide:

```sh
sudo make modules_install
sudo depmod -a
```

Or to load it manually for testing:

```sh
sudo insmod axiom_i2c_drv.ko
```

To remove:

```sh
sudo rmmod axiom_i2c_drv
```


## User-Space IOCTL Interface

Use the provided `test.c` as a reference for sending firmware/configuration files and retrieving checksums via IOCTL.

Supported IOCTLs:
- `AXIOM_CFG_UPDATE` – Flash configuration file
- `AXIOM_CFG_CHECKSUM` – Get configuration checksum
- `AXIOM_FW_UPDATE` – Flash firmware file
- `AXIOM_FW_CHECKSUM` – Get firmware version/checksum

## Preparing Firmware and Configuration Files for testing

Before testing the module, ensure the firmware and configuration files are available in the standard firmware search path (e.g., `/vendor/firmware/`).  
The filenames **must match** those defined in `axiom_test.c`:

To copy firmware and config files from current directory to `/vendor/firmware/`:
```sh
sudo cp <firmware_file_name> /vendor/firmware/axiom_firmware.alc
sudo cp <config_file_name> /vendor/firmware/axiom_config.bin
```

Then statically build the test file for the given architecture and execute.

## Test Files Directory

Sample test files (e.g., `axiom_test.c`), along with example firmware and
configuration binaries (`axiom_firmware.alc`, `axiom_config.bin`), are placed
inside the `test/` directory in this repo.

These are for testing and development only and are **not intended for upstream**.
Make sure to copy the binaries to the appropriate firmware directory before use.

## Important Notes

- The module name and output file is `axiom_i2c_drv.ko` (set in the Makefile).
- The bootloader reset command fails to reset the bootloader after flashing, therefore manual reset (Power Off, Power On) should be done for changes to reflect.

## Sources
- [TouchNetix axiom touchscreen driver Kernel Patch](https://patchwork.kernel.org/project/linux-input/patch/20231211121430.1689139-4-kamel.bouhara@bootlin.com/#25691996)
- [Axiom Linux Kernel Module Repo](https://github.com/TouchNetix/aXiom_Linux_Kernel_Module)
- [Axiom Pylib Repo](https://github.com/TouchNetix/axiom_pylib)

## License

This driver is licensed under the GNU General Public License v2.0.  
See the `SPDX-License-Identifier` in each source file.

## Authors

Karthik Choda <Karthik.Choda@harman.com> \
Varun Rajesh <Vaddi.Rajesh@harman.com>

## Maintainer Note

This version is a cleaned-up and functional port of vendor-provided Python-based firmware/configuration flashing logic, now integrated as a Linux kernel module with a character device and ioctl interface.

Some existing author tags were temporarily removed due to unclear source contribution across vendor and upstream. This will be re-evaluated with maintainers once the contribution history is clarified.
