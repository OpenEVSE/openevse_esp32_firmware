CPP      := g++
CPPFLAGS := -I FakeDuino -I ../src -ggdb 
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
