LINK    = @echo linking $@ && gcc 
CC      = @echo compiling $@ && gcc 
AR      = @echo generating static library $@ && ar crv
FLAGS   = -g -W -Wall -fPIC
GCCFLAGS = 
DEFINES = 
HEADER  = -I./include
LIBS    = -lpthread
LINKFLAGS =

HEADER += -I./include/arch_v5
HEADER += -I./include/cavalry
HEADER += -I./include/sysfs_monitor
HEADER += -I./include/websocket

#LIBS    += -lrt
#LIBS    += -pthread

OBJECT := main.o src/ws_server.o src/stream.o src/websocket/base64.o src/websocket/intlib.o\
		  src/websocket/sha1.o src/websocket/websocket.o \

BIN_PATH = ./dist/bin

TARGET = main

$(TARGET) : $(OBJECT) 
	$(LINK) $(FLAGS) $(LINKFLAGS) -o $@ $^ $(LIBS)

.c.o:
	$(CC) -c $(HEADER) $(FLAGS) -o $@ $<

install: $(TARGET)
	rm -rf $(BIN_PATH)
	mkdir -p $(BIN_PATH)
	cp $(TARGET) $(BIN_PATH)
web:
	sh ./build_ffmpeg.sh

.PHONY: $(TARGET) web

clean:
	rm -rf $(TARGET) *.o *.so *.a src/*.o