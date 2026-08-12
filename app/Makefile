APP := youyeetoo_app
BUILD_DIR := build

CAMERA_SDK_DIR ?= $(abspath ../CameraSDK)
TOOLCHAIN_BIN ?= $(abspath ../LubanCat_Linux_Generic_SDK_20260424/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin)
LUBANCAT_UBUNTU22_DIR ?= $(abspath ../LubanCat_Linux_Generic_SDK_20260424/ubuntu22.04)
ifeq ($(origin CXX),default)
ifneq (,$(wildcard $(TOOLCHAIN_BIN)/aarch64-none-linux-gnu-g++))
CXX := $(TOOLCHAIN_BIN)/aarch64-none-linux-gnu-g++
else ifneq (,$(shell command -v aarch64-linux-gnu-g++ 2>/dev/null))
CXX := $(shell command -v aarch64-linux-gnu-g++)
else
CXX := aarch64-none-linux-gnu-g++
endif
endif

SRCS := main.cpp \
	src/camera_app.cpp \
	src/boot_state_service.cpp \
	src/can_service.cpp \
	src/camera_manager.cpp \
	src/camera_controller.cpp \
	src/camera_port_resolver.cpp \
	src/camera_sdk_adapter.cpp \
	src/command_dispatcher.cpp \
	src/downlink_service.cpp \
	src/log_service.cpp \
	src/main_service.cpp \
	src/narrowband_service.cpp \
	src/ota_receive_service.cpp \
	src/storage_service.cpp \
	src/task_manager.cpp \
	src/telemetry_command_interface.cpp \
	src/telemetry_manager.cpp \
	src/user_data_store.cpp \
	src/usb_service.cpp \
	src/watchdog_service.cpp \
	src/worker_channel.cpp \
	src/worker_supervisor.cpp \
	src/camera_worker.cpp \
	src/can_protocol.cpp \
	src/can_worker.cpp \
	src/telemetry_worker.cpp

OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

CPPFLAGS += -Iinclude -Isrc -I$(CAMERA_SDK_DIR)/include
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS += -L$(CAMERA_SDK_DIR)/lib -Wl,-rpath,'$$ORIGIN/../lib'
LDLIBS += -lCameraSDK -latomic -pthread -ldl

.PHONY: all clean info

all: $(BUILD_DIR)/$(APP)

info:
	@echo "APP=$(APP)"
	@echo "CXX=$(CXX)"
	@echo "CAMERA_SDK_DIR=$(CAMERA_SDK_DIR)"
	@echo "LUBANCAT_UBUNTU22_DIR=$(LUBANCAT_UBUNTU22_DIR)"

$(BUILD_DIR)/$(APP): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
