CPP      := g++

BASE_ENV = openevse_wifi_v1

EPOXY_DUINO_DIR := ../../EpoxyDuino
EPOXY_CORE_PATH ?= $(EPOXY_DUINO_DIR)/cores/epoxy
EPOXY_LIB_DIR ?= $(EPOXY_DUINO_DIR)/libraries

ARDUINO_LIB_DIR := ../.pio/libdeps/$(BASE_ENV)

CPPFLAGS := \
  -I . \
  -I ../include \
  -I ../src \
  -I $(EPOXY_CORE_PATH) \
  -I $(EPOXY_LIB_DIR)/MicroTasks/src \
  -I "$(ARDUINO_LIB_DIR)/Micro Debug/src" \
  -I $(ARDUINO_LIB_DIR)/MicroTasks/include \
  -I $(ARDUINO_LIB_DIR)/StreamSpy/src \
  -I $(ARDUINO_LIB_DIR)/ArduinoJson/src \
  -I $(ARDUINO_LIB_DIR)/OpenEVSE/src \
  -I $(EPOXY_LIB_DIR)/EpoxyFS/src \
  -ggdb \
  -D DIVERT_SIM \
  -D ARDUINO=100 \
  -D UNIX_HOST_DUINO \
  -D EPOXY_DUINO \
  -D EPOXY_CORE_ESP8266 \
  -D ENERGY_METER_STORE_STATE=0 \
  -D ARDUINOJSON_ENABLE_PROGMEM=0
#  \
#  -D ENABLE_DEBUG \
#  -D ENABLE_DEBUG_DIVERT \
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

STREAM_SPY_OBJ := \
  StreamSpy.o \

OPENEVSE_WIFI_OBJ := \
  divert.o \
  current_shaper.o \
  evse_man.o \
  evse_monitor.o \
  energy_meter.o \
  event_log.o \
  debug.o \
  manual.o

ARDUINO_OBJ := \
  avr_stdlib.o \
  Arduino.o \
  Esp.o \
  IPAddress.o \
  main.o \
  Print.o \
  SPI.o \
  StdioSerial.o \
  Stream.o \
  Wire.o \
  WMath.o \
  WString.o

EPOXY_FS_OBJ := \
  EpoxyFS.o \
  FS.o \
  FSImpl.o

OBJ    := \
  $(MICRO_TASKS_OBJ) \
  $(STREAM_SPY_OBJ) \
  $(OPENEVSE_LIB_OBJ) \
  $(OPENEVSE_WIFI_OBJ) \
  $(EPOXY_FS_OBJ) \
  $(ARDUINO_OBJ) \
  $(MAINS)
DEPS   :=
VPATH 	:= \
  . \
  ../src \
  $(EPOXY_CORE_PATH) \
  $(ARDUINO_LIB_DIR)/OpenEVSE/src \
  $(ARDUINO_LIB_DIR)/MicroTasks/src \
  $(ARDUINO_LIB_DIR)/StreamSpy/src \
  $(EPOXY_LIB_DIR)/EpoxyFS/src

.PHONY: all clean

all: $(TARGETS)

clean:
	rm -f $(TARGETS) $(OBJ)

$(OBJ): %.o : %.cpp $(DEPS)
	$(CPP) -c -o $@ $< $(CPPFLAGS)

$(TARGETS): % : $(filter-out $(MAINS), $(OBJ)) %.o
	$(CPP) -o $@ $(LIBS) $^ $(CPPFLAGS) $(LDFLAGS)
