#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#define BIT(nr) (1UL << (nr))

// alarm_reg bits
#define ALARM_CAP_UV BIT(0) //capacitor undervoltage alarm
#define ALARM_CAP_OV BIT(1) //capacitor overvoltage alarm
#define ALARM_GPI_UV BIT(2) //gpi undervoltage alarm
#define ALARM_GPI_OV BIT(3) //gpi overvoltage alarm
#define ALARM_VIN_UV BIT(4) //vin undervoltage alarm
#define ALARM_VIN_OV BIT(5) //vin overvoltage alarm
#define ALARM_VCAP_UV BIT(6) //vcap undervoltage alarm
#define ALARM_VCAP_OV BIT(7) //vcap overvoltage alarm
#define ALARM_VOUT_UV BIT(8) //vout undervoltage alarm
#define ALARM_VOUT_OV BIT(9) //vout overvoltage alarm
#define ALARM_IIN_OC BIT(10) //input overcurrent alarm
#define ALARM_ICHG_UC BIT(11) //charge undercurrent alarm
#define ALARM_DTEMP_COLD BIT(12) //die temperature cold alarm
#define ALARM_DTEMP_HOT BIT(13) //die temperature hot alarm
#define ALARM_ESR_HI BIT(14) //esr high alarm
#define ALARM_CAP_LO BIT(15) //capacitance low alarm

// mon status bits
#define MON_CAPSR_ACTIVE BIT(0)
#define MON_CAPESR_SCHEDULED BIT(1)
#define MON_CAPESR_PENDING BIT(2)
#define MON_CAP_DONE BIT(3)
#define MON_ESR_DONE BIT(4)
#define MON_CAP_FAILED BIT(5)
#define MON_ESR_FAILED BIT(6)
#define MON_POWER_FAILED BIT(8)
#define MON_POWER_RETURNED BIT(9)

// charger status bits
#define CHRG_STEPDOWN BIT(0)
#define CHRG_STEPUP BIT(1)
#define CHRG_CV BIT(2)
#define CHRG_UVLO BIT(3)
#define CHRG_INPUT_ILIM BIT(4)
#define CHRG_CAPPG BIT(5)
#define CHRG_SHNT BIT(6)
#define CHRG_BAL BIT(7)
#define CHRG_DIS BIT(8)
#define CHRG_CI BIT(9)
#define CHRG_PFO BIT(11)


#define log_monitor(alert_name, description) \
	if (monitor & alert_name) { \
		printf(#description "\n"); \
	}

/*
#define log_alarm(alert_name, reg1, reg2, description) \
  if (alarms & alert_name) { \
	int value1 = read_integer_value(reg1); \
	int value2 = read_integer_value(reg2); \
	if(value1 < 0 || value2 < 0) { \
	  perror("Error reading register"); \
	} else { \
	  printf(#description " %s: %d; %s: %d;\n", reg1, value1, reg2, value2); \
	} \
  }
*/

#define PATH_MAX 4096
//ohms
#define RT 86600
#define RTST 121
// microohms
#define RSNSC 5

static int fds[3] = {-1, -1, -1};

int show();
int await_alerts();
int write_value(char *name, char *value, char *unit);
int read_file(char *attr_name, char *buf);
int read_integer_value(char *attr_name);
int status_report();
int clear_all();
void signal_handler(int sig);
int convert_to_LSB(long value, char *unit, char *attr);
char * convert_from_LSB(char * buf, char * attr_name);
int LSB_to_celsius(long long meas_dtemp);
int LSB_to_farads(int units);
int LSB_to_milliohms(int units);
int LSB_to_millivolts(int units, int conversion_factor);
int celsius_to_LSB(int degrees);
int farads_to_LSB(long long cap);
int meas_trunc(long long number);
int milliohms_to_LSB(long long esr);
int millivolts_to_LSB(long long voltage_millivolts, int conversion_factor);
int starts_with(const char *str, const char *prefix);
static inline int throw(const char *message, int error);
static inline void log_chrg(int chrg, int bit, const char *description);
static void log_alarm(int alarms, int alarm_num, char *alarm_desc, char *reg1, char *reg2, char *desc1, char *desc2);
char * description(char *reg);

#define SYSFS_PATH "/sys/bus/i2c/devices/i2c-2/2-0009/hwmon/hwmon4"
#define usage "Usage: %s <command>\nAccepted commands:\n\tshow\n\tawait\n\twrite file value [measurement unit]\n\tread [-c] file\n\tclear\n\tstatus\n"

int main(int argc, char* argv[]){ 
	signal(SIGINT, signal_handler);
	
	if(argc < 2){
		printf(usage, argv[0]);
		return 1;
	}
	if(strcmp("show", argv[1]) == 0) {
		return show();
	}
	if(strcmp("await", argv[1]) == 0) {
		return await_alerts();
	}
	if(strcmp("write", argv[1]) == 0 && argc > 3) {
		if(argc == 4){
			return write_value(argv[2], argv[3], "");
		} else if(argc == 5){
			return write_value(argv[2], argv[3], argv[4]);
		}
		return 1;
	}
	if(strcmp("read", argv[1]) == 0 && argc > 2) {
		if (argc == 3) {
			printf("%d\n", read_integer_value(argv[2]));
			return 0;
		}
		else if (argc == 4 && strcmp("-c", argv[2]) == 0) {
			char buf[10];
			if (read_file(argv[3], buf))
				return 1;
			char *result = convert_from_LSB(buf, argv[3]);
			if(result != NULL)
				printf("%s\n", result);
			else
				printf("Error in main\n");
		}
		return 1;
	}
	if(strcmp("clear", argv[1]) == 0) {
		return clear_all();
	}
	if(strcmp("status", argv[1]) == 0) {
		printf("SUPERCAPACITORS STATUS REPORT\n");
		printf("-----------------------------\n\n");
		status_report();
		printf("REGISTERS VALUES:\n");
		show();
		return 0;
	}
	return 0;
}

/**
 * await_alerts() - poll for alerts
 * Return: on success is closed by SIGINT, returns errno on error.
*/
int await_alerts(){
	// we will check for alarms and monitor status alerts
	struct pollfd ufds[3];
	char paths[2][128];
	char data[6];

	printf("You will be notified in the event of an alarm, or a change in monitor status.\n");

	sprintf(paths[0], "%s/alarm_reg", SYSFS_PATH);
	sprintf(paths[1], "%s/mon_status", SYSFS_PATH);

	for(int i = 0; i<2; i++){
		if((fds[i] = open(paths[i], O_RDONLY)) < 0){
			int err = errno;
			fprintf(stderr, "await_alerts() Failed to open %s %s\n", paths[i], strerror(errno));
			return err;
		}
		ufds[i].fd = fds[i];
		ufds[i].revents = 0;
		// POLLPRI means there is some exceptional condition on the file descriptor
		// in this case, the sysfs_notify
		ufds[i].events = POLLPRI | POLLERR;
	}
	printf("Polling for alerts...\n");
	// these are dummy reads, so that we won't instantly get a notification
	read(fds[0], data, 6);
	read(fds[1], data, 6);
	// start waiting for a notification
	while(poll(ufds, 2, -1) >= 0){
		// this is so sysfs returns new data
		if((lseek(fds[0], 0, SEEK_SET) < 0) | (lseek(fds[1], 0, SEEK_SET) < 0))
			return throw("await_alerts Failure in lseek", -1);

		printf("New data ltc-monitor\n");
		// TODO magari non usare syscalls?
		if(ufds[0].revents & POLLPRI){
			if(read(fds[0], data, 6) > 0){
				data[5] = '\0';
				printf("[ltc-monitor] Alarm value: %s\n", data);
			}
			else
				return throw("await_alerts: Error in reading alarm register", errno);
		}
		if(ufds[1].revents & POLLPRI){
			if(read(fds[1], data, 6) > 0) {
				data[5] = '\0';
				printf("[ltc-monitor] Monitor value: %s\n", data);
			}
			else
				return throw("await_alerts: Error in reading monitor status", errno);
		}
		status_report();
	}
	// this shouldn't happen
	printf("Polling finished\n");
	return 0;
}

/**
 * show() - print sysfs directory contents
 *
 * Open directory, ignore uninteresting files, only read sysfs entries
 * Print file name, unconverted value, converted value
 * Return: 0 on success, otherwise error code
*/
int show(){
	DIR *dir;
	struct dirent *entry;
	int i = 0;

	//open directory
	dir = opendir(SYSFS_PATH);
	if(dir == NULL)
		return throw("Error in opening directory", errno);

	while ((entry = readdir(dir)) != NULL) {
		struct stat statbuf;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "name") == 0
				|| strcmp(entry->d_name, "uevent") == 0){	
			continue;
		}

		char full_path[PATH_MAX];
		snprintf(full_path, sizeof(full_path), "%s/%s", SYSFS_PATH, entry->d_name);
		
		if (stat(full_path, &statbuf) == -1) {
			perror("Error getting file stat");
			continue;
		}
		
		// sysfs files are considered regular files.
		if (S_ISREG(statbuf.st_mode)) {
			char buf[10];
			char *buf2;
			printf("%-20s: ", entry->d_name);
			
			if (read_file(entry->d_name, buf))
				continue;
			buf[strlen(buf)-1] = '\0';
			printf("%-7s", buf);
			buf2 = convert_from_LSB(buf, entry->d_name);
			if (buf2 != NULL) {
				printf("%-7s", buf2);
				free(buf2);
			}
			// print in two columns
			if(i%2 == 0)
				printf("\t");
			else
				printf("\n");
			i++;
		}
	}
	printf("\n");
	return 0;
}

/**
 * 
 * Return: errno on failure, 0 on success.
*/
int read_file(char *attr_name, char *buf)
{
	char full_path[PATH_MAX];
	snprintf(full_path, sizeof(full_path), "%s/%s", SYSFS_PATH, attr_name);
	FILE *file = fopen(full_path, "r");

	if (file == NULL) 
		return throw("read_file Failed to open sysfs file", errno);

	if(fgets(buf, sizeof(buf), file) == NULL)
		return throw("read_file Failed to read from sysfs file", errno);

	fclose(file);
	return 0;
}

/*
 * Return: value read on success, -1 on failure
 * Since -1 can be a valid return value, it only causes a warning.
*/
int read_integer_value(char *attr_name){
	// Construct full path to the file
	char buf[10];
	int value;
	char *endptr;
	if (read_file(attr_name, buf)) 
		return throw("read_integer_value ", -1);

	value = strtol(buf, &endptr, 10);
	if (value == 0 && endptr == buf) {
		fprintf(stderr, "read_integer_value value is not valid %s\n", strerror(errno));
		return -1;
	}
	
	return value;
}
	
int write_value(char *name, char *value_string, char *unit)
{
	char full_path[PATH_MAX];
	FILE *file;
	long value;
	char *endptr;
	int val;

	snprintf(full_path, sizeof(full_path), "%s/%s", SYSFS_PATH, name);
	file = fopen(full_path,"r+");

	if(file == NULL)
		return throw("write_value Failed to open sysfs file", errno);

	if(strcmp(unit, "") == 0){
		if(fprintf(file, "%s", value_string) > 0){
			printf("Wrote %s on %s\n", value_string, name);
			return 0;
		}
		else {
			return throw("Could not write on file", errno);
		}
	}

	value = strtol(value_string, &endptr, 10);
	if(value == 0 && endptr == value_string)
		return throw("Value is not valid", EINVAL);

	val = convert_to_LSB(value, unit, name);

	if(fprintf(file, "%d", val)){
		printf("Wrote %d on %s\n", val, name);
		return 0;
	}
	return 1;
}

int clear_all(){
	char buf[7];
	int res;
	int active_alarms = read_integer_value("alarm_reg");
	snprintf(buf, sizeof(buf), "%d", active_alarms);
	res = write_value("clr_alarms", buf, "");
	if (res)
		return throw("Error in clearing alarms", res);
	return 0;
}

void signal_handler(int sig){
	for(int i = 0; i<3; i++){
		if(fds[i] >= 0){
			close(fds[0]);
		}
	}
	exit(0);
}

static inline int throw(const char *message, int error)
{
	perror(message);
	return error;
}

static inline void log_chrg(int chrg, int bit, const char *description){
	if(chrg & bit) {
		printf("%s\n", description);
	}
}

// UTILITY FUNCTIONS

int convert_to_LSB(long value, char *unit, char *attr_name)
{
	if(strcmp(unit, "mV") == 0){
		if(starts_with(attr_name, "vcap1") || starts_with(attr_name, "vcap2") || 
					starts_with(attr_name, "vcap3") || starts_with(attr_name, "vcap4") || 
					starts_with(attr_name, "gpi") || starts_with(attr_name, "vshunt") || 
					starts_with(attr_name, "cap_ov") || starts_with(attr_name, "cap_uv") ||
					starts_with(attr_name, "meas_vcap1") || starts_with(attr_name, "meas_vcap2") || 
					starts_with(attr_name, "meas_vcap3") || starts_with(attr_name, "meas_vcap4") || 
					starts_with(attr_name, "meas_gpi")) {
			return millivolts_to_LSB(value, 1835);
		}
		else if (starts_with(attr_name, "vcap") || starts_with(attr_name, "meas_vcap")) {
			return millivolts_to_LSB(value, 14760);
		}
		else if (starts_with(attr_name, "vin") || starts_with(attr_name, "vout") || 
				starts_with(attr_name, "meas_vin") || starts_with(attr_name, "meas_vout")) {
			return millivolts_to_LSB(value, 22100);
		}
	}
	else if(strcmp(unit, "F") == 0){
		return farads_to_LSB(value);
	} 
	else if (strcmp(unit, "C") == 0){
		return celsius_to_LSB(value);
	}
	else if (strcmp(unit, "mR") == 0){
		return milliohms_to_LSB(value);
	}
	return throw("convert_to_lsb: not a valid measurement unit", EINVAL);
}

int meas_trunc(long long number)
{
	int integer = (int) number;
	if (integer % 10 == 9) {
		integer++;
	}
	return integer;
}

/*
 * convert from the register's measurement unit to degrees celsius
 *  degrees = 0.028 * meas_dtemp - 251.4
 * using fixed point arithmetic, multiplying all constants by 1000
*/
int LSB_to_celsius(long long meas_dtemp)
{
	long long scaled_result = 28 * meas_dtemp - 251400;
	// this is to adjust for rounding errors
	// if the last decimal digit is 9 then we round up
	if(((scaled_result + 100) / 1000) != (scaled_result / 1000))
		scaled_result += 100;
	return meas_trunc((28 * meas_dtemp - 251400) / 1000);
}

/*
 * convert from degrees celsius to register's measurement unit
 * using fixed point arithmetic
*/
int celsius_to_LSB(int degrees)
{
	return meas_trunc(((long long) degrees*1000 + 251400) / 28);
}

/*
 * convert from millivolts to LSB
 * 1) LSB = 183.5 microvolts (1835)
 * 2) LSB = 2.21 millivolts = 2210 microvolts (22100)
 * 3) LSB = 1.476 millivolts = 1476 microvolts (14760)
*/
int millivolts_to_LSB(long long voltage_millivolts, int conversion_factor)
{
	// the conversion factor is multiplied by one hundred in order to avoid floating point division
	int scale_factor = 10;
	long long voltage_microvolts = voltage_millivolts * 1000;
	return meas_trunc(voltage_microvolts * scale_factor / conversion_factor);
}

/*
 * convert from LSB to millivolts
*/
int LSB_to_millivolts(int units, int conversion_factor)
{
	int scale_factor = 10;
	// example: conversion_factor = 1835;
	long long voltage_microvolts = units * conversion_factor;
	return meas_trunc(voltage_microvolts / (1000 * scale_factor));
}

//Capacitance stack value in LSB
int farads_to_LSB(long long cap)
{
	return 1000000 * cap * RTST / RT / 336;
}

//Capacitance stack value in farads
int LSB_to_farads(int units)
{
	long long result = (long long) units * 336 * RT / RTST / 1000000;
	return meas_trunc(result);
}

int milliohms_to_LSB(long long esr)
{
	return meas_trunc(esr * 64 / RSNSC);
}

int LSB_to_milliohms(int units)
{
	long long result = (long long) units * RSNSC / 64;
	return meas_trunc(result);
}

char * convert_from_LSB(char * buf, char * attr_name)
{
	long long value;
	char *buf2 = malloc(sizeof(char)*15);
	char unit[4];
	char *endptr;
	int val;

	if (buf == NULL) {
		return NULL;
	}
	
	value = strtoll(buf, &endptr, 10);
	if(value == 0 && endptr == buf) {
		printf("Value is %lld	\n", value);
		throw("convert_from_LSB: Error in conversion", -1);
		return NULL;
	}


	if(starts_with(attr_name, "vcap1") || starts_with(attr_name, "vcap2") 
			|| starts_with(attr_name, "vcap3") || starts_with(attr_name, "vcap4") 
			|| starts_with(attr_name, "gpi") || starts_with(attr_name, "vshunt") 
			|| starts_with(attr_name, "cap_ov") || starts_with(attr_name, "cap_uv")
			|| starts_with(attr_name, "meas_vcap1") || starts_with(attr_name, "meas_vcap2") 
			|| starts_with(attr_name, "meas_vcap3") || starts_with(attr_name, "meas_vcap4") 
			|| starts_with(attr_name, "meas_gpi")) {
		val = LSB_to_millivolts(value, 1835);
		strcpy(unit, "mV");
	}
	else if (starts_with(attr_name, "vcap_") || starts_with(attr_name, "meas_vcap")) {
		val = LSB_to_millivolts(value, 14760);
		strcpy(unit, "mV");
	}
	else if (starts_with(attr_name, "vin") || starts_with(attr_name, "vout")
				|| starts_with(attr_name, "meas_vin") || starts_with(attr_name, "meas_vout")) {
		val = LSB_to_millivolts(value, 22100);
		strcpy(unit, "mV");
	}
	else if (!starts_with(attr_name, "cap_esr") && (starts_with(attr_name, "cap") || starts_with(attr_name, "meas_cap"))) {
		val = LSB_to_farads(value);
		strcpy(unit, "F");
	}
	else if (starts_with(attr_name, "dtemp") || starts_with(attr_name, "meas_dtemp")) {
		val = LSB_to_celsius(value);
		strcpy(unit, "C");
	}
	else if (starts_with(attr_name, "esr") || starts_with(attr_name, "meas_esr")) {
		val = LSB_to_milliohms(value);
		strcpy(unit, "mR");
	}
	//else if (starts_with(attr_name, "msk_alarms") || starts_with(attr_name, "alarm_reg") || starts_with(attr_name, "clr_alarms") {
	else{
		val = (int) value;
		strcpy(unit, "");
	}
	snprintf(buf2, 15, "%d %s", val, unit);

	return buf2;
}

// true is 1
int starts_with(const char *str, const char *prefix)
{
	size_t str_len = strlen(str);
	size_t prefix_len = strlen(prefix);

	if (prefix_len > str_len)
			return 0;

	return strncmp(str, prefix, prefix_len) == 0;
}

int status_report(void)
{
	int alarms = read_integer_value("alarm_reg");
	int monitor = read_integer_value("mon_status");
	int chrg = read_integer_value("chrg_status");
	if(alarms == -1 || monitor == -1 || chrg == -1)
		printf("Warning: alarms/monitor/charger status value may be wrong.");


	printf("MONITOR STATUS:\n");;

	log_monitor(MON_CAPSR_ACTIVE, Capacitance/ESR measurement is in progress.)
	log_monitor(MON_CAPESR_SCHEDULED, Waiting programmed time to begin a capacitance/ESR measurement.)
	log_monitor(MON_CAPESR_PENDING, Waiting for satisfactory conditions to begin a capacitance/ESR measurement.)
	log_monitor(MON_CAP_DONE, Capacitance measurement has completed.)
	log_monitor(MON_ESR_DONE, ESR Measurement has completed.)
	log_monitor(MON_CAP_FAILED, The last attempted capacitance measurement was unable to complete.)
	log_monitor(MON_ESR_FAILED, The last attempted ESR measurement was unable to complete.)
	log_monitor(MON_POWER_FAILED, The device is no longer connected to power outlet.)
	log_monitor(MON_POWER_RETURNED, The device is connected to power outlet.)

	printf("ALARMS:\n");
	

	log_alarm(alarms, ALARM_CAP_UV, "Capacitor undervoltage alarm", "meas_cap", "cap_uv_lvl", "meas_cap", "Capacitor Undervoltage Level");
	log_alarm(alarms, ALARM_CAP_OV, "Capacitor overvoltage alarm", "meas_cap", "cap_ov_lvl", "meas_cap", "Capacitor Overvoltage Level");
	log_alarm(alarms, ALARM_GPI_UV, "General purpose Undervoltage alarm", "meas_gpi", "gpi_uv_lvl", "Measured GPI pin voltage", "General Purpose Input Undervoltage Level");
	log_alarm(alarms, ALARM_GPI_OV, "General purpose Overvoltage alarm", "meas_gpi", "gpi_ov_lvl", "Measured GPI pin voltage", "General Purpose Input Overvoltage Level");
	log_alarm(alarms, ALARM_VIN_UV, "Input Undervoltage alarm", "meas_vin", "vin_uv_lvl", "Measured VIN voltage", "General Purpose Input Undervoltage Level");
	log_alarm(alarms, ALARM_VIN_OV, "Input Overvoltage alarm", "meas_vin", "vin_ov_lvl", "Measured VIN voltage", "General Purpose Input Overvoltage Level");
	log_alarm(alarms, ALARM_VCAP_UV, "Capacitor undervoltage alarm", "meas_vcap", "vcap_uv_lvl", "Measured VCAP voltage", "VCAP Undervoltage Level");
	log_alarm(alarms, ALARM_VCAP_OV, "Capacitor overvoltage alarm", "meas_vcap", "vcap_ov_lvl", "Measured VCAP voltage", "VCAP Overvoltage Level");
	log_alarm(alarms, ALARM_VOUT_UV, "Output Undervoltage alarm", "meas_vout", "vout_uv_lvl", "Measured VOUT voltage", "VOUT Undervoltage Level");
	log_alarm(alarms, ALARM_VOUT_OV, "Output Overvoltage alarm", "meas_vout", "vout_ov_lvl", "Measured VOUT voltage", "VOUT Overvoltage Level");
	log_alarm(alarms, ALARM_IIN_OC, "Input overcurrent alarm", "meas_iin", "iin_oc_lvl", "Measured IIN current", "Input Overcurrent Level");
	log_alarm(alarms, ALARM_ICHG_UC, "Charge Undercurrent alarm", "meas_ichg", "ichg_uc_lvl", "Measured ICHG current", "Charge Undercurrent Level");
	log_alarm(alarms, ALARM_DTEMP_COLD, "Temperature Cold alarm", "meas_dtemp", "dtemp_cold_lvl", "Measured die temperature", "Die temperature Cold level");
	log_alarm(alarms, ALARM_DTEMP_HOT, "Temperature hot alarm", "meas_dtemp", "dtemp_hot_lvl", "Measured die temperature", "Die Temperature Hot Level");
	log_alarm(alarms, ALARM_ESR_HI, "stack ESR high alarm", "meas_esr", "esr_hi_lvl", "Measured ESR value", "ESR High Level");
	log_alarm(alarms, ALARM_CAP_LO, "stack capacitance low alarm", "meas_cap", "cap_lo_lvl", "Measured capacitance value", "Capacitance Low Level");

	printf("CHARGER STATUS:\n");

	log_chrg(chrg, CHRG_STEPDOWN, "The synchronous controller is in step-down mode (charging)");
	log_chrg(chrg, CHRG_STEPUP, "The synchronous controller is in step-up mode (backup)");
	log_chrg(chrg, CHRG_CV, "The charger is in constant voltage mode");
	log_chrg(chrg, CHRG_UVLO, "The charger is in undervoltage lockout");
	log_chrg(chrg, CHRG_INPUT_ILIM, "The charger is in input current limit");
	log_chrg(chrg, CHRG_CAPPG, "The capacitor voltage is above power good threshold");
	log_chrg(chrg, CHRG_SHNT, "The capacitor manager is shunting");
	log_chrg(chrg, CHRG_BAL, "The capacitor manager is balancing");
	log_chrg(chrg, CHRG_DIS, "The charger is temporarily disabled for capacitance measurement");
	log_chrg(chrg, CHRG_CI, "The charger is in constant current mode");
	log_chrg(chrg, CHRG_PFO, "Input voltage is below pfi threshold");
	
	return 0;
}


static void log_alarm(int alarms, int alarm_num, char *alarm_desc, char *reg1, char *reg2, char *desc1, char *desc2) {
	if(alarms & alarm_num) {
		int val1, val2;
		printf("%s\n", alarm_desc);
		val1 = read_integer_value(reg1);
		val2 = read_integer_value(reg2);
		if(val1 == -1 || val2 == -1) {
			printf("log_alarm Warning: values may be wrong.\n");
		}
		printf("%s: %d. %s: %d\n", desc1, val1, desc2, val2);
	}
}








