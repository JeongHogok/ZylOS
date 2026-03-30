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

```
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

## Build Verification

```bash
# Cross-compile ZylOS for RISC-V
meson setup builddir --cross-file board/bpi-f3/cross-riscv64.ini
ninja -C builddir

# Deploy to SD card
sudo dd if=builddir/zylos.img of=/dev/sdX bs=4M
```

## Cross-Compilation File

See `cross-riscv64.ini` for meson cross-file configuration.
