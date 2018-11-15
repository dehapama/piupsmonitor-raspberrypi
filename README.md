# piupsmonitor
Programm to monitor the PiUPS+ (PiUSV+) module for the raspberry pi.

This programm is a replacement for the original *piupsmon* which lacks the
availability of source code.

**So far the function for shutting down is not tested in reality (as of 2018-11-15). So
use it on your own risk.**

# Reqirements #

## Hardware ##
* Raspberry Pi
* PiUSV+ 

## Software ##

* `libi2c-dev`     \# Bibliothek fuer C

## Configure ##

Enable I2C

    sudo raspi-config

Go to "5 Interfacing Options" -> "I2C" -> select Yes
and reboot.

# Install #

### Compile `piusvmonitor.c` ###

    cd src
    gcc -o ../usr/bin/piupsmonitor piupsmonitor.c
    cd ..
    
### Install config file ###

    sudo cp -a etc/piupsmonitor /etc
    
The configuration can be changed in `/etc/piupsmonitor/piupsmonitor.conf`

### Install binary  ###

Copy the binary

    sudo cp -a usr/bin/piupsmonitor

Copy the init-file if you want to start `piupsmonitor` during boot

    sudo cp -a etc/init.d/piupsmonitor /etc/init.d
    
    
    
