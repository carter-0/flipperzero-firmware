# Flipper Zero Firmware (my BLE fork)

This is the Flipper Zero firmware fork I created to get [AirDox](https://github.com/carter-0/AirDox) working. There are more details in the original repo. This fork will not be maintained.

## Summary of Changes:

- Stripped out most apps
- Stripped out all but two animations (min. to compile)
- Switched BLE stack to 'stm32wb5x_BLE_Stack_full_fw.bin'
- Added rudimentary BLE packet sniffing hooks to `furi_hal_bt.c`, `gap.c`

## Why did you need to fork?

- Sniffing BLE advertisement packets is not possible on the default 'stm32wb5x_BLE_Stack_light' firmware
- Getting the firmware within Flipper's 1024 KB flash means stripping out a lot of apps and resources
- Official firmware (understandably) has no support for full BLE stack features

## Installation (easy):

1. Go to [Releases](https://github.com/carter-0/flipperzero-firmware/releases)
2. Download the latest .tgz
3. Open qFlipper and connect your Flipper
4. Click `Install from file` and select the .tgz you downloaded

## Installation (from source):

1. `git clone https://github.com/carter-0/flipperzero-firmware`
2. `cd flipperzero-firmware`
3. Connect your Flipper Zero to your device
4. `./fbt updater_package COMPACT=1 DEBUG=1 COPRO_STACK_BIN=stm32wb5x_BLE_Stack_full_fw.bin COPRO_STACK_TYPE=ble_full`
5. `./fbt COMPACT=1 DEBUG=1 COPRO_STACK_TYPE=ble_full COPRO_STACK_BIN=stm32wb5x_BLE_Stack_full_fw.bin COPRO_OB_DATA=scripts/ob_custradio.data COPRO_DISCLAIMER=1 flash_usb_full`
