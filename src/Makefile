PROJECT=piupsmonitor
SOURCES=$(PROJECT).c
CFLAGS=-O
INCLUDE=.

all: piupsmonitor install 

piupsmonitor: $(SOURCES)
	gcc $(CFLAGS) -I$(INCLUDE) -o $(PROJECT) $(SOURCES) -li2c

install: $(PROJECT)
	cp $(PROJECT) /usr/sbin
