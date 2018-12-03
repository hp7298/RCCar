#include "gpiolib_addr.h"
#include "gpiolib_reg.h"

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <linux/watchdog.h> 
#include <errno.h>
#include <math.h>
#include <sys/time.h>

struct js_event {
	unsigned int time;      /* event timestamp in milliseconds */
	short value;   /* value */
	unsigned char type;     /* event type */
	unsigned char number;   /* axis/button number */
};

void getTime(char* buffer) {
	struct timeval tv;
	time_t curtime;
	
	gettimeofday(&tv, NULL);
	curtime=tv.tv_sec;
	
	strftime(buffer,30,"%m-%d-%Y  %T.",localtime(&curtime));	
}

FILE* openLog(char* name) {
	FILE* log = fopen("car.log", "a");
	
	if (log == NULL) {
		char buffer[30];
		getTime(buffer);
		fprintf(stderr, "%s : %s : sev=%s : %s\n", buffer, name, "Warning", "car.log was unable to be opened. Restarting.");
		perror("");
		fflush(stderr);
		exit(0);
	}
	return log;
}

void writeLog(FILE* log, char* name, char* error, char* detail) {
	char buffer[30];
	getTime(buffer);
	fprintf(log, "%s : %s : sev=%s: %s\n", buffer, name, error, detail);
	fflush(log);
}

FILE* openConfig(FILE* log, char* name) {
	
	FILE* config = fopen("/home/pi/car.cfg", "r");
	if (config == NULL) {
		writeLog(log, name, "Critical", "car.cfg was unable to be read. Rebooting");
	}
	
	FILE* configLog = fopen("config.log", "a");
	if (configLog == NULL) {
		writeLog(log, name, "Warning", "config.log was unable to be opened and read.");
	}
	
	writeLog(configLog, name, "Debug", "car.cfg was opened and read.");
	
	return config;

}

int setConfig(FILE* config, int* pins, int size) {
	char buffer[8];
	int i = 0;
	
	while (i < 5) {
		fgets(buffer, 8, config);
		pins[i] = atoi(buffer);
		i++;
	}
	
	fgets(buffer, 8, config);
	return atoi(buffer);
	
}

// Function to initialize and return the GPIO_Handle
GPIO_Handle initializeGPIO(FILE* log, char* name) {
	// initialize GPIO_Handle
	GPIO_Handle gpio = gpiolib_init_gpio();
	// If gpio could not successfully be initialized
	// Go to function errorMessage with error code 1
	if(gpio == NULL) {
		writeLog(log, name, "Critical", "GPIO was unable to be initialized. Rebooting.");
		exit(0);
	}
	writeLog(log, name, "Debug", "GPIO pins were initialized.");
	return gpio;	
}

// Function to allow the gpio pin to output 3.3V, takes in gpio and pinNumber
void outputOn(GPIO_Handle gpio, int pinNumber)
{
	// Turns on the pinNumber specified
	gpiolib_write_reg(gpio, GPSET(0), 1 << pinNumber);
}

// Function that turns off given gpio pin, takes in gpio and pinNumber
void outputOff(GPIO_Handle gpio, int pinNumber)
{
	// Turns off the pinNumber specified
	gpiolib_write_reg(gpio, GPCLR(0), 1 << pinNumber);
}

int openJoystick(GPIO_Handle gpio, FILE* log, char* name, int buzzer, int watchdog) {
	outputOn(gpio, buzzer);
	clock_t start;
	start = time(NULL);
	int fd = open ("/dev/input/js1", O_RDONLY | O_NONBLOCK);
	while (fd == -1) {
		if (time(NULL) - start >= 1) {
			ioctl(watchdog, WDIOC_KEEPALIVE, 0);
			start = time(NULL);
		}
		fd = open ("/dev/input/js1", O_RDONLY | O_NONBLOCK);
	}
	
	outputOff(gpio, buzzer);
	
	writeLog(log, name, "Debug", "Joystick has been connected.");
	
	return fd;
	
}

void offAll(GPIO_Handle gpio, int *pins, int size) {
	int i = 0;
	while (i < size) {
		outputOff(gpio, pins[i]);
		i++;
	}
}

void initializePin(GPIO_Handle gpio, int pinNum) {
	uint32_t sel_reg = gpiolib_read_reg(gpio, GPFSEL(pinNum / 10));
	sel_reg |= 1 << ((pinNum % 10)* 3);
	gpiolib_write_reg(gpio, GPFSEL(pinNum / 10), sel_reg);
}

void initializeAllPins(GPIO_Handle gpio, int* pins, int size) {
	int i = 0;
	while (i < size) {
		initializePin(gpio, pins[i]);
		++i;
	}	
}

int startWatchDog(FILE* log, char* name, int timer) {
	int dog = open("/dev/watchdog", O_WRONLY);
	if (dog == -1) {
		writeLog(log, name, "Critical", "Watchdog was unable to be opened.");
		system("sudo reboot");
	}
		
	writeLog(log, name, "Debug", "Watchdog was successfully opened.");
	
	int timeout = timer;
	if (timer < 0 || timer > 15)
		timeout = 5;
	
	ioctl(dog, WDIOC_SETTIMEOUT, &timeout);
	
	int actualTimeout = -1;
	ioctl(dog, WDIOC_GETTIMEOUT, &actualTimeout);
	
	char buffer[128];
	snprintf(buffer, 128, "Watchdog time out set to %d seconds.", actualTimeout);
	writeLog(log, name, "Debug", buffer);
	
	return dog;
}

void stopWatchDog(FILE* log, char* name, int watchdog) {
	writeLog(log, name, "Debug", "Watchdog has been stopped.");
	
	write(watchdog, "V", 1);
	
	close(watchdog);
}


////


void reboot(GPIO_Handle gpio, FILE* log, char* name, char* error, char* detail, int buzzer, int* pins) {
	writeLog(log, name, error, detail);
	outputOn(gpio, buzzer);
	usleep(500000);
	outputOff(gpio, buzzer);
	usleep(500000);
	outputOn(gpio, buzzer);
	usleep(500000);
	outputOff(gpio, buzzer);
	usleep(500000);
	outputOn(gpio, buzzer);
	usleep(500000);
	outputOff(gpio, buzzer);
	offAll(gpio, pins, 5);
	exit(0);
}

void wheelControl(GPIO_Handle gpio, int counter, float PWM, int pin) {
	if (counter <= PWM)
		outputOn(gpio, pin);
	else 
		outputOff(gpio, pin);
}

void PID(FILE* log, char* name) {
	char buffer[30];
	getTime(buffer);
	fprintf(log, "%s : %s : sev=Info : The program began with PID %d\n", buffer, name, getpid());
}
	

int main(const int argc, const char* const argv[]) {
	
	const char* argName = argv[0];
	
	int i = 0;
	int namelength = 0;
	
	while (argName[i] != 0) {
		namelength++;
		i++;
	}
	
	char programName[namelength];
	
	i = 0;
	
	while(argName[i + 2] != 0) {
		programName[i] = argName[i + 2];
		i++;
	}
	
	
	FILE* log = openLog(programName);
	PID(log, programName);
	// PRINT PID HERE
	
	GPIO_Handle gpio = initializeGPIO(log, programName);

	int pins[5];
	FILE* config = openConfig(log, programName);

	
	int timer = setConfig(config, pins, 5);
	
	int watchdog = startWatchDog(log, programName, timer);
	
	initializeAllPins(gpio, pins, 5);
	
	const int left_f = pins[0]; // 18
	const int left_b = pins[1]; // 17
	const int right_f = pins[2]; // 22
	const int right_b = pins[3]; // 23
	const int buzzer = pins[4]; // 21
		
	int offPressed = 0;
	clock_t offStart = 0;
	clock_t offCurrent = 0;
	
	int buttonPrev = 0;
		
	const int MINy = -25000;
	const int MAXy = 25000;
	
	const int MINx = -25000;
	const int MAXx = 25000;
	
	float left_f_PWM = 0;
	float left_b_PWM = 0;
	float right_f_PWM =0;
	float right_b_PWM = 0;
		
	float left = 0;
	float right = 0;
	float y = 0;
	float x = 0;
	
	offAll(gpio, pins, 5);
	
	int fd = openJoystick(gpio, log, programName, buzzer, watchdog);
	
	struct js_event e;
		
	int counter = 0;
	
	time_t start = time(NULL);
	time_t check = time(NULL);
	
	while (1) {
		if (time(NULL) - start >= 1) {
			ioctl(watchdog, WDIOC_KEEPALIVE, 0);
			start = time(NULL);
		}
		if (time(NULL) - check >= 60) {
			writeLog(log, programName, "Info", "Program is running successfully.");
			check = time(NULL);
		}
		if (counter >= 100) {
			if(read( fd, &e, sizeof(e)) == -1) {
				if (errno != EAGAIN) {
					reboot(gpio, log, programName, "Error", "Joystick file cannot be read anymore. Rebooting", buzzer, pins);
				}
			} 
			else {
				if (e.number == 9) {
					if (e.value) {
						stopWatchDog(log, programName, watchdog);
						writeLog(log, programName, "Debug", "Program was choosen to be closed for debugging.");
						outputOn(gpio, buzzer);
						usleep(2000000);
						outputOff(gpio, buzzer);
						offAll(gpio, pins, 5);
						exit(1);
					}
				}
					
				
				if (e.number == 7) {
					if (e.value == 1) {
						outputOn(gpio, buzzer);
						if (buttonPrev == 0) {
							buttonPrev = 1;
							writeLog(log, programName, "Info", "Honk-Honk!"); 
						}
					}
					else {
						outputOff(gpio, buzzer);
						if (buttonPrev == 1)
							buttonPrev = 0;
					}
				}
				

				if (e.number == 3)
					y = ((2.0 * (e.value - MINy)) / (MAXy - MINy)) - 1.0;
				if (e.number == 2)
					x = ((2.0 * (e.value - MINx)) / (MAXx - MINx)) - 1.0;
				
				if(fabs(x) < 0.1)
					x = 0;
				
				if (fabs(y) < 0.1)
					y = 0;
				
				left = y - x;
				right = y + x;
				
				if (left > 1)
					left = 1;
				if (right > 1)
					right = 1;
				
				if (left < -1)
					left = -1;
				if (right < -1)
					right = -1;
						
				left_f_PWM = 0;
				left_b_PWM = 0;
				right_f_PWM = 0;
				right_b_PWM = 0;
				
				if (left > 0)
					left_f_PWM = left * 100.0;
				else
					left_b_PWM = -left* 100.0;
				
				if (right > 0)
					right_f_PWM = right * 100.0;
				else
					right_b_PWM = -right * 100.0;
			}
			counter = 0;
		}
		
		wheelControl(gpio, counter, left_f_PWM, left_f);
		wheelControl(gpio, counter, left_b_PWM, left_b);
		wheelControl(gpio, counter, right_f_PWM, right_f);
		wheelControl(gpio, counter, right_b_PWM, right_b);
	
		counter++;
		
		usleep(10);
	
	}
	
	return 0;

}
