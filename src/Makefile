# Definir el compilador
CC = gcc
VERSION = \"1.0.0\"

# Flags de compilación y vinculación
CFLAGS = -fPIC -Wall -g3 -O0 $(shell pkg-config --cflags libavcodec libavdevice libavfilter libavformat libavutil libswresample libswscale jansson)
LDFLAGS = $(shell pkg-config --libs libavcodec libavdevice libavfilter libavformat libavutil libswresample libswscale jansson)

# Define los archivos fuente (incluye main.c si es necesario)
SOURCES = pktav_keyvalue.c pktav_mediainfo.c pktav_netutils.c pktav_proto.c pktav_strings.c pktav_sigchld.c pktav_log.c pktav_error.c pktav_video.c pktav_types.c test_mediainfo.c 

# Define los archivos objeto correspondientes a las fuentes
OBJECTS = $(SOURCES:.c=.o)

# Nombre del ejecutable
TARGET = main

# Regla principal
all: $(TARGET)

# Compilar el ejecutable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Compilar cada archivo fuente .c en su objeto .o
%.o: %.c
	$(CC) $(CFLAGS) -DVERSION=$(VERSION) -DCOMPILER="\"$(shell gcc --version | head -n 1)\"" -DDATE=\"$(shell date +%Y-%m-%d)\"  -DTIME=\"$(shell date +%H:%M:%S)\" -c $< -o $@

# Limpiar archivos objeto y el ejecutable
clean:
	rm -f $(OBJECTS) $(TARGET)
