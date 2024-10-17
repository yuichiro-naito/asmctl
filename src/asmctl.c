/*-
 * Copyright (c) 2015 Yuichiro NAITO <naito.yuichiro@gmail.com>
 * Copyright (c) 2024 Joshua Rogers <joshua@joshua.hu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Command line tool for Apple's System Management Console (SMC).
 *
 * This command may require the following sysctl variables:
 *
 *  hw.acpi.video.lcd0.*	(acpi_video(4))
 *  hw.asmc.0.light.control	(asmc(4))
 *  hw.acpi.acline		(acpi(4))
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/backlight.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if (defined(HAVE_SYS_CAPSICUM_H) && (HAVE_LIBCASPER_H))
#define WITH_CASPER 1 // WITH_CASPER is needed since 12.0R
#define USE_CAPSICUM 1
#include <libcasper.h>
#include <sys/capsicum.h>
#include <sys/nv.h>
#include <casper/cap_sysctl.h>
#ifdef HAVE_CAPSICUM_HELPERS_H
#include "capsicum_helpers.h"
#else
#include <nl_types.h>
#endif
#endif

#include "asmctl.h"

#define KB_CUR_LEVEL "dev.asmc.0.light.control"

#define ACPI_VIDEO_LEVELS "hw.acpi.video.lcd0.levels"

#define ACPI_VIDEO_ECO_LEVEL "hw.acpi.video.lcd0.economy"
#define ACPI_VIDEO_FUL_LEVEL "hw.acpi.video.lcd0.fullpower"
#define ACPI_VIDEO_CUR_LEVEL "hw.acpi.video.lcd0.brightness"

#define BACKLIGHT_ECO_LEVEL "backlight_economy_level"
#define BACKLIGHT_FUL_LEVEL "backlight_full_level"
#define BACKLIGHT_CUR_LEVEL "backlight_current_level"

#define AC_POWER "hw.acpi.acline"

/* Keyboard backlight current level (0-100) */
int kb_current_level = -1;

/* Number of video levels (either backlight(9) or acpi_video) */
int num_of_video_levels = 0;

/* Array of video level values (either backlight(9) or acpi_video) */
int *video_levels = NULL;

/* hw.acpi.video backlight current level (one of video_levels) */
int acpi_video_current_level;

/* hw.acpi.video  backlight economy level (one of video_levels) */
int acpi_video_economy_level = -1;

/* hw.acpi.video backlight fullpower level (one of video_levels) */
int acpi_video_fullpower_level = -1;

/* Backlight(9) current level (one of video_levels) */
int backlight_current_level;

/* Backlight(9) economy level (one of video_levels) */
int backlight_economy_level = -1;

/* Backlight(9) fullpower level level (one of video_levels) */
int backlight_fullpower_level = -1;

/* set 1 if AC powered else 0 */
int ac_powered = 0;

/* set 1 if backlight(9) instead of hw.acpi.video */
int use_backlight = 0;

/* file name of default backlight device */
static char *backlight_device = "/dev/backlight/backlight0";

/* file name to save state */
static char *conf_filename = "/var/lib/asmctl.conf";

/* file descriptor for the state file */
int conf_fd;

/* file descriptor for the default backlight device */
int backlight_fd;

/* struct containing backlight(9) properties */
struct backlight_props props;

#ifdef USE_CAPSICUM
cap_channel_t *ch_sysctl;
#define sysctlbyname(A, B, C, D, E)                                            \
	cap_sysctlbyname(ch_sysctl, (A), (B), (C), (D), (E))
#endif


struct asmc_driver *asmc_drivers[] = {
	&backlight_driver, &acpi_video_driver, &acpi_keyboard_driver
};

struct asmc_driver_context video_ctx, keyboard_ctx;

static int
lookup_driver(enum CATEGORY cat, struct asmc_driver **drv, void **ctx)
{
	struct asmc_driver *ad, **p;
	void *c;

	ARRAY_FOREACH(p, asmc_drivers) {
		ad = *p;
		if (ad->category == cat) {
			if ((c = calloc(1, ad->ctx_size)) == NULL)
				return -1;
			if (ad->init(c) < 0) {
				free(c);
				continue;
			}
			*drv = ad;
			*ctx = c;
			return 0;
		}
	}
	return -1;
}

int
init_driver_context()
{
	if (lookup_driver(KEYBOARD, &keyboard_ctx.driver,
			  &keyboard_ctx.context) < 0)
		return -1;
	if (lookup_driver(VIDEO, &video_ctx.driver, &video_ctx.context) < 0) {
		ASMC_CLEANUP(&keyboard_ctx);
		return -1;
	}
	return 0;
}


/**
   Store backlight levels to file.
   Write in sysctl.conf(5) format to restore by sysctl(1)
 */
int store_conf_file() {
	int rc;
	FILE *fp;
	nvlist_t *nl;
	const char *name;
	int type;
	void *cookie;

	if ((nl = nvlist_create(0)) == NULL) {
		fprintf(stderr, "nvlist_create: %s\n", strerror(errno));
		return -1;
	}
	ASMC_SAVE(&keyboard_ctx, nl);
	ASMC_SAVE(&video_ctx, nl);

	rc = ftruncate(conf_fd, 0);
	if (rc < 0) {
		fprintf(stderr, "ftruncate: %s\n", strerror(errno));
		nvlist_destroy(nl);
		return rc;
	}

	rc = lseek(conf_fd, 0, SEEK_SET);
	if (rc < 0) {
		fprintf(stderr, "lseek: %s\n", strerror(errno));
		nvlist_destroy(nl);
		return rc;
	}

	fp = fdopen(conf_fd, "w");
	if (fp == NULL) {
		fprintf(stderr, "can not write %s\n", conf_filename);
		nvlist_destroy(nl);
		return -1;
	}
	fprintf(fp, "# DO NOT EDIT MANUALLY!\n"
		    "# This file is written by asmctl.\n");
	cookie = NULL;
	while ((name = nvlist_next(nl, &type, &cookie)) != NULL) {
		if (type != NV_TYPE_NUMBER)
			continue;
		fprintf(fp, "%s=%d\n", name, (int)nvlist_get_number(nl, name));
	}
	nvlist_destroy(nl);

#if 0
	fprintf(fp, "%s=%d\n", ACPI_VIDEO_ECO_LEVEL, acpi_video_economy_level);
	fprintf(fp, "%s=%d\n", ACPI_VIDEO_FUL_LEVEL,
		acpi_video_fullpower_level);
	fprintf(fp, "%s=%d\n", ACPI_VIDEO_CUR_LEVEL, acpi_video_current_level);
	fprintf(fp, "%s=%d\n", BACKLIGHT_ECO_LEVEL, backlight_economy_level);
	fprintf(fp, "%s=%d\n", BACKLIGHT_FUL_LEVEL, backlight_fullpower_level);
	fprintf(fp, "%s=%d\n", BACKLIGHT_CUR_LEVEL, backlight_current_level);
	fprintf(fp, "%s=%d\n", KB_CUR_LEVEL, kb_current_level);
#endif

	rc = fdclose(fp, NULL);
	if (rc < 0) {
		fprintf(stderr, "can not write %s\n", conf_filename);
		return rc;
	}

	return rc;
}

int set_keyboard_backlight_level(int val) {
	int rc;
	char buf[sizeof(int)];

	memcpy(buf, &val, sizeof(int));

	rc = sysctlbyname(KB_CUR_LEVEL, NULL, NULL, buf, sizeof(int));
	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", KB_CUR_LEVEL,
			strerror(errno));
		return rc;
	}

	printf("set keyboard backlight brightness: %d\n", val);

	kb_current_level = val;

	return store_conf_file();
}

int get_keyboard_backlight_level() {
	int rc;
	char buf[sizeof(int)];
	size_t buflen = sizeof(int);

	rc = sysctlbyname(KB_CUR_LEVEL, buf, &buflen, NULL, 0);
	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", KB_CUR_LEVEL,
			strerror(errno));
		return rc;
	}

	rc = *((int *)buf);
	kb_current_level = rc;

	return rc;
}

int get_acpi_video_level() {
	int rc;
	char buf[sizeof(int)];
	size_t buflen = sizeof(int);

	rc = sysctlbyname(ACPI_VIDEO_CUR_LEVEL, buf, &buflen, NULL, 0);

	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", ACPI_VIDEO_CUR_LEVEL,
			strerror(errno));
		return rc;
	}

	return *((int *)buf);
}

int set_acpi_video_level(int val) {
	char *key;
	int rc;
	char buf[sizeof(int)];

	if (val < 0 || val > 100)
		return -1;

	memcpy(buf, &val, sizeof(int));

	rc = sysctlbyname(ACPI_VIDEO_CUR_LEVEL, NULL, NULL, buf, sizeof(int));
	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", ACPI_VIDEO_CUR_LEVEL,
			strerror(errno));
		return rc;
	}

	printf("set video brightness: %d\n", val);

	key = (ac_powered) ? ACPI_VIDEO_FUL_LEVEL : ACPI_VIDEO_ECO_LEVEL;

	rc = sysctlbyname(key, NULL, NULL, buf, sizeof(int));
	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", key, strerror(errno));
		return rc;
	}

	acpi_video_current_level = val;

	if (ac_powered)
		acpi_video_fullpower_level = val;
	else
		acpi_video_economy_level = val;

	return store_conf_file();
}

int set_backlight_video_level(int val) {
	int rc;

	if (val < 0 || val > 100)
		return -1;

	props.brightness = val;

	rc = ioctl(backlight_fd, BACKLIGHTUPDATESTATUS, &props);
	if (rc < 0) {
		fprintf(stderr, "ioctl BACKLIGHTUPDATESTATUS : %s\n",
			strerror(errno));
		return rc;
	}

	printf("set backlight brightness: %d\n", val);

	backlight_current_level = val;

	if (ac_powered)
		backlight_fullpower_level = val;
	else
		backlight_economy_level = val;

	return store_conf_file();
}

int set_lcd_brightness(int val) {
	int rc;

	rc = use_backlight ? set_backlight_video_level(val)
			   : set_acpi_video_level(val);

	return rc;
}

int get_video_up_level() {
	int v =
	    use_backlight ? backlight_current_level : acpi_video_current_level;
	int i;

	/* A bug(?) exists on some screens that make it impossible to raise the
	   backlight properly:

	   $ backlight 80
	   $ backlight
	   brightness: 79
	   $ backlight 75
	   $ backlight
	   brightness: 74

	   This line therefore raises the backlight by two
	*/
	if (use_backlight && props.nlevels == 0 && v < 100) {
		v++;
	}

	for (i = 0; i < num_of_video_levels; i++) {
		if (video_levels[i] == v) {
			if (i == num_of_video_levels - 1)
				return video_levels[i];
			else
				return video_levels[i + 1];
		}
	}
	return -1;
}

int get_video_down_level() {
	int v =
	    use_backlight ? backlight_current_level : acpi_video_current_level;
	int i;

	/* A bug(?) exists on some screens that make it impossible to decrease
	   the backlight properly.

	   For example, the only way to decrease from 80 to 79 is by
	   either first increasing to 81 and setting backlight=80, or
	   decreasing to 79 and setting backlight=80:

	   $ backlight
	   brightness: 80
	   $ backlight 79
	   $ backlight
	   brightness: 78
	   $ backlight
	   brightness: 79

	   Attempting to set backlight=80 (to actually set backlight=79)
	   fails, as setting the current value of backlight does not
	   change its value:

	   $ backlight
	   brightness: 80
	   $ backlight 80
	   $ backlight
	   brightness: 80
	   $ backlight 79
	   $ backlight
	   brightness: 78

	   Therefore, this function, if using backlight(9), will decrease at a
	   minimum level of 2.

	   The only way to set the backlight to truly dim is by setting
	   backlight=0 when the backlight=1 is not previously set. The follow
	   line assures that if backlight=2, it does not set backlight=1 but
	   instead backlight=0.
	*/

	if (use_backlight && props.nlevels == 0 && v == 2) {
		v--;
	}

	for (i = num_of_video_levels - 1; i >= 0; i--) {
		if (video_levels[i] == v) {
			if (i == 0)
				return video_levels[0];
			else
				return video_levels[i - 1];
		}
	}
	return -1;
}

int get_saved_levels() {
	FILE *fp;
	char buf[128];
	char name[80];
	char *p;
	int value, len;
	int rc;
	nvlist_t *nl;

	rc = lseek(conf_fd, 0, SEEK_SET);

	if (rc < 0) {
		fprintf(stderr, "lseek: %s\n", strerror(errno));
		return rc;
	}

	fp = fdopen(conf_fd, "r");
	if (fp == NULL) {
		fprintf(stderr, "can not read %s\n", conf_filename);
		return -1;
	}

	if ((nl = nvlist_create(0)) == NULL) {
		fprintf(stderr, "nvlist_create: %s\n", conf_filename);
		fdclose(fp, NULL);
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (buf[0] == '#')
			continue;
		p = strchr(buf, '=');
		if (p == NULL)
			continue;
		len = p - buf;
		len = (len < sizeof(name) - 1) ? len : sizeof(name) - 1;
		strncpy(name, buf, len);
		name[len] = '\0';
		value = strtol(p + 1, &p, 10);
		if (*p != '\n')
			continue;
		nvlist_add_number(nl, name, value);

		if (strcmp(name, ACPI_VIDEO_ECO_LEVEL) == 0) {
			acpi_video_economy_level = value;
		} else if (strcmp(name, ACPI_VIDEO_FUL_LEVEL) == 0) {
			acpi_video_fullpower_level = value;
		} else if (strcmp(name, ACPI_VIDEO_CUR_LEVEL) == 0) {
			acpi_video_current_level = value;
		} else if (strcmp(name, BACKLIGHT_ECO_LEVEL) == 0) {
			backlight_economy_level = value;
		} else if (strcmp(name, BACKLIGHT_FUL_LEVEL) == 0) {
			backlight_fullpower_level = value;
		} else if (strcmp(name, BACKLIGHT_CUR_LEVEL) == 0) {
			backlight_current_level = value;
		} else if (strcmp(name, KB_CUR_LEVEL) == 0) {
			kb_current_level = value;
		}
	}
	ASMC_LOAD(&keyboard_ctx, nl);
	ASMC_LOAD(&video_ctx, nl);
	nvlist_destroy(nl);
	rc = fdclose(fp, NULL);
	if (rc < 0) {
		fprintf(stderr, "can not write %s\n", conf_filename);
		return rc;
	}

	return 0;
}

int get_ac_powered() {
	int rc;
	char buf[128];
	size_t buflen = sizeof(buf);

	rc = sysctlbyname(AC_POWER, buf, &buflen, NULL, 0);
	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", AC_POWER, strerror(errno));
		return rc;
	}

	ac_powered = *((int *)buf);

	return rc;
}

int get_acpi_video_levels() {
	int *buf, rc, *v, n;
	size_t buflen = -1;

	rc = sysctlbyname(ACPI_VIDEO_LEVELS, NULL, &buflen, NULL, 0);
	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", ACPI_VIDEO_LEVELS,
			strerror(errno));
		return rc;
	}

	if (buflen < 1) {
		fprintf(stderr, "failed to retrieve %s length: %zu\n",
			ACPI_VIDEO_LEVELS, buflen);
		return -1;
	}

	buf = (int *)malloc(buflen);
	if (buf == NULL) {
		fprintf(stderr, "failed to allocate %lu bytes memory\n",
			buflen);
		return -1;
	}

	rc = sysctlbyname(ACPI_VIDEO_LEVELS, (void *)buf, &buflen, NULL, 0);
	if (rc < 0) {
		fprintf(stderr, "sysctl %s : %s\n", ACPI_VIDEO_LEVELS,
			strerror(errno));
		free(buf);
		return rc;
	}

	n = buflen / sizeof(int);
	if (n < 3) {
		fprintf(stderr, "fewer than 3 video levels retrieved\n");
		free(buf);
		return -1;
	}

	/* if conf_file is empty or not created,
	   use default value */
	if (acpi_video_fullpower_level < 0) {
		 acpi_video_fullpower_level = (int)buf[0];
	}
	if (acpi_video_economy_level < 0) {
		acpi_video_economy_level = (int)buf[1];
	}

	/* ignore first two elements for range */
	n -= 2;
	v = (int *)realloc(video_levels, n * sizeof(int));
	if (v == NULL) {
		fprintf(stderr, "failed to allocate %zu bytes memory\n",
			n * sizeof(int));
		free(buf);
		return -1;
	}

	memcpy(v, &buf[2], n * sizeof(int));

	num_of_video_levels = n;
	video_levels = v;

	free(buf);

	return rc;
}

int get_backlight_video_levels() {
	int i;
	int rc;

	if (backlight_fd < 0) {
		return -1;
	}

	use_backlight = 1;

	rc = ioctl(backlight_fd, BACKLIGHTGETSTATUS, &props);
	if (rc < 0) {
		fprintf(stderr, "ioctl BACKLIGHTGETSTATUS : %s\n",
			strerror(errno));
		return -1;
	}

	if (props.nlevels != 0) {
		num_of_video_levels = props.nlevels; // XXX: +1 for level=0?
	} else {
		num_of_video_levels = BACKLIGHTMAXLEVELS + 1; // 0-100 inclusive
	}

	video_levels =
	    (int *)realloc(video_levels, num_of_video_levels * sizeof(int));
	if (video_levels == NULL) {
		fprintf(stderr, "failed to allocate %zu bytes memory\n",
			num_of_video_levels * sizeof(int));
		return -1;
	}

	if (props.nlevels != 0) {
		for (i = 0; i < props.nlevels; i++) {
			video_levels[i] = props.levels[i];
		}
	} else {
		for (i = 0; i < num_of_video_levels; i++) {
			video_levels[i] = i;
		}
	}

	if (backlight_economy_level < 0) {
		backlight_economy_level = 60; // arbitrary value
	}
	if (backlight_fullpower_level < 0) {
		backlight_fullpower_level = 100; // arbitrary value
	}

	return rc;
}

int get_video_levels() {
	return (get_backlight_video_levels() < 0 &&
		get_acpi_video_levels() < 0) ? -1 : 0;
}

int compare_video_levels(const void *a, const void *b) {
	return (*(int*)a - *(int*)b);
}

void sort_video_levels() {
	qsort(video_levels, num_of_video_levels, sizeof(int), compare_video_levels);
}

#ifdef USE_CAPSICUM
int init_capsicum(struct asmc_driver_context *c) {
	int rc;
#ifdef HAVE_CAP_SYSCTL_LIMIT_NAME
	void *limits;
#else
	nvlist_t *limits;
#endif
	cap_channel_t *ch_casper;
	cap_rights_t conf_fd_rights;
	cap_rights_t backlight_fd_rights;

	static const unsigned long backlightcmds[] = {BACKLIGHTUPDATESTATUS,
						      BACKLIGHTGETSTATUS};

#ifdef HAVE_CAPSICUM_HELPERS_H
	caph_cache_catpages();
#else
	catopen("libc", NL_CAT_LOCALE);
#endif

	/* Open a channel to casperd */
	ch_casper = cap_init();
	if (ch_casper == NULL) {
		fprintf(stderr, "cap_init() failed\n");
		return -1;
	}

	/* Enter capability mode */
	rc = cap_enter();
	if (rc < 0) {
		fprintf(stderr, "capability is not supported\n");
		cap_close(ch_casper);
		return rc;
	}

	/* limit conf_fd to read/write/seek/fcntl/ftruncate */
	/* fcntl is used in fdopen(3) */
	cap_rights_init(&conf_fd_rights, CAP_READ | CAP_WRITE | CAP_SEEK |
					     CAP_FCNTL | CAP_FTRUNCATE);
	rc = cap_rights_limit(conf_fd, &conf_fd_rights);
	if (rc < 0) {
		fprintf(stderr, "cap_rights_limit() failed\n");
		cap_close(ch_casper);
		return rc;
	}

	if (backlight_fd >= 0) {
		/* limit backlight_fd to ioctl */
		cap_rights_init(&backlight_fd_rights, CAP_IOCTL);
		rc = cap_rights_limit(backlight_fd, &backlight_fd_rights);
		if (rc < 0) {
			fprintf(stderr, "cap_rights_limit() failed\n");
			cap_close(ch_casper);
			return rc;
		}

		/* limit allowed backlight_fd ioctl commands */
		rc = cap_ioctls_limit(backlight_fd, backlightcmds,
				      nitems(backlightcmds));
		if (rc < 0) {
			fprintf(stderr, "cap_ioctls_limit() failed\n");
			cap_close(ch_casper);
			return rc;
		}
	}

	/* open channel to casper sysctl */
	ch_sysctl = cap_service_open(ch_casper, "system.sysctl");
	if (ch_sysctl == NULL) {
		fprintf(stderr, "cap_service_open(\"system.sysctl\") failed\n");
		cap_close(ch_casper);
		return rc;
	}

#ifdef HAVE_CAP_SYSCTL_LIMIT_NAME
	limits = cap_sysctl_limit_init(ch_sysctl);
	cap_sysctl_limit_name(limits, ACPI_VIDEO_LEVELS, CAP_SYSCTL_READ);
	cap_sysctl_limit_name(limits, ACPI_VIDEO_ECO_LEVEL, CAP_SYSCTL_RDWR);
	cap_sysctl_limit_name(limits, ACPI_VIDEO_FUL_LEVEL, CAP_SYSCTL_RDWR);
	cap_sysctl_limit_name(limits, ACPI_VIDEO_CUR_LEVEL, CAP_SYSCTL_RDWR);
	cap_sysctl_limit_name(limits, KB_CUR_LEVEL, CAP_SYSCTL_RDWR);
	cap_sysctl_limit_name(limits, AC_POWER, CAP_SYSCTL_READ);

	rc = cap_sysctl_limit(limits);
	if (rc < 0) {
		fprintf(stderr, "cap_sysctl_limit failed %s\n",
			strerror(errno));
		cap_close(ch_casper);
		cap_close(ch_sysctl);
		return rc;
	}

	ASMC_SET_RIGHTS(c, ch_sysctl, limits);

#else
	/* limit sysctl names as following */
	limits = nvlist_create(0);
	nvlist_add_number(limits, ACPI_VIDEO_LEVELS, CAP_SYSCTL_READ);
	nvlist_add_number(limits, ACPI_VIDEO_ECO_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, ACPI_VIDEO_FUL_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, ACPI_VIDEO_CUR_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, KB_CUR_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, AC_POWER, CAP_SYSCTL_READ);

	rc = cap_limit_set(ch_sysctl, limits);
	if (rc < 0) {
		fprintf(stderr, "cap_limit_set failed %s\n", strerror(errno));
		nvlist_destroy(limits);
		cap_close(ch_casper);
		cap_close(ch_sysctl);
		return rc;
	}

	nvlist_destroy(limits);
#endif

	/* close connection to casper */
	cap_close(ch_casper);

	return 0;
}
#endif

void usage(const char *prog) {
	printf("usage: %s [video|key] [up|down]\n", prog);
	printf("\nChange video or keyboard backlight more or less bright.\n");
}

void cleanup()
{
	ASMC_CLEANUP(&keyboard_ctx);
	ASMC_CLEANUP(&video_ctx);
	close(conf_fd);
}

int main(int argc, char *argv[]) {
	int d;
	int rc = 0;
	struct asmc_driver_context *ctx;

	if (argc <= 2) {
		usage(argv[0]);
		return 1;
	}

	if (init_driver_context() < 0)
		return 1;

	if ((strcmp(argv[1], "video") == 0 || strcmp(argv[1], "lcd") == 0)) {
		ctx = &video_ctx;
	} else if (strcmp(argv[1], "kb") == 0 || strcmp(argv[1], "kbd") == 0 ||
		 strcmp(argv[1], "keyboard") == 0 || strcmp(argv[1], "key") == 0)
		ctx = &keyboard_ctx;
	else {
		usage(argv[0]);
		cleanup();
		return 1;
	}

	conf_fd = open(conf_filename, O_CREAT | O_RDWR, 0600);
	if (conf_fd < 0) {
		fprintf(stderr, "can not open %s\n", conf_filename);
		return 1;
	}

#ifdef USE_CAPSICUM
	if (init_capsicum(ctx) < 0) {
		cleanup();
		return 1;
	}
#endif

	/* initialize */
	if (get_ac_powered() < 0 || get_saved_levels() < 0) {
		cleanup();
		return 1;
	}

	if (strcmp(argv[2], "acpi") == 0 || strcmp(argv[2], "a") == 0) {
		ASMC_ACPI(ctx, ac_powered);
	} else if (strcmp(argv[2], "up") == 0 ||
		   strcmp(argv[2], "u") == 0) {
		ASMC_UP(ctx);
	} else if (strcmp(argv[2], "down") == 0 ||
		   strcmp(argv[2], "d") == 0) {
		ASMC_DOWN(ctx);
	}

	store_conf_file();

	cleanup();


	if (argc > 2) {
		if ((strcmp(argv[1], "video") == 0 ||
		     strcmp(argv[1], "lcd") == 0)) {
			if (get_video_levels() < 0) {
				fprintf(stderr, "failed to get video levels\n");
				cleanup();
				return 1;
			}

			sort_video_levels();

			/* acpi: use the previously saved brightness */
			if (strcmp(argv[2], "acpi") == 0 ||
			    strcmp(argv[2], "a") == 0) {
				if (use_backlight == 0) {
					d = ac_powered
						? acpi_video_fullpower_level
						: acpi_video_economy_level;
				} else {
					d = ac_powered
						? backlight_fullpower_level
						: backlight_economy_level;
				}
				rc = set_lcd_brightness(d);
			} else {
				/* human asmctl call: use current brightness */
				if (use_backlight == 0) {
					/* may be -1, will fail
					 * get_video_up_level/get_video_down_level
					 */
					acpi_video_current_level =
					    get_acpi_video_level();
				} else {
					backlight_current_level =
					    props.brightness;
				}

				if (strcmp(argv[2], "up") == 0 ||
				    strcmp(argv[2], "u") == 0) {
					d = get_video_up_level();
					rc = set_lcd_brightness(d);
				} else if (strcmp(argv[2], "down") == 0 ||
					   strcmp(argv[2], "d") == 0) {
					d = get_video_down_level();
					rc = set_lcd_brightness(d);
				} else {
					usage(argv[0]);
					rc = -1;
				}
			}
			free(video_levels);
		}
		if (strcmp(argv[1], "kb") == 0 || strcmp(argv[1], "kbd") == 0 ||
		    strcmp(argv[1], "keyboard") == 0 ||
		    strcmp(argv[1], "key") == 0) {
			/* acpi: use the previously saved brightness */
			if (strcmp(argv[2], "acpi") == 0 ||
			    strcmp(argv[2], "a") == 0) {
				if (kb_current_level < 0) {
					cleanup();
					return 1;
				}
				rc = set_keyboard_backlight_level(
				    kb_current_level);
			} else {

				d = get_keyboard_backlight_level();
				if (d < 0) {
					cleanup();
					return 1;
				}

				/* human asmctl call: use current brightness */
				if (strcmp(argv[2], "up") == 0 ||
				    strcmp(argv[2], "u") == 0) {
					d += 10;
					if (d > 100)
						d = 100;
					rc = set_keyboard_backlight_level(d);
				} else if (strcmp(argv[2], "down") == 0 ||
					   strcmp(argv[2], "d") == 0) {
					d -= 10;
					if (d < 0)
						d = 0;
					rc = set_keyboard_backlight_level(d);
				} else {
					usage(argv[0]);
					rc = -1;
				}
			}
		}
	} else {
		usage(argv[0]);
		rc = -1;
	}

	cleanup();

	return (rc < 0 ? 1 : 0);
}
