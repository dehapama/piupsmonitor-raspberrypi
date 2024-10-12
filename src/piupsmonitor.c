#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <wctype.h>
#include <errno.h>

//#define DEBUG
//#define BUTTON_TEST
//#define SHUTDOWN_TEST

/* PiUPS+ Defines */
#define TWI_CMD_GETSTATUS 0x00
#define TWI_CMD_GETVERSION 0x01
#define TWI_CMD_GETVOLTAGE 0x02
#define TWI_CMD_SHUTDOWN 0x10
 
#define STAT_PRI_POW  0  // Primary Power Supply Bit 0
#define STAT_SEC_POW 1 // Secondary Power Supply Bit 1
#define STAT_BAT_LOW 2 // Battery Low Bit 2
#define STAT_BAT_CHARGE 3 // Akku wird geladen
#define STAT_BAT_FULL 4 // Akku ist voll
#define STAT_SW1_AKTIV 5 // Taster S1 betaetigt

#define LOGLEVEL_ERROR 0
#define LOGLEVEL_NOTICE 1
#define LOGLEVEL_DEBUG 2

int device;

struct timespec sleeptime={0,10000000};
struct timespec longsleeptime={0,500000000};

const char* loglevel_label[] = {"ERROR", "NOTICE", "DEBUG" };

const char* config_tokens[] = {
  "ShutdownTimer",
  "PowerOffTimer",
  "ShutdownCmd",
  "LogLevel",
  "LogStatusDesc",
  "StatusChangedCmd",
  "ButtonPressedCmd",
  "LogFile",
  0
};

enum ConfigEnum {
  CONF_SHUTDOWNTIMER,
  CONF_POWEROFFTIMER,
  CONF_SHUTDOWNCMD,
  CONF_LOGLEVEL,
  CONF_LOGSTATUSDESC,
  CONF_STATUSCHANGEDCMD,
  CONF_BUTTONPRESSEDCMD,
  CONF_LOGFILE
};
  
struct {
  int piups_address;
  int log_level;
  int log_status_desc;
  int shutdown_timer;
  int power_off_timer;
  char *shutdown_cmd;
  char *status_changed_cmd;
  char *button_pressed_cmd;
  char *logfile;
  char *pidfile;
  int button_pressed_delay;
} settings = {
  0x18,                    // PiUPS I2C address
  LOGLEVEL_DEBUG,          // log level
  1,                       // log status desc
  5,                       // shutdown timer
  15,                      // power off timer
  "init 0",                // shutdown command
  "",                      // status changed cmd
  "init 0",                // button pressed cmd
  "/var/log/piupsmonitor.log", // logfile
  "/var/run/piupsmonitor.pid", // pidfile
  5,                       // button pressed delay (time to wait for the next button press)
};

int16_t swap(int16_t value) {
  value=((value & 0xff) << 8) + ((value >> 8 ) & 0xff);
  return value;
}

char version[13];

union {
  struct {
    unsigned char primary_power_supply : 1;
    unsigned char secondary_power_supply : 1;
    unsigned char battery_low : 1;
    unsigned char battery_charge : 1;
    unsigned char battery_full : 1;
    unsigned char button_pressed : 1;
  } bit;
    unsigned char total;
} status, status_old;
  
struct {
  uint16_t u_bat;
  int16_t i_rasp;
  int16_t u_usb;
  int16_t u_usv;
  int16_t u_ext;
} voltage;

enum {
  STATE_NORMAL,
  STATE_SHUTDOWN,
} state;

pid_t shutdown_cmd_pid=0;
pid_t button_pressed_cmd_pid=0;
pid_t status_changed_cmd_pid=0;

int logprintf(int log_level, const char *fmt, ...) {
  if (log_level<=settings.log_level) {
    time_t now = time(NULL);
    struct tm *lcltime=localtime(&now);
    char timestamp[50];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",lcltime);
    fprintf(stderr,"%s [%s] ",timestamp,loglevel_label[log_level]);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr,fmt, ap);
    fprintf(stderr,"\n");
  }
  return 0;
}

#ifdef DEBUG
void debug_var(void* value, int size) {
  unsigned char* v=(unsigned char* )value;
  int i;
  for (i=0; i<size; i++)
    printf("%02x ",v[i]);
}
#endif

int find_string_in_array(const char* needle, const char** haystack) {
  int index;
  for (index=0; haystack[index]; index++) {
    if (strcasecmp(needle, haystack[index])==0)
      return index;
  }
  return -1;
}

char *trim_string(char *buffer) {
  char *p;
  for (p=&buffer[strlen(buffer)-1];p>=buffer && iswspace(*p); p--) *p=0; 
  return buffer;
}

void print_settings()
{
  logprintf(LOGLEVEL_NOTICE,"ShutdownTimer: %d",settings.shutdown_timer);
  logprintf(LOGLEVEL_NOTICE,"PowerOffTimer: %d",settings.power_off_timer);
  logprintf(LOGLEVEL_NOTICE,"ShutdownCmd: %s",settings.shutdown_cmd);
  logprintf(LOGLEVEL_NOTICE,"StatusChangedCmd: %s",settings.status_changed_cmd);
  logprintf(LOGLEVEL_NOTICE,"LogLevel: %s",loglevel_label[settings.log_level]);
  logprintf(LOGLEVEL_NOTICE,"LogDescription: %d",settings.log_status_desc);
  logprintf(LOGLEVEL_NOTICE,"ButtonPressedCmd: %s",settings.button_pressed_cmd);
  logprintf(LOGLEVEL_NOTICE,"LogFile: %s",settings.logfile);
  logprintf(LOGLEVEL_NOTICE,"PID-File: %s",settings.pidfile);
}

int read_settings() {
  FILE *fp = fopen("/etc/piupsmonitor/piupsmonitor.conf","r");
  char buffer[1024];
  while (!feof(fp)) {
    if (!fgets(buffer,sizeof(buffer),fp)) 
      break;
    trim_string(buffer);
    char *comment=strchr(buffer,'#');
    if (comment) *comment=0; // Remove comments from line
    if (buffer[0]==0)
      continue;
    char *value=buffer;
    char *token=strsep(&value,"=");
#ifdef DEBUG
    if (value && token) printf("Found %s = %s\n",token,value);
#endif
    switch (find_string_in_array(token, config_tokens)) {
    case CONF_SHUTDOWNTIMER:
      sscanf(value,"%d",&settings.shutdown_timer);
      break;
    case CONF_POWEROFFTIMER:
      sscanf(value,"%d",&settings.power_off_timer);
      break;
    case CONF_SHUTDOWNCMD:
      settings.shutdown_cmd=strdup(value);
      break;
    case CONF_LOGLEVEL:
      settings.log_level=find_string_in_array(value,loglevel_label);
      if (settings.log_level==-1) settings.log_level=LOGLEVEL_NOTICE;
      break;
    case CONF_LOGSTATUSDESC:
      sscanf(value,"%d",&settings.log_status_desc);
      break;
    case CONF_STATUSCHANGEDCMD:
      settings.status_changed_cmd=strdup(value);
      break;
    case CONF_BUTTONPRESSEDCMD:
      settings.button_pressed_cmd=strdup(value);
      break;
    case CONF_LOGFILE:
      settings.logfile=strdup(value);
      break;
    default:
      logprintf(LOGLEVEL_ERROR,"Unknown token %s in config file",buffer);
      break;
      }
      
    }
  fclose(fp);
}

int read_version() {
  int result=i2c_smbus_read_i2c_block_data(device,TWI_CMD_GETVERSION,sizeof(version)-1,(__u8*)&version);
  if (result==-1) {
    int errnum=errno;
    logprintf(LOGLEVEL_ERROR,"Can't read version: %s (%d)",strerror(errnum),errnum);
  }
  version[sizeof(version)-1]=0;
  logprintf(LOGLEVEL_DEBUG,"Version: %s",version);
  
}

void print_voltage() {
  logprintf(LOGLEVEL_DEBUG,"U_Bat: %d mV",voltage.u_bat);
  logprintf(LOGLEVEL_DEBUG,"I_Rasp: %d mA",voltage.i_rasp);
  logprintf(LOGLEVEL_DEBUG,"U_USB: %d mV",voltage.u_usb);
  logprintf(LOGLEVEL_DEBUG,"U_USV: %d mV",voltage.u_usv);
  logprintf(LOGLEVEL_DEBUG,"U_Ext: %d mV",voltage.u_ext);
}

int read_voltage() {
  int result=i2c_smbus_read_i2c_block_data(device,TWI_CMD_GETVOLTAGE,sizeof(voltage),(__u8*)&voltage);
  if (result==-1) {
    int errnum=errno;
    logprintf(LOGLEVEL_ERROR,"Can't read voltage: %s (%d)",strerror(errnum),errnum);
    nanosleep(&longsleeptime,0);
    return result;
  }
#ifdef DEBUG
  printf("read %d bytes\n",result);
  debug_var(&voltage,sizeof(voltage));
  printf("%04x %04x\n",255,swap(255));
#endif
  voltage.u_bat=swap(voltage.u_bat);
  voltage.i_rasp=swap(voltage.i_rasp);
  voltage.u_usb=swap(voltage.u_usb);
  voltage.u_usv=swap(voltage.u_usv);
  voltage.u_ext=swap(voltage.u_ext);
  nanosleep(&sleeptime,0);
  return 0;
}

int run_command(const char* cmd) {
  int cpid=fork();
  if (cpid==-1) return cpid;
  if (cpid==0) {
    exit(execl("/bin/sh", "sh", "-c", cmd, (char *) 0));
  }
  return cpid;
}

void print_change_status()
{
  if (status.total==status_old.total) 
    return;
  if (settings.log_status_desc) {
    if (status_old.bit.primary_power_supply!=status.bit.primary_power_supply)
      logprintf(LOGLEVEL_NOTICE,"Change in Primary Power Supply: %d",status.bit.primary_power_supply);
    if (status_old.bit.secondary_power_supply!=status.bit.secondary_power_supply)
      logprintf(LOGLEVEL_NOTICE,"Change in Secondary Power Supply: %d",status.bit.secondary_power_supply);
    if (status_old.bit.battery_low!=status.bit.battery_low)
      logprintf(LOGLEVEL_NOTICE,"Change in Battery Low: %d",status.bit.battery_low);
    if (status_old.bit.battery_charge!=status.bit.battery_charge)
      logprintf(LOGLEVEL_NOTICE,"Change in Battery Charge: %d",status.bit.battery_charge);
    if (status_old.bit.battery_full!=status.bit.battery_full)
      logprintf(LOGLEVEL_NOTICE,"Change in Battery Full: %d",status.bit.battery_full);
    if (status_old.bit.button_pressed!=status.bit.button_pressed)
      logprintf(LOGLEVEL_NOTICE,"Change in Button Pressed: %d",status.bit.button_pressed);
  } else {
    logprintf(LOGLEVEL_NOTICE,"Status changed from %02x to %02x",status_old.total,status.total);
  }
  if (settings.status_changed_cmd[0]) {
    status_changed_cmd_pid=run_command(settings.status_changed_cmd);
    if (status_changed_cmd_pid==-1) {
      logprintf(LOGLEVEL_ERROR,"Starting StatusChendeCmd %s failed",settings.status_changed_cmd);
      status_changed_cmd_pid=0;
    } else {
      logprintf(LOGLEVEL_DEBUG,"Successfully started StatusChangedCmd %s",settings.status_changed_cmd);
    }
  }
}

void print_status() {
  logprintf(LOGLEVEL_DEBUG,"Primary Power Supply: %d",status.bit.primary_power_supply);
  logprintf(LOGLEVEL_DEBUG,"Secondary Power Supply: %d",status.bit.secondary_power_supply);
  logprintf(LOGLEVEL_DEBUG,"Battery Low: %d",status.bit.battery_low);
  logprintf(LOGLEVEL_DEBUG,"Battery Charge: %d",status.bit.battery_charge);
  logprintf(LOGLEVEL_DEBUG,"Battery Full: %d",status.bit.battery_full);
  logprintf(LOGLEVEL_DEBUG,"Button Pressed: %d",status.bit.button_pressed);
}

int read_status() {
  int result=i2c_smbus_read_byte_data(device,TWI_CMD_GETSTATUS);
  if (result==-1) {
    int errnum=errno;
    logprintf(LOGLEVEL_ERROR,"Can't read status: %s (%d)",strerror(errnum),errnum);
    nanosleep(&longsleeptime,0);
    return result;
  }
  if (result==0xff) {
    logprintf(LOGLEVEL_ERROR,"Implausible status %04x read",result);
    nanosleep(&longsleeptime,0);
    return -1;
  }
    
  status.total=result & 0xff;
#if 0  
  char* p=(char*)&status;
  *p=result & 0xff;
#endif
#ifdef DEBUG
  //  printf("Status: %02x(%04x)\n",status.total,result);
#endif
  nanosleep(&sleeptime,0);
  return result;
}

int set_power_off_timer() {
  logprintf(LOGLEVEL_NOTICE,"Set PowerOffTimer to %d",settings.power_off_timer);
  i2c_smbus_write_byte_data(device,TWI_CMD_SHUTDOWN,settings.power_off_timer);
}


int write_pid_file(const char* prgname) {
  if (getuid()!=0) {
    logprintf(LOGLEVEL_ERROR,"Running without root privileges");
    return -1;
  }
  FILE *fp;
  char filename[100];
  const char* p=strrchr(prgname,'/');
  if (p==NULL) p=prgname; else p++;
  snprintf(filename,sizeof(filename),"/var/run/%s.pid",p);
  settings.pidfile=strdup(filename);
  fp=fopen(filename,"r");
  if (fp) {
    fclose(fp);
    logprintf(LOGLEVEL_ERROR,"%s already running (%s)",prgname,filename);
    exit(EXIT_FAILURE);
  }
  fp=fopen(filename,"w");
  if (!fp) {
    logprintf(LOGLEVEL_ERROR,"Cannot open %s for writing",filename);
    exit(EXIT_FAILURE);
  }
  fprintf(fp,"%d",getpid());
  fclose(fp);
  return 0;
}

int open_logfile()
{
  logprintf(LOGLEVEL_NOTICE,"Redirection log messages to %s",settings.logfile);
  
  FILE *fp=fopen(settings.logfile,"a");
  if (!fp) {
    logprintf(LOGLEVEL_ERROR,"Cannot open logfile %s");
    return -1;
  }
  dup2(fileno(fp),fileno(stderr));
  return 0;
}

volatile sig_atomic_t done = 0;
void terminate(int signum)
{
   done = signum;
}

void cleanup() {
  if (getuid()==0) {
    remove(settings.pidfile);
  }
}

int main(int argc, const char* argv[]) {
  
  if (!(argc==2 && strcmp(argv[1],"poweroff")==0)) {
    // Don't try to write pid file if the raspberry should poweroff
    write_pid_file(argv[0]);
  }
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = terminate;
  if (sigaction(SIGTERM, &action, NULL)==-1) {
    perror("Can't set signal handler for SIGTERM");
    exit(EXIT_FAILURE);
  }
  
  if (sigaction(SIGINT, &action, NULL)==-1) {
    perror("Can't set signal handler for SIGINT");
    exit(EXIT_FAILURE);
  }

  /* Geraetedatei oeffnen */
  printf("Opening device...");
  if ((device = open("/dev/i2c-1", O_RDWR)) < 0) {
    cleanup();
    perror("open() failed");
    exit(EXIT_FAILURE);
  }
  printf(" OK\n");

  /* Spezifizieren der Slave-Adresse -> Kommunikation frei geben */
  if (ioctl(device, I2C_SLAVE, settings.piups_address) < 0) {
    cleanup();
    perror("Failed to acquire bus access and/or talk to slave\n");
    exit(EXIT_FAILURE);
  }

  state=STATE_NORMAL;

  read_settings();
  open_logfile();
  print_settings();
  
  logprintf(LOGLEVEL_NOTICE,"Version %d",read_version());
  while (read_voltage()==-1);
  print_voltage();
  while (read_status()==-1);
  print_status();
  status_old=status;

  if (argc==2 && strcmp(argv[1],"poweroff")==0) {
    set_power_off_timer();
    done=-1;
  }
  
  time_t voltage_time=time(NULL);
  time_t shutdown_time=time(NULL);
  time_t seconds_time=time(NULL);
  time_t button_pressed_time=0;

#ifdef BUTTON_TEST
  time_t button_demo_time=time(NULL)+10;
#endif

#ifdef SHUTDOWN_TEST
  time_t shutdown_demo_time=time(NULL)+10;
#endif
  
  while (!done) {
    time_t current_time=time(NULL);

    // handle background processes
    if (shutdown_cmd_pid) {
      int wstatus;
      pid_t w=waitpid(shutdown_cmd_pid,&wstatus,WNOHANG);
      if (w!=0) {
        if (w==-1) {
          cleanup();
          perror("waitpid");
          exit(EXIT_FAILURE);
        }
        if (WIFEXITED(wstatus)) {
          logprintf(LOGLEVEL_NOTICE,
                    "ShutdownCmd %s exited with status %d",
                    settings.shutdown_cmd, WEXITSTATUS(wstatus));
        }
        shutdown_cmd_pid=0;
      }
    }
    if (button_pressed_cmd_pid) {
      int wstatus;
      pid_t w=waitpid(button_pressed_cmd_pid,&wstatus,WNOHANG);
      if (w!=0) {
        if (w==-1) {
          cleanup();
          perror("waitpid");
          exit(EXIT_FAILURE);
        }
        if (WIFEXITED(wstatus)) {
          logprintf(LOGLEVEL_NOTICE,
                    "ButtonpressedCmd %s exited with status %d",
                    settings.button_pressed_cmd, WEXITSTATUS(wstatus));
        }
        button_pressed_cmd_pid=0;
      }
    }

    // read voltage
    if (current_time!=voltage_time && settings.log_level>=LOGLEVEL_NOTICE) {
      read_voltage();
      print_voltage();
      voltage_time=current_time;
    }

    // read status
    status_old=status;
    if (read_status()==-1) {
      continue;
    }
#ifdef BUTTON_TEST
    if (current_time>button_demo_time) {
      status.bit.button_pressed=1;
      button_demo_time=current_time+5000;
    }
#endif
#ifdef SHUTDOWN_TEST
    if (current_time>shutdown_demo_time) {
      status.bit.primary_power_supply=0;
    }
#endif
    print_status();
    print_change_status();

    // handle button press
    if (!status_old.bit.button_pressed && status.bit.button_pressed
        && current_time-button_pressed_time>settings.button_pressed_delay
        && !button_pressed_cmd_pid
        && settings.button_pressed_cmd[0]) {
      button_pressed_time=current_time;
      button_pressed_cmd_pid=run_command(settings.button_pressed_cmd);
      if (button_pressed_cmd_pid==-1) {
        logprintf(LOGLEVEL_ERROR,"Starting ButtonPressedCmd %s failed",settings.button_pressed_cmd);
        button_pressed_cmd_pid=0;
      }
      else 
        logprintf(LOGLEVEL_NOTICE,"Successfully started Button_PressedCmd %s",settings.button_pressed_cmd);
    }
    
    switch (state) {
    case STATE_NORMAL:
      if (!status.bit.primary_power_supply) {
        shutdown_time=current_time;
        state=STATE_SHUTDOWN;
      }
      break;
    case STATE_SHUTDOWN:
      if (status.bit.primary_power_supply) {
        state=STATE_NORMAL;
        break;
      }
      if (current_time-shutdown_time > settings.shutdown_timer) {
        if (!shutdown_cmd_pid && settings.shutdown_cmd[0]) {
          shutdown_cmd_pid=run_command(settings.shutdown_cmd);
          if (shutdown_cmd_pid==-1) {
            logprintf(LOGLEVEL_ERROR,"Starting ShutdownCmd %s failed",settings.shutdown_cmd);
            shutdown_cmd_pid=0;
          }
          else 
            logprintf(LOGLEVEL_NOTICE,"Successfully started ShutdownCmd %s",settings.shutdown_cmd);
        }
        state=STATE_NORMAL;
      }
      else {
        if (current_time!=seconds_time)
          logprintf(LOGLEVEL_NOTICE,"Shutdown Timer: %d/%d",current_time-shutdown_time,settings.shutdown_timer);
      }
      //      if (current_time-shutdown_time > 5) state=STATE_NORMAL;
      break;
    }

    seconds_time=current_time;
    nanosleep(&sleeptime,0);
  }

  // Cleanup
  if (done!=-1) cleanup();
  logprintf(LOGLEVEL_NOTICE,"Terminated by signal %d",done);

  return 0;
}

