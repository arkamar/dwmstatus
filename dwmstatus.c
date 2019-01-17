#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
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
#define POLL_INOTIFIER 1

static Display *dpy;
static char *kbl[] = {
	"EN",
	"CZ",
};

static char *smprintf(char *fmt, ...);
static char *getvol(void);
static unsigned int getkblayout(void);

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
	static char buf[32];
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
		sprintf(buf, "Err");
		return buf;
	}

	snd_mixer_handle_events(handle);
	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	snd_mixer_selem_get_playback_volume(elem, 0, &vol);
	snd_mixer_selem_get_playback_switch(elem, 0, &sw);

	snd_mixer_selem_id_free(s_elem);
	snd_mixer_close(handle);

	perc = 100 * (min - vol) / (min - max);
	sprintf(buf, "%d%s", perc, ((sw) ? "" : "M"));
	return buf;
}

static
char *
mktimes(const char *fmt) {
	static char buf[129];
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

	return buf;
}

void
set_status(const char *str) {
	XStoreName(dpy, DefaultRootWindow(dpy), str);
	XSync(dpy, False);
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
	struct pollfd pfd[2];
	double load_avgs[3];
	int timer_fd;
	int inotifier_fd;
	int watch_descriptor;
	char *vol = getvol();
	char *status;
	int ret;

	timer_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
	if (timer_fd == -1) {
		perror("timerfd_create");
		exit(1);
	}
	memset(&timer_value, 0, sizeof timer_value);
	timer_value.it_interval.tv_sec = 5;
	timer_value.it_value.tv_sec = 1;

	ret = timerfd_settime(timer_fd, 0, &timer_value, NULL);
	if (ret == -1) {
		perror("timerfd_settime");
		exit(1);
	}

	pfd[POLL_TIMER].fd = timer_fd;
	pfd[POLL_TIMER].events = POLLIN;

	inotifier_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotifier_fd == -1) {
		perror("inotify_init1");
		exit(1);
	}
	watch_descriptor = inotify_add_watch(inotifier_fd, "/dev/snd/controlC1", IN_CLOSE);
	if (watch_descriptor == -1) {
		perror("inotify_add_watch");
		exit(1);
	}

	pfd[POLL_INOTIFIER].fd = inotifier_fd;
	pfd[POLL_INOTIFIER].events = POLLIN;

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
				if (getloadavg(load_avgs, 3) < 0) {
					perror("getloadavg");
				}
			}
			if (pfd[POLL_INOTIFIER].revents & POLLIN) {
				vol = getvol();
				flush_fd(inotifier_fd);
			}

			status = smprintf("%s V:%s L:%.2f %.2f %.2f %s",
					kbl[getkblayout()], vol, load_avgs[0], load_avgs[1], load_avgs[2], mktimes("%a %d %b %Y %H:%M"));
			set_status(status);
			fprintf(stderr, "status: %s\n", status);
			free(status);
		}
	}

	XCloseDisplay(dpy);
	close(inotifier_fd);
	close(timer_fd);

	return 0;
}

