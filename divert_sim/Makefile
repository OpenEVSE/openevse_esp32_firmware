CPP      := g++


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
  -D ENABLE_CONFIG_CHANGE_NOTIFICATION=0
#   \
#  -D ENABLE_DEBUG \
#  -D SERIAL_OUTFD=STDERR_FILENO
#  -D ENABLE_DEBUG_DIVERT \
#  -D ENABLE_DEBUG_INPUT_FILTER \
#  -D ENABLE_DEBUG_EVSE_MAN \
#  -D ENABLE_DEBUG_EVSE_MONITOR
LDFLAGS := -pthread

TARGETS:= divert_sim
MAINS  := $(addsuffix .o, $(TARGETS) )
#OBJ    := kbd.o command.o display.o $(MAINS)
#DEPS   := defs.h command.h

MICRO_TASKS_OBJ := \
  MicroTasks.o \
  MicroTasksTask.o \
  MicroTasksEvent.o \
  MicroTasksEventListener.o \
  MicroTasksAlarm.o \
  MicroTasksNode.o \
  MicroTasksList.o

OPENEVSE_LIB_OBJ := \
  openevse.o \
  RapiSender.o

CONFIG_JSON_OBJ := \
  ConfigJson.o

STREAM_SPY_OBJ := \
  StreamSpy.o \

OPENEVSE_WIFI_OBJ := \
  input_filter.o \
  divert.o \
  current_shaper.o \
  evse_man.o \
  evse_monitor.o \
  energy_meter.o \
  event_log.o \
  debug.o \
  manual.o \
  app_config.o

ARDUINO_OBJ := \
  avr_stdlib.o \
  Arduino.o \
  base64.o \
  Esp.o \
  IPAddress.o \
  Print.o \
  SPI.o \
  StdioSerial.o \
  Stream.o \
  Wire.o \
  WMath.o \
  WString.o \
  Injection.o

EPOXY_FS_OBJ := \
  EpoxyFS.o \
  FS.o \
  FSImpl.o

EPOXY_EEPROM_OBJ := \
  EpoxyEepromEsp.o

ESPAL_OBJ := \
  espal.o

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
	rm -f $(TARGETS) $(OBJ)

server:
	python3 server.py

$(OBJ): %.o : %.cpp $(DEPS)
	$(CPP) -c -o $@ $< $(CPPFLAGS)

$(TARGETS): % : $(filter-out $(MAINS), $(OBJ)) %.o
	$(CPP) -o $@ $(LIBS) $^ $(CPPFLAGS) $(LDFLAGS)
