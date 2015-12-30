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
#include <sensors/sensors.h>

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
#define TEMP_SENSOR_F		"/sys/class/hwmon/hwmon%d/temp%d_"

static size_t vBar(int percent, int w, int h, char *fg_color, char *bg_color, char *dest, size_t size)
{
	char		*format		= "^c%s^^r0,%d,%d,%d^";
	int			bar_height	= (percent * h) / 100;
	int			y			= (BAR_HEIGHT - h) / 2;
	size_t		used		= 0;

	if (percent > 100) {
		percent = 100;
	} else if (percent < 0) {
		percent = 0;
	}

	if (bg_color) {
		used += snprintf(dest + used, size - used, format,
					bg_color, y, w, h);
	}
	if (fg_color) {
		used += snprintf(dest + used, size - used, format,
					fg_color, y + h - bar_height, w, bar_height);
	}

	return(used);
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

	r = vBar(percent, 10, BAR_HEIGHT, fg_color, COLOR_GREY, dest, size);

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
	int			i, x;
	long int	busy_time, wait_time, total_time;
	char		cpu_name[32];
	static int	new_cpu_usage[MAX_CPUS][7];
	static int	old_cpu_usage[MAX_CPUS][7];

	if (!(f = fopen("/proc/stat", "r"))) {
		return(-1);
	}

	for (i = 0; i < MAX_CPUS && !feof(f); ++i) {
		line = NULL;
		getline(&line, &len, f);

		if (!line) {
			break;
		}

		if (8 != sscanf(line, "%s %d %d %d %d %d %d %d",
				cpu_name,
				&new_cpu_usage[i][0], // user
				&new_cpu_usage[i][1], // nice
				&new_cpu_usage[i][2], // system
				&new_cpu_usage[i][3], // idle
				&new_cpu_usage[i][4], // iowait
				&new_cpu_usage[i][5], // irq
				&new_cpu_usage[i][6]) // softirq
		) {
			free(line);
			continue;
		}
		free(line);

		if (strncasecmp(cpu_name, "cpu", 3)) {
			i--;
			continue;
		}
		busy_time	= 0;
		wait_time	= 0;
		total_time	= 0;
		for (x = 0; x < 7; x++) {
			switch (x) {
				case 3: // idle
					break;

				case 4: // iowait
					wait_time += new_cpu_usage[i][x] - old_cpu_usage[i][x];
					break;

				default:
					busy_time += new_cpu_usage[i][x] - old_cpu_usage[i][x];
					break;
			}
			total_time += new_cpu_usage[i][x] - old_cpu_usage[i][x];
			old_cpu_usage[i][x] = new_cpu_usage[i][x];
		}

		if (total_time) {
			cpuper[(i * 2) + 0] = (busy_time * 100) / total_time;
			cpuper[(i * 2) + 1] = (wait_time * 100) / total_time;
		} else {
			cpuper[(i * 2) + 0] = 0;
			cpuper[(i * 2) + 1] = 0;
		}

		count++;
	}
	cpuper[(i * 2) + 0] = -1;
	cpuper[(i * 2) + 1] = -1;

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

static int getTempBar(char *dest, size_t len)
{
	int							used	= 0;
	int							temp;
	int							high;
	int							crit;
	int							cx, fx;
	const sensors_chip_name		*chip;
	const sensors_feature		*feature;
	const sensors_subfeature	*sub;
	char						*label;
	double						value;

	*dest = '\0';

	cx = 0;
	while ((chip = sensors_get_detected_chips(NULL, &cx))) {
		fx = 0;
		while ((feature = sensors_get_features(chip, &fx))) {
			/* We're only interested in temperatures */
			if (SENSORS_FEATURE_TEMP != feature->type) {
				continue;
			}

			/* We're only interested in CPU cores (for now) */
			if (!(label = sensors_get_label(chip, feature)) ||
				!strstr(label, "Core")
			) {
				continue;
			}

			if ((sub = sensors_get_subfeature(chip, feature, SENSORS_SUBFEATURE_TEMP_INPUT))) {
				sensors_get_value(chip, sub->number, &value);
				temp = (int) value;
			} else {
				continue;
			}

			if ((sub = sensors_get_subfeature(chip, feature, SENSORS_SUBFEATURE_TEMP_MAX))) {
				sensors_get_value(chip, sub->number, &value);
				high = (int) value;
			} else {
				high = 80;
			}

			if ((sub = sensors_get_subfeature(chip, feature, SENSORS_SUBFEATURE_TEMP_CRIT))) {
				sensors_get_value(chip, sub->number, &value);
				crit = (int) value;
			} else {
				crit = 90;
			}

			used += vBar((temp * 100) / crit, 2, BAR_HEIGHT,
				temp < high ? COLOR_WHITE : COLOR_RED, COLOR_GREY,
				dest + used, len - used);

			used += snprintf(dest + used, len - used, "^f3^");
		}
	}

	return(used);
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
		snprintf(dest, len, " ^c%s^PLAYING ^c%s^%s^f-2^^c%s^ BY ^c%s^%s ",
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
	r += snprintf(dest + r, len - r, "  ^c%s^VOL  ^f1^", COLOR_RED);
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
	size_t		curwidth, lastwidth = 0, padding, used;
	int			cpuper[MAX_CPUS * 2];
	char		*status;
	char		line[2 * 1024];
	char		buffer[4 * 1024];

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "Cannot open display.\n");
		return(EXIT_FAILURE);
	}

	sensors_init(NULL);

	for (;;) {
		status = buffer;
		*status = '\0';

		/* Phone call or music */
		if (!getScriptStr("dial what", line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"  ^c%s^%s", COLOR_RED, line);

			if (!getScriptStr("dial who", line, sizeof(line))) {
				status += snprintf(status, sizeof(buffer) - (status - buffer),
					"  ^c%s^%s", COLOR_WHITE, line);
			}
		} else if (!getMPDInfo(line, sizeof(line))) {
				status += snprintf(status, sizeof(buffer) - (status - buffer),
					"%s", line);
		}

		/* CPU label */
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			" ^c%s^CPU  ^c%s^^f1^", COLOR_RED, COLOR_WHITE);

		/*
			For each CPU (0 is average of all)

			Each CPU has 2 values. The busy percentage and the iowait percentage
		*/
		count = getCPUUsage(cpuper);
		for (i = 1; i < count; i++) {
			if (-1 == cpuper[i * 2]) {
				break;
			}
			used = 0;

			/* Draw iowait + busy first */
			used += vBar(cpuper[(i * 2)] + cpuper[(i * 2) + 1], 4, BAR_HEIGHT,
							COLOR_RED, COLOR_GREY,
							line + used, sizeof(line) - used);

			/* Draw just busy on top */
			used += vBar(cpuper[i * 2], 4, BAR_HEIGHT,
							COLOR_WHITE, NULL,
							line + used, sizeof(line) - used);

			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s^f5^", line);
		}

		/* MEM usage */
		vBar((i = getMEMUsage()), 6, BAR_HEIGHT, COLOR_WHITE, COLOR_GREY, line, sizeof(line));
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			"  ^c%s^MEM  ^f1^%s^f6^", COLOR_RED, line);

		/* Volume */
		if (0 < getVolumeBar(line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}

		/* Temperature */
		if (0 < getTempBar(line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				" ^c%s^TEMP  ", COLOR_RED);
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s^f3^", line);
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
				"   ^c%s^%s", COLOR_RED, line);
		}

		/* Time */
		if (!getDateTime("%I:%M %p", line, sizeof(line))) {
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"   ^c%s^%s", COLOR_WHITE, line);
		}

		curwidth = status - buffer;
		if (lastwidth > curwidth) {
			/* Pad with spaces if the last status was longer */
			padding = lastwidth - curwidth;

			if (curwidth + padding + 1 > sizeof(buffer)) {
				padding = sizeof(buffer) - (curwidth + 1);
			}

			memmove(buffer + padding, buffer, curwidth + 1);
			memset(buffer, ' ', padding);
		}

		setStatus(dpy, (status = buffer));
		lastwidth = curwidth;


		if (argc > 1 && !strcasecmp(argv[1], "-d")) {
			printf("STATUS: %s\n", status);
		}
		sleep(1);
	}
}


