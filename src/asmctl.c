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

#include "asmctl.h"

#define AC_POWER "hw.acpi.acline"

/* set 1 if AC powered else 0 */
int ac_powered = 0;

/* file name to save state */
static char *conf_filename = "/var/lib/asmctl.conf";

/* file descriptor for the state file */
int conf_fd = -1;

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
int
store_conf_file()
{
	FILE *fp;
	nvlist_t *nl;
	const char *name;
	int type;
	void *cookie;

	if (conf_fd < 0)
		return -1;

	if ((nl = nvlist_create(0)) == NULL) {
		fprintf(stderr, "nvlist_create: %s\n", strerror(errno));
		return -1;
	}
	ASMC_SAVE(&keyboard_ctx, nl);
	ASMC_SAVE(&video_ctx, nl);

	if (ftruncate(conf_fd, 0) < 0) {
		fprintf(stderr, "ftruncate: %s\n", strerror(errno));
		goto err;
	}

	if (lseek(conf_fd, 0, SEEK_SET) < 0) {
		fprintf(stderr, "lseek: %s\n", strerror(errno));
		goto err;
	}

	if ((fp = fdopen(conf_fd, "w")) == NULL) {
		fprintf(stderr, "can not write %s\n", conf_filename);
		goto err;
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

	if (fdclose(fp, NULL) < 0) {
		fprintf(stderr, "can not write %s\n", conf_filename);
		return -1;
	}

	return 0;
err:
	nvlist_destroy(nl);
	return -1;
}

int
conf_get_int(nvlist_t *conf, const char *key, int *val)
{
	if (! nvlist_exists_number(conf, key))
		return -1;
	*val = nvlist_get_number(conf, key);
	return 0;
}

int
get_saved_levels()
{
	FILE *fp;
	char buf[128];
	char name[80];
	char *p;
	int value, len;
	nvlist_t *nl;

	if (conf_fd < 0)
		return -1;

	if (lseek(conf_fd, 0, SEEK_SET) < 0) {
		fprintf(stderr, "lseek: %s\n", strerror(errno));
		return -1;
	}

	if ((fp = fdopen(conf_fd, "r")) == NULL) {
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
	}
	ASMC_LOAD(&keyboard_ctx, nl);
	ASMC_LOAD(&video_ctx, nl);

	nvlist_destroy(nl);
	if (fdclose(fp, NULL) < 0) {
		fprintf(stderr, "can not write %s\n", conf_filename);
		return -1;
	}

	return 0;
}

int
get_ac_powered()
{
	char buf[128];
	size_t buflen = sizeof(buf);

	if (sysctlbyname(AC_POWER, buf, &buflen, NULL, 0) < 0) {
		fprintf(stderr, "sysctl %s : %s\n", AC_POWER, strerror(errno));
		return -1;
	}

	ac_powered = *((int *)buf);

	return 0;
}

#ifdef USE_CAPSICUM
int
init_capsicum(struct asmc_driver_context *c)
{
	int rc;
	cap_sysctl_limit_t *limits;
	cap_channel_t *ch_casper;
	cap_rights_t conf_fd_rights;

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

	/* open channel to casper sysctl */
	ch_sysctl = cap_service_open(ch_casper, "system.sysctl");
	if (ch_sysctl == NULL) {
		fprintf(stderr, "cap_service_open(\"system.sysctl\") failed\n");
		cap_close(ch_casper);
		return rc;
	}

	limits = cap_sysctl_limit_init(ch_sysctl);
	cap_sysctl_limit_name(limits, AC_POWER, CAP_SYSCTL_READ);
	ASMC_SET_RIGHTS(&keyboard_ctx, ch_sysctl, limits);
	ASMC_SET_RIGHTS(&video_ctx, ch_sysctl, limits);

	rc = cap_sysctl_limit(limits);
	if (rc < 0) {
		cap_sysctl_limit_destroy(limits);
		fprintf(stderr, "cap_sysctl_limit failed %s\n",
			strerror(errno));
		cap_close(ch_casper);
		cap_close(ch_sysctl);
		return rc;
	}

	cap_sysctl_limit_destroy(limits);

	/* close connection to casper */
	cap_close(ch_casper);

	return 0;
}
#endif

void
usage(const char *prog)
{
	printf("usage: %s [video|key] [up|down]\n", prog);
	printf("\nChange video or keyboard backlight more or less bright.\n");
}

void
cleanup()
{
	ASMC_CLEANUP(&keyboard_ctx);
	ASMC_CLEANUP(&video_ctx);
	if (conf_fd != -1)
		close(conf_fd);
}

int
main(int argc, char *argv[])
{
	int rc = 0;
	struct asmc_driver_context *ctx;

	if (argc <= 2) {
		usage(argv[0]);
		return 1;
	}

	if (init_driver_context() < 0)
		return 1;

	if ((conf_fd = open(conf_filename, O_CREAT | O_RDWR, 0600)) < 0) {
		fprintf(stderr, "can not open %s\n", conf_filename);
		goto err;
	}

	if ((strcmp(argv[1], "video") == 0 || strcmp(argv[1], "lcd") == 0)) {
		ctx = &video_ctx;
	} else if (strcmp(argv[1], "kb") == 0 || strcmp(argv[1], "kbd") == 0 ||
		   strcmp(argv[1], "keyboard") == 0 ||
		   strcmp(argv[1], "key") == 0)
		ctx = &keyboard_ctx;
	else {
		usage(argv[0]);
		goto err;
	}

#ifdef USE_CAPSICUM
	if (init_capsicum(ctx) < 0)
		goto err;
#endif

	/* initialize */
	if (get_ac_powered() < 0 || get_saved_levels() < 0)
		goto err;

	if (strcmp(argv[2], "acpi") == 0 || strcmp(argv[2], "a") == 0) {
		ASMC_ACPI(ctx);
	} else if (strcmp(argv[2], "up") == 0 ||
		   strcmp(argv[2], "u") == 0) {
		ASMC_UP(ctx);
	} else if (strcmp(argv[2], "down") == 0 ||
		   strcmp(argv[2], "d") == 0) {
		ASMC_DOWN(ctx);
	}

	store_conf_file();

	cleanup();
	return 0;
err:
	cleanup();
	return 1;
}
