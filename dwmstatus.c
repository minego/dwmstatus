/** ********************************************************************
 * DWM STATUS by <clement@6pi.fr>
 *
 * Compile with:
 * gcc -Wall -pedantic -std=c99 -lX11 -lasound dwmstatus.c
 *
 **/

/*
	Heavily modified version of dwm status (originally by <clement@6pi.fr> with
	customizations for my own machines.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <wchar.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <X11/Xlib.h>

#define COLOR_RED			"#FF0000"
#define COLOR_WHITE			"#FFFFFF"
#define DEGREE_CHAR			((char) 176)

#define MAX_CPUS			32
#define BAR_HEIGHT			15
#define BAT_NOW_FILE		"/sys/class/power_supply/BAT0/charge_now"
#define BAT_FULL_FILE		"/sys/class/power_supply/BAT0/charge_full"
#define BAT_STATUS_FILE		"/sys/class/power_supply/BAT0/status"
#define TEMP_SENSOR_FILE	"/sys/class/hwmon/hwmon1/temp1_input"

static char *vBar(int percent, int w, int h, char *fg_color, char *bg_color)
{
	char		*value;
	char		*format		= "^c%s^^r0,%d,%d,%d^^c%s^^r0,%d,%d,%d^";
	int			bar_height	= (percent * h) / 100;
	int			y			= (BAR_HEIGHT - h) / 2;

	if (!(value = (char*) malloc(sizeof(char) * 128))) {
		fprintf(stderr, "Cannot allocate memory for buf.\n");
		exit(1);
	}

	snprintf(value, 128, format, bg_color, y, w, h, fg_color, y + h - bar_height, w, bar_height);
	return(value);
}

static int hBar(char *string, size_t size, int percent, int w, int h, char *fg_color, char *bg_color)
{
	char		*format		= "^c%s^^r0,%d,%d,%d^^c%s^^r%d,%d,%d,%d^";
	int			bar_width	= (percent * w) / 100;
	int			y = (BAR_HEIGHT - h) / 2;

	return(snprintf(string, size, format, fg_color, y, bar_width, h, bg_color, bar_width, y, w - bar_width, h));
}

static int hBarBordered(char *string, size_t size, int percent, int w, int h, char *fg_color, char *bg_color, char *border_color)
{
	char		*format		= "^c%s^^r0,%d,%d,%d^^f1^%s";
	int			y			= (BAR_HEIGHT - h)/2;
	char		tmp[128];

	hBar(tmp, 128, percent, w - 2, h - 2, fg_color, bg_color);
	return(snprintf(string, size, format, border_color, y, w, h, tmp));
}

static void percentColor(char *string, int percent)
{
	char		*format	= "#%X0%X000";
	int			g		= (percent * 15) / 100;
	int			r		= 15 - g;

	snprintf(string, 8, format, r, g);
}

static int getBattery(void)
{
	FILE		*f;
	int			energy_now;
	static int	energy_full		= -1;

	if (energy_full == -1) {
		if (!(f = fopen(BAT_FULL_FILE, "r"))) {
			return -1;
		}

		fscanf(f, "%d", &energy_full);
		fclose(f);
	}

	if (!(f = fopen(BAT_NOW_FILE, "r"))) {
		return -1;
	}

	fscanf(f, "%d", &energy_now);
	fclose(f);

	return(((float) energy_now / (float) energy_full) * 100);
}

/*
	Return:
		'D'		Discharging
		'F'		Full
		'C'		Charging
		'\0'	error
*/
static char getBatteryStatus()
{
	FILE		*f;
	char		status;

	if (!(f = fopen(BAT_STATUS_FILE, "r"))) {
		return(-1);
	}

	if (1 != fread(&status, sizeof(char), 1, f)) {
		status = '\0';
	}
	fclose(f);
	return(toupper(status));
}

static int getBatteryBar(char *string, size_t size, int w, int h)
{
	int		percent			= getBattery();
	int		y				= (BAR_HEIGHT - 5) / 2;
	char	*bg_color		= "#444444";
	char	*border_color	= "#EEEEEE";
	char	*format			= "%s^c%s^^f%d^^r0,%d,%d,%d^^f%d^";
	char	fg_color[8];
	char	tmp[128];

	switch (getBatteryStatus()) {
		case 'C':	/* Charging		*/
		case 'F':	/* Full			*/
			memcpy(fg_color, border_color, 8);
			break;

		case 'D':	/* Discharging	*/
			percentColor(fg_color, percent);
			break;

		default:
			return(-1);
	}

	hBarBordered(tmp, 128, percent, w - 2, h, fg_color, bg_color, border_color);
	return(snprintf(string, size, format, tmp, border_color, w - 2, y, 2, 5, 2));
}

/* Return the number of cpus detected */
static int getCPUUsage(int *cpuper)
{
	FILE		*f;
	size_t		len		= 0;
	char		*line	= NULL;
	int			count	= 0;
	int			i;
	long int	idle_time, other_time;
	char		cpu_name[32];
	static int	new_cpu_usage[MAX_CPUS][4];
	static int	old_cpu_usage[MAX_CPUS][4];

	if (!(f = fopen("/proc/stat", "r"))) {
		return(-1);
	}

	for (i = 0; i < MAX_CPUS && !feof(f); ++i) {
		line = NULL;
		getline(&line, &len, f);

		if (!line) {
			break;
		}

		if (5 != sscanf(line, "%s %d %d %d %d",
				cpu_name,
				&new_cpu_usage[i][0],
				&new_cpu_usage[i][1],
				&new_cpu_usage[i][2],
				&new_cpu_usage[i][3])
		) {
			free(line);
			continue;
		}
		free(line);

		if (strncasecmp(cpu_name, "cpu", 3)) {
			continue;
		}
		count++;

		idle_time	= new_cpu_usage[i][3] - old_cpu_usage[i][3];
		other_time	= new_cpu_usage[i][0] - old_cpu_usage[i][0]
					+ new_cpu_usage[i][1] - old_cpu_usage[i][1]
					+ new_cpu_usage[i][2] - old_cpu_usage[i][2];

		if (idle_time + other_time) {
			cpuper[i] = (other_time * 100) / (idle_time + other_time);
		} else {
			cpuper[i] = 0;
		}

		old_cpu_usage[i][0] = new_cpu_usage[i][0];
		old_cpu_usage[i][1] = new_cpu_usage[i][1];
		old_cpu_usage[i][2] = new_cpu_usage[i][2];
		old_cpu_usage[i][3] = new_cpu_usage[i][3];
	}

	fclose(f);
	return(count);
}

static int getMEMUsage(void)
{
	FILE		*f;
	size_t		len		= 0;
	char		*line	= NULL;
	int			kbytes, totalkb = 0, availkb = 0;
	int			r;
	char		name[128];

	if (!(f = fopen("/proc/meminfo", "r"))) {
		return(-1);
	}

	while (!feof(f)) {
		line = NULL;
		getline(&line, &len, f);
		if (!line) {
			break;
		}

		kbytes = 0;
		if (2 != (r = sscanf(line, "%s %d", name, &kbytes))) {
			free(line);
			break;
		}
		free(line);

		/* printf("Name: %s, kbytes: %d\n", name, kbytes); */
		if (!strcasecmp(name, "MemTotal:")) {
			totalkb = kbytes;
		} else if (!strcasecmp(name, "MemAvailable:")) {
			availkb = kbytes;
		}
	}
	fclose(f);

	if (!totalkb) {
		return(0);
	}

	return(((totalkb - availkb) * 100) / totalkb);
}

static char * getDateTime(char *format)
{
	time_t		result		= time(NULL);
	struct tm	*resulttm	= localtime(&result);
	size_t		size		= 64;
	char		*buf;

	if (!(buf = malloc(size + 1))) {
		fprintf(stderr, "Cannot allocate memory for buf.\n");
		exit(1);
	}

	if (!resulttm) {
		fprintf(stderr, "Error getting localtime.\n");
		exit(1);
	}

	if (!strftime(buf, size, format, resulttm)) {
		fprintf(stderr, "strftime is 0.\n");
		exit(1);
	}

	return(buf);
}

static int getTemperature()
{
	int		temp;
	FILE	*f;

	if (!(f = fopen(TEMP_SENSOR_FILE, "r"))) {
		fprintf(stderr, "Error opening temp1_input.\n");
		return(-1);
	}

	fscanf(f, "%d", &temp);
	fclose(f);

	temp = temp / 1000;

	/* Convert to fahrenheit */
	temp = (9.0 / 5.0) * temp + 32.0;

	return(temp);
}

static int getWifiPercent()
{
	int		percent = -1;
	FILE	*f;

	if (!(f = fopen("/proc/net/wireless", "r"))) {
		return(-1);
	}

	fscanf(f, "%*[^\n]\n%*[^\n]\n%*s %*[0-9] %d", &percent);
	fclose(f);

	return(percent);
}

int getScriptStr(char *path, char *dest, size_t size)
{
	FILE		*f;
	size_t		len;

	if (!(f = popen(path, "r"))) {
		return(-1);
	}

	*dest = '\0';
	fgets(dest, size, f);
	fclose(f);

	len = strlen(dest);
	while (len > 0 && isspace(dest[len - 1])) {
		dest[--len] = '\0';
	}


	if (*dest) {
		return(0);
	} else {
		return(-1);
	}
}

int getScriptPercentage(char *path)
{
	char	line[1024];

	if (!getScriptStr(path, line, sizeof(line))) {
		return(atoi(line));
	}

	return(-1);
}

static void setStatus(Display *dpy, char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

int main(int argc, char **argv)
{
	Display		*dpy;
	int			i, count;
	int			cpuper[MAX_CPUS];
	char		*value;
	char		*status;
	char		line[1024];
	char		buffer[4 * 1024];

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "Cannot open display.\n");
		return(EXIT_FAILURE);
	}

	for (;;) {
		status = buffer;
		*status = '\0';

/*
		TODO MPD
*/
		/* Phone call */
		if (!getScriptStr("dial what", line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"  ^c%s^%s", COLOR_RED, line);

			if (!getScriptStr("dial who", line, sizeof(line))) {
				status += snprintf(status, sizeof(buffer) - (status - buffer),
					"^c%s^%s ", COLOR_WHITE, line);
			}
		}

		/* CPU label */
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			"^c%s^CPU^c%s^^f1^", COLOR_RED, COLOR_WHITE);

		/* For each CPU (0 is average of all) */
		count = getCPUUsage(cpuper);
		for (i = 1; i < count; i++) {
			if (-1 == cpuper[i]) {
				break;
			}

			value = vBar(cpuper[i], 4, 15, "#FFFFFF", "#666666");
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s^f5^", value);
			free(value);
		}

		/* MEM usage */
		value = vBar((i = getMEMUsage()), 6, 15, "#FFFFFF", "#666666");
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			"  ^c%s^MEM^f1^%s^f6^^c%s^ %d%%", COLOR_RED, value, COLOR_WHITE, i);

		/* Volume */
		value = vBar((i = getScriptPercentage("volume.sh")), 6, 15, "#FFFFFF", "#666666");
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			" ^c%s^VOL^f1^%s^f6^^c%s^^f8^", COLOR_RED, value, COLOR_WHITE);

		/* Temp */
		if (0 < (i = getTemperature())) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				" ^c%s^TEMP^f1^^c%s^%d%cF^f4^ ", COLOR_RED, COLOR_WHITE, i, DEGREE_CHAR);
		}

		/* Wifi */
		if (0 <= (count = getWifiPercent())) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				" ^c%s^WIFI^c%s^^f1^", COLOR_RED, COLOR_WHITE);

			/* Show 5 bars, each on if the value is high enough... */
			for (i = 1; i <= 5; i++) {
				if (count + 5 >= (20 * i)) {
					value = vBar(20 * i, 5, 15, "#FFFFFF", "#222222");
				} else {
					value = vBar(20 * i, 5, 15, "#666666", "#222222");
				}

				status += snprintf(status, sizeof(buffer) - (status - buffer),
					"%s^f6^", value);
				free(value);
			}
		}

		if (0 < getBatteryBar(line, 256, 25, 11)) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"  %s", line);
		}

		/* Date */
		if ((value = getDateTime("%a %b %d"))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				" ^c%s^%s", COLOR_RED, value);
			free(value);
		}

		/* Time */
		if ((value = getDateTime("%I:%M %p"))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"^f-4^^c%s^%s", COLOR_WHITE, value);
			free(value);
		}

#if 0
		snprintf(status, sizeof(status),
			"^c%s^ [VOL %d%%] [CPU^f1^%s^f4^%s^f4^%s^f4^%s^f3^^c%s^] [W %d] [TEMP %d%cF] %s^c%s^ %s ",
			fg_color, vol,
			cpu_bar[0], cpu_bar[1], cpu_bar[2], cpu_bar[3],
			fg_color, wifi,
			temp, DEGREE_CHAR,
			bat0
			fg_color, datetime
		);
#endif
		setStatus(dpy, (status = buffer));

		if (argc > 1 && !strcasecmp(argv[1], "-d")) {
			printf("STATUS: %s\n", status);
		}
		sleep(1);
	}
}


