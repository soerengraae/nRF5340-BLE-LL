# nRF5340 Bare-Metal BLE Link Layer

A from-scratch BLE Link Layer implementation running on the nRF5340 network core, with no SDK or RTOS — just CMSIS headers and direct register access.

Currently implements **non-connectable advertising** (ADV_NONCONN_IND) on all three advertising channels with configurable interval/window timing, data whitening, and CRC per the Bluetooth Core Spec 5.4.

## What it does

- Application core boots, assigns GPIO and peripherals to the network core, then sleeps
- Network core starts the HF clock, configures the RADIO peripheral for BLE 1 Mbit/s mode, and begins advertising
- Transmits on channels 37, 38, 39 with the standard BLE advertising access address (`0x8E89BED6`)
- Advertises the device name "nRF5340" using the random static address from FICR
- RTC0 controls advertising interval and window; TIMER0 provides RX scan timeout

## Project structure

```
src/
  main_app.c          Application core — peripheral setup and network core release
  net.c               Network core — radio config, advertising state machine, ISRs
include/              Nordic CMSIS device headers (nrf5340)
startup/              Startup files and system init for both cores
linker/               Linker scripts for application and network cores
CMSIS/                ARM CMSIS core headers
makefile              Build system (GNU Make + arm-none-eabi-gcc)
```

## Building

Requires `arm-none-eabi-gcc` on your PATH.

```sh
make CORE=net        # Build network core (default)
make CORE=app        # Build application core
```

## Flashing

Requires [nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools) (`nrfjprog`).

Flash both cores (app core must be flashed first):

```sh
make flash CORE=app
make flash CORE=net
```

## Hardware

Tested on the **nRF5340 DK** (PCA10095). LED1 (P0.28) toggles with advertising state.

## License

None specified — use at your own risk.
