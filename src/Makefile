PROJECT=piupsmonitor
SOURCES=$(PROJECT).c
CFLAGS=-O
INCLUDE=.

all: piupsmonitor

piupsmonitor: $(SOURCES)
	gcc $(CFLAGS) -I$(INCLUDE) -o $(PROJECT) $(SOURCES) -li2c

copy: $(PROJECT)
	cp $(PROJECT) /usr/sbin

install: $(PROJECT)
	cp $(PROJECT) /usr/sbin
	cp -r ../etc/piupsmonitor /etc
	cp -r ../etc/systemd/system/* /etc/systemd/system
