#!/bin/bash
# Build script for Linux/macOS

set -e  # Exit on error

echo "=== Wireless Audio Accessory Simulator Build Script ==="
echo ""

# Create build directory
if [ ! -d "build" ]; then
    echo "Creating build directory..."
    mkdir build
fi

cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo "Building..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "=== Build Complete ==="
echo ""
echo "Executables:"
echo "  ./build/accessory_simulator"
echo "  ./build/host_daemon"
echo ""
echo "To run:"
echo "  Terminal 1: ./build/accessory_simulator"
echo "  Terminal 2: ./build/host_daemon"
echo ""
