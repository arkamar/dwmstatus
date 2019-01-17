#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <alsa/asoundlib.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>

#define LEN(x) (sizeof x / sizeof * x)

#define POLL_TIMER  0

static Display *dpy;
static char *kbl[] = {
	"EN",
	"CZ",
};

static char *smprintf(char *fmt, ...);
static char *getvol(void);
static unsigned int getkblayout(void);
static char * mktimes(char *fmt);
static char *loadavg(void);
static void setstatus(char *str);

char *
smprintf(char *fmt, ...) {
	va_list fmtargs;
	char *ret;
	int len;

	va_start(fmtargs, fmt);
	len = vsnprintf(NULL, 0, fmt, fmtargs);
	va_end(fmtargs);

	ret = malloc(++len);
	if (ret == NULL) {
		perror("malloc");
		exit(1);
	}

	va_start(fmtargs, fmt);
	vsnprintf(ret, len, fmt, fmtargs);
	va_end(fmtargs);

	return ret;
}

char *
getvol(void) {
	long int min, max, vol;
	int sw, perc;
	snd_mixer_t * handle;
	snd_mixer_elem_t * elem;
	snd_mixer_selem_id_t * s_elem;

	snd_mixer_open(&handle, 0);
	snd_mixer_attach(handle, "default");
	snd_mixer_selem_register(handle, NULL, NULL);
	snd_mixer_load(handle);
	snd_mixer_selem_id_malloc(&s_elem);
	snd_mixer_selem_id_set_name(s_elem, "Master");

	elem = snd_mixer_find_selem(handle, s_elem);

	if (elem == NULL) {
		snd_mixer_selem_id_free(s_elem);
		snd_mixer_close(handle);
		return smprintf("Err");
	}

	snd_mixer_handle_events(handle);
	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	snd_mixer_selem_get_playback_volume(elem, 0, &vol);
	snd_mixer_selem_get_playback_switch(elem, 0, &sw);

	snd_mixer_selem_id_free(s_elem);
	snd_mixer_close(handle);

	perc = 100 * (min - vol) / (min - max);
	return smprintf("%d%s", perc, ((sw) ? "" : "M"));
}

char *
mktimes(char *fmt) {
	char buf[129];
	time_t tim;
	struct tm *timtm;

	memset(buf, 0, sizeof(buf));
	tim = time(NULL);
	timtm = localtime(&tim);
	if (timtm == NULL) {
		perror("localtime");
		exit(1);
	}

	if (!strftime(buf, sizeof(buf)-1, fmt, timtm)) {
		fprintf(stderr, "strftime == 0\n");
		exit(1);
	}

	return smprintf("%s", buf);
}

void
setstatus(char *str) {
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
}

char *
loadavg(void) {
	double avgs[3];

	if (getloadavg(avgs, 3) < 0) {
		perror("getloadavg");
		exit(1);
	}

	return smprintf("%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
}

unsigned int
getkblayout(void) {
	XkbStateRec state;
	XkbGetState(dpy, XkbUseCoreKbd, &state);
	return state.group;
}

void
flush_fd(const int fd) {
	char buf[BUFSIZ];
	ssize_t ret;

	while ((ret = read(fd, buf, sizeof buf)) > 0) {
	}
}

int
main(void) {
	struct itimerspec timer_value;
	struct pollfd pfd[1];
	int timer_fd;
	char *status;
	char *avgs;
	char *tmprg;
	char *vol;
	int ret;

	timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
	if (timer_fd == -1) {
		perror("timerfd_create");
		exit(1);
	}
	memset(&timer_value, 0, sizeof timer_value);
	timer_value.it_interval.tv_sec = 2;
	timer_value.it_value.tv_sec = 1;

	ret = timerfd_settime(timer_fd, 0, &timer_value, NULL);
	if (ret == -1) {
		perror("timerfd_settime");
		exit(1);
	}

	pfd[POLL_TIMER].fd = timer_fd;
	pfd[POLL_TIMER].events = POLLIN;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "dwmstatus: cannot open display.\n");
		return 1;
	}

	for (;;) {
		ret = poll(pfd, LEN(pfd), -1);
		switch (ret) {
		case 0:
			fprintf(stderr, "timeout\n");
			break;
		case -1:
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		default:
			if (pfd[POLL_TIMER].revents & POLLIN) {
				flush_fd(timer_fd);
			}
			avgs = loadavg();
			tmprg = mktimes("%a %d %b %Y %H:%M");
			vol = getvol();

			status = smprintf("%s V:%s L:%s %s",
					kbl[getkblayout()], vol, avgs, tmprg);
			setstatus(status);
			free(avgs);
			free(tmprg);
			free(status);
			free(vol);
		}
		fprintf(stderr, "loop\n");
	}

	XCloseDisplay(dpy);
	close(timer_fd);

	return 0;
}

