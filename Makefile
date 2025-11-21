CXX = g++
CXXFLAGS = -std=c++17 -O3 -I/usr/include/drm $(shell pkg-config --cflags freetype2)
LDFLAGS = -lEGL -lGLESv2 -lgbm -ldrm $(shell pkg-config --libs freetype2) -lrt

SOURCES = src/main.cpp \
          src/core/Renderer.cpp \
          src/widgets/Label.cpp \
          src/widgets/Fader.cpp \
          src/widgets/Button.cpp \
          src/widgets/Layout.cpp \
          src/input/TouchManager.cpp

TARGET = bin/test

all: $(TARGET)

$(TARGET): $(SOURCES)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: all
	sudo ./$(TARGET)

.PHONY: all clean run
