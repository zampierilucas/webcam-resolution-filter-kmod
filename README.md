# Webcam Resolution Filter Kernel Module

A Linux kernel module that filters webcam resolutions by intercepting V4L2 ioctl calls. This prevents applications from automatically selecting unwanted resolution formats that may cause performance issues or other problems.

## Overview

This kernel module provides fine-grained control over which webcam resolutions are visible to applications. It can filter out resolutions that are too small, too large, or target specific video devices, helping to avoid bandwidth issues, performance problems, or application crashes caused by unsupported high-resolution formats.

## Features

- Filters frame sizes returned by `v4l2-ctl --list-formats-ext`
- Configurable minimum and maximum resolution limits (default: no limits)
- Uses kernel probes to intercept V4L2 ioctl calls
- Supports both discrete and continuous/stepwise frame size types

## Building

```bash
make
```

## Installation

```bash
make install
```

## Usage

### Load the module with no filtering (default - all resolutions allowed):
```bash
sudo insmod webcam_res_filter.ko
```

### Load with custom resolution range (hides unwanted resolutions):
```bash
sudo insmod webcam_res_filter.ko min_width=1280 min_height=720 max_width=1920 max_height=1080
```

### Load with only maximum limits:
```bash
sudo insmod webcam_res_filter.ko max_width=1280 max_height=720
```

### Load with only minimum limits:
```bash
sudo insmod webcam_res_filter.ko min_width=800 min_height=600
```

### Load with just one parameter:
```bash
# Filter out very high resolutions
sudo insmod webcam_res_filter.ko max_width=1920

# Filter out very small resolutions  
sudo insmod webcam_res_filter.ko min_width=640
```

### Target a specific device only:
```bash
sudo insmod webcam_res_filter.ko device_path=/dev/video1 max_width=1280 max_height=720
```


### Unload the module:
```bash
sudo rmmod webcam_res_filter
```

### Test the filtering:
```bash
# Before loading the module
v4l2-ctl --list-formats-ext

# Load the module with filtering parameters
sudo insmod webcam_res_filter.ko max_width=1920 max_height=1080

# Test again - resolutions above 1080p should be filtered out
v4l2-ctl --list-formats-ext
```

## Parameters

All parameters are optional. If no parameters are specified, no filtering is applied.

- `min_width`: Minimum allowed width (-1 for no limit, default: no limit)
- `min_height`: Minimum allowed height (-1 for no limit, default: no limit)
- `max_width`: Maximum allowed width (-1 for no limit, default: no limit)
- `max_height`: Maximum allowed height (-1 for no limit, default: no limit)
- `device_path`: Target device path (e.g., `/dev/video1`, default: all devices)

## How It Works

The module uses kernel probes (kprobes) to intercept calls to the V4L2 ioctl handler. When applications query for available frame sizes using `VIDIOC_ENUM_FRAMESIZES`, the module:

1. **Intercepts** the enumeration requests from applications
2. **Maps** the requested index to known allowed resolutions that match the filter criteria  
3. **Returns** only the filtered resolutions, creating a clean enumeration
4. **Provides** seamless filtering without stopping enumeration

This approach ensures applications only see resolutions within the specified limits.

## Troubleshooting

1. **Module fails to load**: Check kernel logs with `dmesg` for error messages
2. **No filtering effect**: Ensure the module is loaded with `lsmod | grep webcam_res_filter`
3. **Build errors**: Ensure kernel headers are installed (`sudo dnf install kernel-devel`)

## Limitations

- Currently supports x86_64 and ARM64 architectures
- Requires kernel probe support (CONFIG_KPROBES)
- May not work with all V4L2 drivers depending on their implementation

## Known Issues

No known issues. All filtering modes work as expected.