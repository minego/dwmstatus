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

#define COLOR_RED			"#e84f4f"
#define COLOR_WHITE			"#dddddd"
#define COLOR_GREEN			"#b8d68c"
#define COLOR_GREY			"#666666"
#define COLOR_ORANGE		"#e1aa5d"
#define COLOR_DGREY			"#222222"
#define DEGREE_CHAR			((char) 176)

static char *bglist[] = {
	[0] = "#222222", /* black   */
	[1] = "#b8d68c", /* green   */
	[2] = "#ffffff", /* white   */
	[3] = "#dddddd", /* white   */
	[4] = "#404040", /* black   */
	[5] = "#d23d3d", /* red     */
	[6] = "#a0cf5d", /* green   */
};

static char *fglist[] = {
	[0] = "#ffffff",
	[1] = "#000000",
	[2] = "#000000",
	[3] = "#000000",
	[4] = "#ffffff",
	[5] = "#000000",
	[6] = "#000000",
};

char	*bg = (char *) &bglist;
char	*fg = (char *) &fglist;


#define MAX_CPUS			32
#define BAR_HEIGHT			15
#define BAT0				"/sys/class/power_supply/BAT0/"
#define BAT1				"/sys/class/power_supply/BAT1/"
#define BAT_STATUS_FILE		"status"
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

static int getBattery(void)
{
	FILE		*f;
	int			energy_now;
	int			energy_full;

	if (!(f = fopen(BAT0 "charge_full", "r")) &&
		!(f = fopen(BAT1 "charge_full", "r")) &&
		!(f = fopen(BAT0 "energy_full", "r")) &&
		!(f = fopen(BAT1 "energy_full", "r"))
	) {
		return -1;
	}

	fscanf(f, "%d", &energy_full);
	fclose(f);

	if (!(f = fopen(BAT0 "charge_now", "r")) &&
		!(f = fopen(BAT1 "charge_now", "r")) &&
		!(f = fopen(BAT0 "energy_now", "r")) &&
		!(f = fopen(BAT1 "energy_now", "r"))
	) {
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

	if (!(f = fopen(BAT0 BAT_STATUS_FILE, "r")) &&
		!(f = fopen(BAT1 BAT_STATUS_FILE, "r"))
	) {
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
			strcpy(fg_color, COLOR_GREEN);
			break;

		case 'F':	/* Full			*/
			strcpy(fg_color, COLOR_WHITE);
			break;

		case 'D':	/* Discharging	*/
			if (percent < 20) {
				strcpy(fg_color, COLOR_RED);
			} else {
				strcpy(fg_color, COLOR_ORANGE);
			}
			break;

		default:
			return(-1);
	}

	r = vBar(percent, 10, BAR_HEIGHT, fg_color, COLOR_GREY, dest, size);

	/* Cover over the top bits so it looks like a battery */
	r += snprintf(dest + r, size - r, "^c" COLOR_DGREY "^^r0,0,3,2^^r7,0,3,2^");
// TODO Set to the current bg color instead of COLOR_DGREY
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
	char						*color;
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

			if (temp >= 65) {
				color = COLOR_RED;
			} else if (temp >= 50) {
				color = COLOR_ORANGE;
			} else {
				color = COLOR_WHITE;
			}

			/* Scale temp between 30 and the high limit */
			temp -= 30;
			crit -= 30;
			high -= 30;

			used += vBar((temp * 100) / crit, 2, BAR_HEIGHT,
				color, COLOR_GREY,
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
		snprintf(dest, len, "PLAYING %s BY %s",
			mpd_song_get_tag(song, MPD_TAG_TITLE, 0),
			mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));

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
	r += snprintf(dest + r, len - r, "VOL  ^f1^");
	r += vBar(volume, 6, BAR_HEIGHT, !muted ? COLOR_WHITE : COLOR_RED, COLOR_GREY, dest + r, len - r);
	r += snprintf(dest + r, len - r, "^f6^^f8^");
	return(r);
}

static void setStatus(Display *dpy, char *str)
{
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

size_t nextbg(int color, char *status, size_t size)
{
	bg = (char *) bglist[color % (sizeof(bglist) / sizeof(char *))];
	fg = (char *) fglist[color % (sizeof(fglist) / sizeof(char *))];

	return(snprintf(status, size, "^a%s^^c%s^", bg, fg));
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
			status += nextbg(1, status, sizeof(buffer) - (status - buffer));

			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);

			if (!getScriptStr("dial who", line, sizeof(line))) {
				status += snprintf(status, sizeof(buffer) - (status - buffer),
					"%s", line);
			}
		} else if (!getMPDInfo(line, sizeof(line))) {
			status += nextbg(1, status, sizeof(buffer) - (status - buffer));

			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}

		/* CPU label */
		status += nextbg(4, status, sizeof(buffer) - (status - buffer));
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			"CPU ");

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
		status += nextbg(4, status, sizeof(buffer) - (status - buffer));
		vBar((i = getMEMUsage()), 6, BAR_HEIGHT, COLOR_WHITE, COLOR_GREY, line, sizeof(line));
		status += snprintf(status, sizeof(buffer) - (status - buffer),
			"MEM %s^f6^", line);

		/* Volume */
#if 0
		if (0 < getVolumeBar(line, sizeof(line))) {
			status += nextbg(5, status, sizeof(buffer) - (status - buffer));
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}
#endif

		/* Temperature */
		if (0 < getTempBar(line, sizeof(line))) {
			status += nextbg(4, status, sizeof(buffer) - (status - buffer));
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"TEMP ");
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}

		/* Wifi */
		if (0 < getWifiBar(line, sizeof(line))) {
			status += nextbg(0, status, sizeof(buffer) - (status - buffer));
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}

		/* Battery */
		if (0 < getBatteryBar(line, sizeof(line))) {
			status += nextbg(0, status, sizeof(buffer) - (status - buffer));
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}

		/* Date */
		if (!getDateTime("%a %b %d", line, sizeof(line))) {
			status += nextbg(2, status, sizeof(buffer) - (status - buffer));
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
		}

		/* Time */
		if (!getDateTime("%I:%M %p", line, sizeof(line))) {
			status += nextbg(0, status, sizeof(buffer) - (status - buffer));
			status += snprintf(status, sizeof(buffer) - (status - buffer),
				"%s", line);
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


