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
 * If a backlight(9) device is available, the following device file is
 * used instead of 'hw.acpi.video.lcd0.*'.
 *
 *  /dev/backlight/backlight0
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

#include "asmctl.h"

#define AC_POWER "hw.acpi.acline"

/* set 1 if AC powered else 0 */
int ac_powered = 0;

/* file name to save state */
static char *conf_filename = "/var/lib/asmctl.conf";

/* file descriptor for the state file */
static int conf_fd = -1;

/* available drivers. */
static struct asmc_driver *asmc_drivers[] = {
#ifdef HAVE_SYS_BACKLIGHT_H
    &backlight_driver,
#endif
    &acpi_video_driver, &acpi_keyboard_driver
};

/* driver context for video & keyboard. */
static struct asmc_driver_context video_ctx, keyboard_ctx;

/*
  available subcommands.
  MUST be sorted by name.
*/
static struct driver_type {
	char *name;
	struct asmc_driver_context *context;
} type_table[] = {
	{"kb", &keyboard_ctx},
	{"kbd", &keyboard_ctx},
	{"key", &keyboard_ctx},
	{"keyboard", &keyboard_ctx},
	{"lcd", &video_ctx},
	{"video", &video_ctx},
};

/*
  lookup up an asmc driver of the category. returns the first
  match and successfully initialized driver in 'asmc_drivers'.
 */
static int
lookup_driver(enum CATEGORY cat, struct asmc_driver **drv, void **ctx)
{
	struct asmc_driver *ad, **p;
	void *c;

	ARRAY_FOREACH(p, asmc_drivers) {
		ad = *p;
		if (ad->category != cat)
			continue;
		if ((c = calloc(1, ad->ctx_size)) == NULL) {
			fprintf(stderr,
				"failed to allocate %zu bytes memory\n",
				ad->ctx_size);
			return -1;
		}
		if (ad->init(c) < 0) {
			free(c);
			continue;
		}
		*drv = ad;
		*ctx = c;
		return 0;
	}
	return -1;
}

/* clean up the driver context */
static void cleanup_driver_context(struct asmc_driver_context *c)
{
	ASMC_CLEANUP(c);
	free(c->context);
}

/* initialize video & keyboard backlight drivers. */
static int
init_driver_context()
{
	if (lookup_driver(KEYBOARD, &keyboard_ctx.driver,
			  &keyboard_ctx.context) < 0)
		return -1;
	if (lookup_driver(VIDEO, &video_ctx.driver, &video_ctx.context) < 0) {
		cleanup_driver_context(&keyboard_ctx);
		return -1;
	}
	return 0;
}

/**
   Store backlight levels to file.
   Write in sysctl.conf(5) format to restore by sysctl(1)
 */
static int
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

/* utility: check and get the number from the key. */
int
conf_get_int(nvlist_t *conf, const char *key, int *val)
{
	if (! nvlist_exists_number(conf, key))
		return -1;
	*val = nvlist_get_number(conf, key);
	return 0;
}

/* utility: choose the brightness level on an acpi event */
int
choose_acpi_level(int eco, int full)
{
	return (ac_powered) ? (MAX(eco, full)) : (MIN(eco, full));
}

static int
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

static int
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

/* Global channel to the sysctl caspter.*/
cap_channel_t *ch_sysctl;

static int
init_capsicum(struct asmc_driver_context *c)
{
	cap_sysctl_limit_t *limits;
	cap_channel_t *ch_casper;
	cap_rights_t conf_fd_rights;

#ifdef HAVE_CAPSICUM_HELPERS_H
	caph_cache_catpages();
#else
	catopen("libc", NL_CAT_LOCALE);
#endif

	/* Open a channel to casperd */
	if ((ch_casper = cap_init()) == NULL) {
		fprintf(stderr, "cap_init() failed\n");
		return -1;
	}

	/* Enter capability mode */
	if (cap_enter() < 0) {
		fprintf(stderr, "capability is not supported\n");
		cap_close(ch_casper);
		return -1;
	}

	/* limit conf_fd to read/write/seek/fcntl/ftruncate */
	/* fcntl is used in fdopen(3) */
	cap_rights_init(&conf_fd_rights, CAP_READ | CAP_WRITE | CAP_SEEK |
					     CAP_FCNTL | CAP_FTRUNCATE);
	if (cap_rights_limit(conf_fd, &conf_fd_rights) < 0) {
		fprintf(stderr, "cap_rights_limit() failed\n");
		cap_close(ch_casper);
		return -1;
	}

	/* open channel to casper sysctl */
	if ((ch_sysctl = cap_service_open(ch_casper, "system.sysctl")) == NULL) {
		fprintf(stderr, "cap_service_open(\"system.sysctl\") failed\n");
		cap_close(ch_casper);
		return -1;
	}

	/* limit sysctl names */
	limits = cap_sysctl_limit_init(ch_sysctl);
	cap_sysctl_limit_name(limits, AC_POWER, CAP_SYSCTL_READ);

	/* set rights for the drivers */
	ASMC_SET_RIGHTS(&keyboard_ctx, limits);
	ASMC_SET_RIGHTS(&video_ctx, limits);

	if (cap_sysctl_limit(limits) < 0) {
		cap_sysctl_limit_destroy(limits);
		fprintf(stderr, "cap_sysctl_limit failed %s\n",
			strerror(errno));
		cap_close(ch_casper);
		cap_close(ch_sysctl);
		return -1;
	}

	cap_sysctl_limit_destroy(limits);

	/* close connection to casper */
	cap_close(ch_casper);

	return 0;
}
#endif

static void
usage(const char *prog)
{
	printf("usage: %s [video|key] [up|down]\n", prog);
	printf("\nChange video or keyboard backlight more or less bright.\n");
}

static void
cleanup()
{
	cleanup_driver_context(&keyboard_ctx);
	cleanup_driver_context(&video_ctx);
	if (conf_fd != -1)
		close(conf_fd);
}

static int
type_compare(const void *a, const void *b)
{
	const char *s = a;
	const struct driver_type *t = b;
	return strcmp(s, t->name);
}

int
main(int argc, char *argv[])
{
	int rc = 0;
	struct driver_type *type;
	struct asmc_driver_context *ctx;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	if (init_driver_context() < 0) {
		fprintf(stderr, "no driver is found\n");
		return 1;
	}

	if ((conf_fd = open(conf_filename, O_CREAT | O_RDWR, 0600)) < 0) {
		fprintf(stderr, "can not open %s\n", conf_filename);
		goto err;
	}

	/* lookup the driver context */
	type = bsearch(argv[1], type_table, nitems(type_table),
		      sizeof(type_table[0]), type_compare);
	if (type == NULL) {
		usage(argv[0]);
		goto err;
	}
	ctx = type->context;

#ifdef USE_CAPSICUM
	if (init_capsicum(ctx) < 0)
		goto err;
#endif

	/* initialize */
	if (get_ac_powered() < 0 || get_saved_levels() < 0)
		goto err;

	if (strcmp(argv[2], "acpi") == 0 || strcmp(argv[2], "a") == 0)
		ASMC_ACPI(ctx);
	else if (strcmp(argv[2], "up") == 0 || strcmp(argv[2], "u") == 0)
		ASMC_UP(ctx);
	else if (strcmp(argv[2], "down") == 0 || strcmp(argv[2], "d") == 0)
		ASMC_DOWN(ctx);
	else {
		usage(argv[0]);
		goto err;
	}
	store_conf_file();

	cleanup();
	return 0;
err:
	cleanup();
	return 1;
}
