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
#include <mpd/client.h>

#define COLOR_RED			"#FF0000"
#define COLOR_WHITE			"#FFFFFF"
#define COLOR_GREY			"#666666"
#define COLOR_DGREY			"#222222"
#define DEGREE_CHAR			((char) 176)

#define MAX_CPUS			32
#define BAR_HEIGHT			15
#define BAT_NOW_FILE		"/sys/class/power_supply/BAT0/charge_now"
#define BAT_FULL_FILE		"/sys/class/power_supply/BAT0/charge_full"
#define BAT_STATUS_FILE		"/sys/class/power_supply/BAT0/status"
#define TEMP_SENSOR_FILE	"/sys/class/hwmon/hwmon1/temp1_input"

static size_t vBar(int percent, int w, int h, char *fg_color, char *bg_color, char *dest, size_t size)
{
	char		*format		= "^c%s^^r0,%d,%d,%d^^c%s^^r0,%d,%d,%d^";
	int			bar_height	= (percent * h) / 100;
	int			y			= (BAR_HEIGHT - h) / 2;

	if (percent > 100) {
		percent = 100;
	} else if (percent < 0) {
		percent = 0;
	}

	return(snprintf(dest, size, format, bg_color, y, w, h, fg_color, y + h - bar_height, w, bar_height));
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
	int			energy_full;

	if (!(f = fopen(BAT_FULL_FILE, "r"))) {
		return -1;
	}

	fscanf(f, "%d", &energy_full);
	fclose(f);

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
static char getBatteryStatus(void)
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

static int getBatteryBar(char *dest, size_t size)
{
	int		r;
	int		percent			= getBattery();
	char	fg_color[8];

	switch (getBatteryStatus()) {
		case 'C':	/* Charging		*/
		case 'F':	/* Full			*/
			strcpy(fg_color, COLOR_WHITE);
			break;

		case 'D':	/* Discharging	*/
			percentColor(fg_color, percent);
			break;

		default:
			return(-1);
	}

	r = vBar(percent, 10, 15, fg_color, COLOR_GREY, dest, size);

	/* Cover over the top bits so it looks like a battery */
	r += snprintf(dest + r, size - r, "^c" COLOR_DGREY "^^r0,0,3,2^^r7,0,3,2^");
	r += snprintf(dest + r, size - r, "^f10^");

	return(r);
}

static int getWifiPercent(void)
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

static int getWifiBar(char *dest, size_t size)
{
	float	step			= (100.0F / 15.0F);
	int		r				= 0;
	int		percent			= getWifiPercent();
	int		i;

	if (percent < 0) {
		return(-1);
	}

	r = 0;
	*dest = '\0';

	/* Show 15 bars, each on if the value is high enough. */
	r += snprintf(dest + r, size -r, "^c" COLOR_WHITE "^");
	for (i = 1; i <= 15; i++) {
		if (i * step > percent) {
			break;
		}

		r += snprintf(dest + r, size -r, "^r%d,%d,1,%d^",
			i - 1, 15 - (i - 1), i);
	}

	r += snprintf(dest + r, size -r, "^c" COLOR_GREY "^");
	for (; i <= 15; i++) {
		r += snprintf(dest + r, size -r, "^r%d,%d,1,%d^",
			i - 1, 15 - (i - 1), i);
	}

	r += snprintf(dest + r, size -r, "^f15^");

	return(r);
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

static char * getDateTime(char *format, char *dest, size_t size)
{
	time_t		result		= time(NULL);
	struct tm	*resulttm	= localtime(&result);

	if (!resulttm) {
		fprintf(stderr, "Error getting localtime.\n");
		exit(1);
	}

	if (!strftime(dest, size, format, resulttm)) {
		fprintf(stderr, "strftime is 0.\n");
		exit(1);
	}

	return(0);
}

static int getTemperature(void)
{
	int		temp;
	FILE	*f;

	if (!(f = fopen(TEMP_SENSOR_FILE, "r"))) {
		return(-1);
	}

	fscanf(f, "%d", &temp);
	fclose(f);

	temp = temp / 1000;

	/* Convert to fahrenheit */
	temp = (9.0 / 5.0) * temp + 32.0;

	return(temp);
}

static int getMPDInfo(char *dest, size_t len)
{
    struct mpd_connection	*conn;
    struct mpd_status		*status;
    struct mpd_song			*song		= NULL;
	int						r			= -1;

    if (!(conn = mpd_connection_new(NULL, 0, 30000)) ||
		mpd_connection_get_error(conn)
	) {
		return(-1);
    }

    mpd_command_list_begin(conn, true);
    mpd_send_status(conn);
    mpd_send_current_song(conn);
    mpd_command_list_end(conn);

	status = mpd_recv_status(conn);
	if ((status) && (mpd_status_get_state(status) == MPD_STATE_PLAY)) {
		mpd_response_next(conn);

		song = mpd_recv_song(conn);
		snprintf(dest, len, " ^c%s^PLAYING^c%s^%s^f-2^^c%s^BY ^c%s^%s ",
			COLOR_RED, COLOR_WHITE, mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
			COLOR_RED, COLOR_WHITE, mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));

		mpd_song_free(song);

		r = 0;
	}

	mpd_response_finish(conn);
	mpd_connection_free(conn);

	return(r);
}

static int getVolumeBar(char *dest, size_t len)
{
	int		r;
	int		volume	= 0;
	int		muted	= 0;

	if (getScriptStr("volume.sh", dest, len)) {
		*dest = '\0';
		return(-1);
	}
	volume = atoi(dest);

	if (strchr(dest, 'M')) {
		muted = 1;
	}

	r = 0;
	r += snprintf(dest + r, len - r, "  ^c%s^VOL ^f1^", COLOR_RED);
	r += vBar(volume, 6, BAR_HEIGHT, !muted ? COLOR_WHITE : COLOR_RED, COLOR_GREY, dest + r, len - r);
	r += snprintf(dest + r, len - r, "^f6^^c%s^^f8^", COLOR_WHITE);
	return(r);
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
	char		*status;
	char		line[2 * 1024];
	char		buffer[4 * 1024];

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "Cannot open display.\n");
		return(EXIT_FAILURE);
	}

	for (;;) {
		status = buffer;
		*status = '\0';

		/* Phone call or music */
		if (!getScriptStr("dial what", line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"  ^c%s^%s", COLOR_RED, line);

			if (!getScriptStr("dial who", line, sizeof(line))) {
				status += snprintf(status, sizeof(buffer) - (status - buffer),
					"^c%s^%s", COLOR_WHITE, line);
			}
		} else if (!getMPDInfo(line, sizeof(line))) {
				status += snprintf(status, sizeof(buffer) - (status - buffer),
					"%s", line);
		}

		/* CPU label */
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			" ^c%s^CPU ^c%s^^f1^", COLOR_RED, COLOR_WHITE);

		/* For each CPU (0 is average of all) */
		count = getCPUUsage(cpuper);
		for (i = 1; i < count; i++) {
			if (-1 == cpuper[i]) {
				break;
			}

			vBar(cpuper[i], 4, BAR_HEIGHT, COLOR_WHITE, COLOR_GREY, line, sizeof(line));
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s^f5^", line);
		}

		/* MEM usage */
		vBar((i = getMEMUsage()), 6, BAR_HEIGHT, COLOR_WHITE, COLOR_GREY, line, sizeof(line));
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			"  ^c%s^MEM ^f1^%s^f6^", COLOR_RED, line);

		/* Volume */
		if (0 < getVolumeBar(line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}

		/* Temp */
		if (0 < (i = getTemperature())) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				" ^c%s^TEMP ^f1^^c%s^%d%cF^f4^ ", COLOR_RED, COLOR_WHITE, i, DEGREE_CHAR);
		}

		/* Wifi */
		if (0 < getWifiBar(line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				" %s", line);
		}

		/* Battery */
		if (0 < getBatteryBar(line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"  %s", line);
		}

		/* Date */
		if (!getDateTime("%a %b %d", line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"  ^c%s^%s", COLOR_RED, line);
		}

		/* Time */
		if (!getDateTime("%I:%M %p", line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"  ^c%s^%s", COLOR_WHITE, line);
		}

		setStatus(dpy, (status = buffer));

		if (argc > 1 && !strcasecmp(argv[1], "-d")) {
			printf("STATUS: %s\n", status);
		}
		sleep(1);
	}
}


