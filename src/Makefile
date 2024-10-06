PROJECT=piupsmonitor
SOURCES=$(PROJECT).c
CFLAGS=-O
INCLUDE=.

all: binary install 

binary: $(SOURCES)
	gcc $(CFLAGS) -I$(INCLUDE) -o $(PROJECT) $(SOURCES) -li2c

install: $(PROJECT)
	sudo cp $(PROJECT) /usr/sbin
