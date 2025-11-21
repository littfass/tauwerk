#!/bin/bash
echo "BUILD DRIVERS:"
mkdir -p ./bin

g++ -std=c++17 -O3 -o ./bin/tauwerk_gpio_driver ./src/tauwerk_gpio_driver.cpp -lrt -pthread
chmod +x ./bin/tauwerk_gpio_driver
echo "- bin/tauwerk_gpio_driver"

g++ -o ./bin/tauwerk_touchpad_driver ./src/tauwerk_touchpad_driver.cpp \
    -I/usr/include/drm -I/usr/include \
    -I/usr/include/freetype2 -I/usr/include/libpng16 \
    -lEGL -lGLESv2 -lgbm -ldrm -lrt -pthread \
    -lfreetype -lpng \
    -O2 -std=c++17
chmod +x ./bin/tauwerk_touchpad_driver
echo "- bin/tauwerk_touchpad_driver"

g++ -o ./bin/test ./src/test.cpp \
    -I/usr/include/drm -I/usr/include \
    $(pkg-config --cflags freetype2) \
    -I/usr/include/freetype2 -I/usr/include/libpng16 \
    -lEGL -lGLESv2 -lgbm -ldrm -lfreetype -lrt \
    $(pkg-config --libs freetype2) \
    -O3 -std=c++17
chmod +x ./bin/test
echo "- bin/test"
