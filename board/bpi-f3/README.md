# BPI-F3 Board Support Package

## Hardware
- **SoC**: SpacemiT K1 (8× RISC-V cores, RV64IMAFDCV)
- **RAM**: 2GB / 4GB LPDDR4X
- **Storage**: eMMC 16GB / 32GB
- **Display**: MIPI DSI, 1080×2400
- **Connectivity**: WiFi 6 + BT 5.2 (RTL8852BS), 4G LTE modem (optional)
- **GPIO**: 40-pin header

## Build Requirements

```bash
# Cross-compile toolchain (RISC-V 64-bit)
sudo apt install gcc-riscv64-linux-gnu

# Or use Arch RISC-V (native build on the board)
pacman -S base-devel meson ninja
```

## Device Tree

The BPI-F3 uses a SpacemiT K1 device tree. ZylOS requires these DT nodes:

```dts
/dts-v1/;
#include "spacemit-k1.dtsi"

/ {
    model = "Banana Pi BPI-F3 (ZylOS)";
    compatible = "bananapi,bpi-f3", "spacemit,k1";

    /* Display (MIPI DSI) */
    panel: panel@0 {
        compatible = "zylos,mipi-panel";
        reg = <0>;
        width-mm = <68>;
        height-mm = <150>;
        /* Resolution set via drm_panel driver */
    };

    /* WiFi (SDIO) */
    wifi: wifi@1 {
        compatible = "realtek,rtl8852bs";
        /* Managed by wpa_supplicant → HAL wifi */
    };

    /* Bluetooth (UART) */
    bluetooth: bluetooth@2 {
        compatible = "realtek,rtl8852bs-bt";
        /* Managed by BlueZ → HAL bluetooth */
    };

    /* Battery (fuel gauge I2C) */
    battery: battery@36 {
        compatible = "maxim,max17048";
        reg = <0x36>;
        /* Read via sysfs power_supply → HAL battery */
    };
};
```

## Recommended eMMC / SD Partition Layout

ZylOS updater and dm-verity expect an A/B-style layout. This repository does not
ship the kernel/rootfs images, but the board bring-up should use a partition map
compatible with `tools/sign-image.sh` and `board/bpi-f3/fit-image.its`.

| Partition | Example device | Size | Purpose |
|---|---|---:|---|
| boot | `/dev/mmcblk0p1` | 256 MiB | FIT image (`fit-image.itb`), U-Boot env |
| system_a | `/dev/mmcblk0p2` | 4–8 GiB | Active rootfs A |
| verity_a | `/dev/mmcblk0p3` | 256–512 MiB | dm-verity hash tree for A |
| system_b | `/dev/mmcblk0p4` | 4–8 GiB | Inactive rootfs B for OTA |
| verity_b | `/dev/mmcblk0p5` | 256–512 MiB | dm-verity hash tree for B |
| data | `/dev/mmcblk0p6` | remainder | `/data`, user apps, logs |

Minimum updater assumptions:
- Kernel cmdline points to a dm-verity mapped block device.
- Bootloader exposes the active slot (`zyl.slot=a|b`).
- The inactive slot can be written atomically before reboot.

## Build Verification

```bash
# Cross-compile ZylOS for RISC-V
meson setup builddir --cross-file board/bpi-f3/cross-riscv64.ini
ninja -C builddir

# Deploy to SD card
sudo dd if=builddir/zylos.img of=/dev/sdX bs=4M
```

## QEMU Bring-up Notes (sanity only)

The BPI-F3 hardware is not fully emulated by upstream QEMU. Still, basic RISC-V
kernel/rootfs validation can be done with the generic `virt` machine before real
board bring-up:

```bash
qemu-system-riscv64 \
  -machine virt -m 4096 -smp 4 \
  -kernel vmlinuz \
  -initrd initramfs.cpio.gz \
  -append 'root=/dev/vda rw console=ttyS0,115200' \
  -drive file=rootfs.img,format=raw,if=virtio \
  -serial mon:stdio
```

Limitations:
- No BPI-F3-specific GPU/DSI/PMIC modelling.
- No RTL8852BS SDIO/UART emulation.
- Use QEMU for early userspace sanity, not board-complete validation.

## Cross-Compilation File

See `cross-riscv64.ini` for meson cross-file configuration.
