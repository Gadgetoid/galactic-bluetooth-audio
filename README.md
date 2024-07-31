# Blunicorn

Blunicorn is a Bluetooth A2DP Speaker Firmware for the Raspberry Pi Pico W-powered Galactic Unicorn and Cosmic Unicorn from Pimoroni.

Play music to your display, and bask in the warm, LED glow of real-time visualisation effects.

## Installing

You should grab the latest release from https://github.com/Gadgetoid/galactic-bluetooth-audio/releases/latest

Make sure to grab the right package for your board.

Connect your board to a computer via USB.

Reset your board by holding the BOOTSEL (the button on the Pico itself) button and pressing RESET.

Extract the firmware (.uf2 file) and drag it over to the `RPI-RP2` drive.

## Usage

Fire up Bluetooth on your phone or PC, you should see a new "Cosmic Unicorn" or "Galactic Unicorn" device. Connect and play music to see pretty, pretty colours!

Use the A and B buttons to switch effects (more coming soon.)

## Building

For Galactic Unicorn:

```bash
mkdir build.cosmic
cd build.cosmic
cmake .. -DPICO_SDK_PATH=../../pico-sdk -DPICO_EXTRAS_PATH=../../pico-extras -DPICO_BOARD=pico_w -DDISPLAY_PATH=display/galactic/galactic_unicorn.cmake -DCMAKE_BUILD_TYPE=Release
```

For Cosmic Unicorn:

```bash
mkdir build.cosmic
cd build.cosmic
cmake .. -DPICO_SDK_PATH=../../pico-sdk -DPICO_EXTRAS_PATH=../../pico-extras -DPICO_BOARD=pico_w -DDISPLAY_PATH=display/cosmic/cosmic_unicorn.cmake -DCMAKE_BUILD_TYPE=Release
```

For Pico Audio, ie. without a display:

```bash
mkdir build.headless
cd build.headless
cmake .. -DPICO_SDK_PATH=../../pico-sdk -DPICO_EXTRAS_PATH=../../pico-extras -DPICO_BOARD=none -DHEADLESS=1 -DCMAKE_BUILD_TYPE=Release
```