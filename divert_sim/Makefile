CPP      := g++

BASE_ENV = openevse_huzzah32_idf

CPPFLAGS := -I. -I FakeDuino -I ../src -I../.pio/libdeps/$(BASE_ENV)/ArduinoJson/src -I../.pio/libdeps/$(BASE_ENV)/OpenEVSE/src -ggdb -DDIVERT_SIM -DRAPI_PORT=Console
#-DENABLE_DEBUG -DDEBUG_PORT=Console
LDFLAGS := -pthread

TARGETS:= divert_sim
MAINS  := $(addsuffix .o, $(TARGETS) )
#OBJ    := kbd.o command.o display.o $(MAINS)
#DEPS   := defs.h command.h
OBJ    := divert.o WString.o RapiSender.o Console.o Print.o Core.o $(MAINS)
DEPS   :=
VPATH 	:= .:../src:FakeDuino

.PHONY: all clean

all: $(TARGETS)

clean:
	rm -f $(TARGETS) $(OBJ)

$(OBJ): %.o : %.cpp $(DEPS)
	$(CPP) -c -o $@ $< $(CPPFLAGS)

$(TARGETS): % : $(filter-out $(MAINS), $(OBJ)) %.o
	$(CPP) -o $@ $(LIBS) $^ $(CPPFLAGS) $(LDFLAGS)
