# piupsmonitor
Programm to monitor the PiUPS+ (PiUSV+) module for the raspberry pi.

This programm is a replacement for the original *piupsmon* which lacks the
availability of source code.

I tried to use the original init.d script, but that does not work, due to the fact, that the
Raspian Os now uses systemd. The PiUSV+ won't power off because the variable RUNLEVEL ist not set during shutdown
causing the poweroff timer not to be set.

So I created two systemd-services, one to start the daemon in multi-user.target and another to programm the poweroff
timer on shutdown. By replacing the `/usr/sbin/piupsmonitor` with `/usr/bin/piupsmon` the service might even work with the original *piupsmon*. 

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

    mkdir -p usr/sbin
    gcc -O -o usr/sbin/piupsmonitor src/piupsmonitor.c -li2c
    
### Install config file ###

    sudo cp -a etc/piupsmonitor /etc
    
The configuration can be changed in `/etc/piupsmonitor/piupsmonitor.conf`

### Install binary  ###

Copy the binary

    sudo cp -a usr/sbin/piupsmonitor /usr/sbin

Copy the systemd service files if you want to start `piupsmonitor` during boot

    sudo cp -a etc/systemd/system/piupsmonitor*.service /etc/systemd/system/

and enable the services

    sudo systemctl daemon-reload
    sudo systemctl enable piupsmonitor.service
    sudo systemctl start piupsmonitor.service
    sudo systemctl enable piupsmonitor-poweroff.service
    
Don't start the `piupsmonitor-poweroff.service` or your Raspberry will poweroff whithout shutting down. The service
will be started during shutdown to switch off the power after a delay (default 15 seconds).
   
# Problems #

### i2c speed problems ###
On my Raspberry Pis I get `Remote I/O error (121)` if I use the default i2c
baudrate. So I reduced it to 40000 and got no errors anymor.

Add the following to `/boot/config.txt`

    dtparam=i2c_baudrate=40000
    
and reboot.

This problem is probably caused by the Raspberry Pi I2C clock-stretching
bug. For more details
s. e.g. [here](http://www.advamation.com/knowhow/raspberrypi/rpi-i2c-bug.html)
or ask Google.

By the way the problem exists with the original `piupsmon`, too. There I get
occasionaly power failures in the logfile. With lowering the i2c baudrate they
are gone.
