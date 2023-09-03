# Controller report rate tester for Linux

## Introduction

This is a simple tool to test the report rate of a controller on Linux.  
This is not portable, Please use XInputTest for Windows.

## Usage

```bash
./controller_report_rate_tester <joystick event device path> [max samples]
```

Find the joystick event device path with `ls /dev/input/by-id/`.  

Spin the left stick around in a circle to test the report rate.

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## License

MIT License. See LICENSE file.