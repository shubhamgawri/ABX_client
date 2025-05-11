CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra
LDFLAGS = 

NLOHMANN_JSON_PATH = $(shell pkg-config --exists nlohmann_json && echo "system" || echo "local")

ifeq ($(NLOHMANN_JSON_PATH),system)
    CXXFLAGS += $(shell pkg-config --cflags nlohmann_json)
    LDFLAGS += $(shell pkg-config --libs nlohmann_json)
else
    CXXFLAGS += -I./include
endif

TARGET = abx_client

.PHONY: all clean prepare

all: prepare $(TARGET)

prepare:
	@if [ "$(NLOHMANN_JSON_PATH)" = "local" ]; then \
		echo "nlohmann/json not found system-wide, downloading locally..."; \
		mkdir -p include/nlohmann; \
		wget -q -O include/nlohmann/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp || \
		curl -s -o include/nlohmann/json.hpp https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp; \
	fi

$(TARGET): abx_client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
	rm -rf include