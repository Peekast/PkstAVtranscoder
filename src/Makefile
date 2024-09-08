# Definir el compilador
CC = gcc
VERSION = \"0.0.1\"

CFLAGS = -fPIC -Wall -g3 -O0 $(shell pkg-config --cflags libavcodec libavdevice libavfilter libavformat libavutil libswresample libswscale jansson)
LDFLAGS = $(shell pkg-config --libs libavcodec libavdevice libavfilter libavformat libavutil libswresample libswscale jansson)

SOURCES = pktav_keyvalue.c pktav_mediainfo.c pktav_netutils.c pktav_proto.c pktav_strings.c pktav_sigchld.c pktav_log.c pktav_error.c pktav_video.c pktav_types.c test_mediainfo.c 

OBJECTS = $(SOURCES:.c=.o)

TARGET = main

# Regla principal
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -DVERSION=$(VERSION) -DCOMPILER="\"$(shell gcc --version | head -n 1)\"" -DDATE=\"$(shell date +%Y-%m-%d)\"  -DTIME=\"$(shell date +%H:%M:%S)\" -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
