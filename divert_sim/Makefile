CPP      := g++

# Set V=1 to see full commands
ifndef V
Q := @
endif

EPOXY_DUINO_DIR := ../../EpoxyDuino
EPOXY_CORE_PATH ?= $(EPOXY_DUINO_DIR)/cores/epoxy
EPOXY_LIB_DIR ?= $(EPOXY_DUINO_DIR)/libraries

#BASE_ENV = openevse_wifi_v1
#ARDUINO_LIB_DIR := ../.pio/libdeps/$(BASE_ENV)
ARDUINO_LIB_DIR := ../..

CPPFLAGS := \
  -I . \
  -I ../include \
  -I ../src \
  -I $(EPOXY_CORE_PATH) \
  -I $(ARDUINO_LIB_DIR)/MicroDebug/src \
  -I $(ARDUINO_LIB_DIR)/MicroTasks/include \
  -I $(ARDUINO_LIB_DIR)/StreamSpy/src \
  -I $(ARDUINO_LIB_DIR)/ConfigJson/src \
  -I $(ARDUINO_LIB_DIR)/ArduinoJson/src \
  -I $(ARDUINO_LIB_DIR)/OpenEVSE_Lib/src \
  -I $(ARDUINO_LIB_DIR)/ESPAL/src \
  -I $(EPOXY_LIB_DIR)/EpoxyFS/src \
  -I $(EPOXY_LIB_DIR)/EpoxyEepromEsp/src \
  -ggdb \
  -D DIVERT_SIM \
  -D ARDUINO=100 \
  -D UNIX_HOST_DUINO \
  -D EPOXY_DUINO \
  -D EPOXY_CORE_ESP8266 \
  -D ENERGY_METER_STORE_STATE=0 \
  -D ARDUINOJSON_ENABLE_PROGMEM=0 \
  -D ENABLE_CONFIG_V1_IMPORT=0 \
  -D ENABLE_CONFIG_CHANGE_NOTIFICATION=0  \
#  -D ENABLE_DEBUG \
#  -D ENABLE_DEBUG_MICRO_TASK \
#  -D ENABLE_DEBUG_DIVERT \
#  -D ENABLE_DEBUG_INPUT_FILTER \
#  -D ENABLE_DEBUG_EVSE_MAN \
#  -D ENABLE_DEBUG_EVSE_MONITOR
LDFLAGS := -pthread

OBJ_DIR := obj
TARGETS:= divert_sim
MAINS  := $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(TARGETS)) )
#OBJ    := kbd.o command.o display.o $(MAINS)
#DEPS   := defs.h command.h

MICRO_TASKS_OBJ := \
  $(OBJ_DIR)/MicroTasks.o \
  $(OBJ_DIR)/MicroTasksTask.o \
  $(OBJ_DIR)/MicroTasksEvent.o \
  $(OBJ_DIR)/MicroTasksEventListener.o \
  $(OBJ_DIR)/MicroTasksAlarm.o \
  $(OBJ_DIR)/MicroTasksNode.o \
  $(OBJ_DIR)/MicroTasksList.o

OPENEVSE_LIB_OBJ := \
  $(OBJ_DIR)/openevse.o \
  $(OBJ_DIR)/RapiSender.o

CONFIG_JSON_OBJ := \
  $(OBJ_DIR)/ConfigJson.o

STREAM_SPY_OBJ := \
  $(OBJ_DIR)/StreamSpy.o \

OPENEVSE_WIFI_OBJ := \
  $(OBJ_DIR)/input_filter.o \
  $(OBJ_DIR)/divert.o \
  $(OBJ_DIR)/current_shaper.o \
  $(OBJ_DIR)/evse_man.o \
  $(OBJ_DIR)/evse_monitor.o \
  $(OBJ_DIR)/energy_meter.o \
  $(OBJ_DIR)/event_log.o \
  $(OBJ_DIR)/debug.o \
  $(OBJ_DIR)/manual.o \
  $(OBJ_DIR)/app_config.o

ARDUINO_OBJ := \
  $(OBJ_DIR)/avr_stdlib.o \
  $(OBJ_DIR)/Arduino.o \
  $(OBJ_DIR)/base64.o \
  $(OBJ_DIR)/Esp.o \
  $(OBJ_DIR)/IPAddress.o \
  $(OBJ_DIR)/Print.o \
  $(OBJ_DIR)/SPI.o \
  $(OBJ_DIR)/StdioSerial.o \
  $(OBJ_DIR)/Stream.o \
  $(OBJ_DIR)/Wire.o \
  $(OBJ_DIR)/WMath.o \
  $(OBJ_DIR)/WString.o \
  $(OBJ_DIR)/Injection.o

EPOXY_FS_OBJ := \
  $(OBJ_DIR)/EpoxyFS.o \
  $(OBJ_DIR)/FS.o \
  $(OBJ_DIR)/FSImpl.o

EPOXY_EEPROM_OBJ := \
  $(OBJ_DIR)/EpoxyEepromEsp.o

ESPAL_OBJ := \
  $(OBJ_DIR)/espal.o

OBJ    := \
  $(MICRO_TASKS_OBJ) \
  $(CONFIG_JSON_OBJ) \
  $(STREAM_SPY_OBJ) \
  $(ESPAL_OBJ) \
  $(OPENEVSE_LIB_OBJ) \
  $(OPENEVSE_WIFI_OBJ) \
  $(EPOXY_FS_OBJ) \
  $(EPOXY_EEPROM_OBJ) \
  $(ARDUINO_OBJ) \
  $(MAINS)
DEPS   :=
VPATH 	:= \
  . \
  ../src \
  $(EPOXY_CORE_PATH) \
  $(EPOXY_CORE_PATH)/epoxy_test/Injection \
  $(EPOXY_CORE_PATH)/epoxy_test/Script \
  $(ARDUINO_LIB_DIR)/OpenEVSE_Lib/src \
  $(ARDUINO_LIB_DIR)/MicroTasks/src \
  $(ARDUINO_LIB_DIR)/ConfigJson/src \
  $(ARDUINO_LIB_DIR)/StreamSpy/src \
  $(ARDUINO_LIB_DIR)/ESPAL/src \
  $(EPOXY_LIB_DIR)/EpoxyFS/src \
  $(EPOXY_LIB_DIR)/EpoxyEepromEsp/src

.PHONY: all clean

all: $(TARGETS)

clean:
	rm -f $(TARGETS)
	rm -rf $(OBJ_DIR)

server:
	python3 server.py

$(OBJ_DIR):
	$(Q)mkdir -p $(OBJ_DIR)

$(OBJ): $(OBJ_DIR)/%.o : %.cpp $(DEPS) | $(OBJ_DIR)
	@echo "CC $<"
	$(Q)$(CPP) -c -o $@ $< $(CPPFLAGS)

$(TARGETS): % : $(filter-out $(MAINS), $(OBJ)) $(OBJ_DIR)/%.o
	@echo "LD $@"
	$(Q)$(CPP) -o $@ $(LIBS) $^ $(CPPFLAGS) $(LDFLAGS)
