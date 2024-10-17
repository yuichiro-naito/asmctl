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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <sys/sysctl.h>

#include "asmctl.h"

#define ACPI_VIDEO_LEVELS "hw.acpi.video.lcd0.levels"

#define ACPI_VIDEO_ECO_LEVEL "hw.acpi.video.lcd0.economy"
#define ACPI_VIDEO_FUL_LEVEL "hw.acpi.video.lcd0.fullpower"
#define ACPI_VIDEO_CUR_LEVEL "hw.acpi.video.lcd0.brightness"

struct acpi_video_context {
#ifdef USE_CAPSICUM
	cap_channel_t *avc_sysctl;
#endif
	int avc_economy_level;
	int avc_fullpower_level;
	int avc_current_level;
	int avc_nlevels;
	int *avc_levels;
};

static int
acpi_video_init(void *context)
{
	struct acpi_video_context *c = context;

	c->avc_fullpower_level = -1;
	c->avc_economy_level = -1;

	return 0;
}

int conf_get_int(nvlist_t *, const char *, int *);

static int
acpi_video_load_conf(void *context, nvlist_t *cf)
{
	struct acpi_video_context *c = context;

	if (conf_get_int(cf, ACPI_VIDEO_ECO_LEVEL, &c->avc_economy_level) < 0 ||
	    conf_get_int(cf, ACPI_VIDEO_FUL_LEVEL, &c->avc_fullpower_level) < 0 ||
	    conf_get_int(cf, ACPI_VIDEO_CUR_LEVEL, &c->avc_current_level) < 0)
		return -1;

	return 0;
}

static int
acpi_video_save_conf(void *context, nvlist_t *cf)
{
	struct acpi_video_context *c = context;

	nvlist_add_number(cf, ACPI_VIDEO_ECO_LEVEL, c->avc_economy_level);
	nvlist_add_number(cf, ACPI_VIDEO_FUL_LEVEL, c->avc_fullpower_level);
	nvlist_add_number(cf, ACPI_VIDEO_CUR_LEVEL, c->avc_current_level);
	return 0;
}

#ifdef USE_CAPSICUM
static int
acpi_video_cap_set_rights(void *context, cap_channel_t *ch_sysctl,
			  cap_sysctl_limit_t *limits)
{
	struct acpi_video_context *c = context;

	c->avc_sysctl = ch_sysctl;

	cap_sysctl_limit_name(limits, ACPI_VIDEO_LEVELS, CAP_SYSCTL_READ);
	cap_sysctl_limit_name(limits, ACPI_VIDEO_ECO_LEVEL, CAP_SYSCTL_RDWR);
	cap_sysctl_limit_name(limits, ACPI_VIDEO_FUL_LEVEL, CAP_SYSCTL_RDWR);
	cap_sysctl_limit_name(limits, ACPI_VIDEO_CUR_LEVEL, CAP_SYSCTL_RDWR);

	return 0;
}
#endif

static int
acpi_video_cleanup(void *context)
{
	struct acpi_video_context *c = context;
	// nothing to do
	return 0;
}

static int
compare_video_levels(const void *a, const void *b)
{
	return (*(int*)a - *(int*)b);
}

static int
get_acpi_video_levels(struct acpi_video_context *c)
{
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
	if (c->avc_fullpower_level < 0)
		c->avc_fullpower_level = (int)buf[0];

	if (c->avc_economy_level < 0)
		c->avc_economy_level = (int)buf[1];

	/* ignore first two elements for range */
	n -= 2;
	v = (int *)malloc(n * sizeof(int));
	if (v == NULL) {
		fprintf(stderr, "failed to allocate %zu bytes memory\n",
			n * sizeof(int));
		free(buf);
		return -1;
	}

	memcpy(v, &buf[2], n * sizeof(int));

	qsort(v, n, sizeof(int), compare_video_levels);

	c->avc_nlevels = n;
	c->avc_levels = v;

	free(buf);

	return rc;
}

static int
set_acpi_video_level(struct acpi_video_context *c, int val) {
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

	c->avc_current_level = val;

	if (ac_powered)
		c->avc_fullpower_level = val;
	else
		c->avc_economy_level = val;

	return 0;
}

static int
get_video_up_level(struct acpi_video_context *c)
{
	int v = c->avc_current_level;
	int i;

	for (i = 0; i < c->avc_nlevels; i++)
		if (c->avc_levels[i] == v)
			return c->avc_levels[MIN(i + 1, c->avc_nlevels - 1)];

	return -1;
}

static int
get_video_down_level(struct acpi_video_context *c)
{
	int v = c->avc_current_level;
	int i;

	for (i = c->avc_nlevels - 1; i >= 0; i--)
		if (c->avc_levels[i] == v)
			return c->avc_levels[MAX(0, i - 1)];

	return -1;
}

static int
acpi_video_event(void *context, int event)
{
	struct acpi_video_context *c = context;

	if (get_acpi_video_levels(c) < 0)
		return -1;
	return set_acpi_video_level(c,
		    ac_powered ? c->avc_fullpower_level : c->avc_economy_level);
}

static int
acpi_video_up(void *context)
{
	struct acpi_video_context *c = context;

	if (get_acpi_video_levels(c) < 0)
		return -1;
	return set_acpi_video_level(c, get_video_up_level(c));
}

static int
acpi_video_down(void *context)
{
	struct acpi_video_context *c = context;

	if (get_acpi_video_levels(c) < 0)
		return -1;
	return set_acpi_video_level(c, get_video_down_level(c));
}

struct asmc_driver acpi_video_driver =
{
	.name = "acpi_video",
	.category = VIDEO,
	.ctx_size = sizeof(struct acpi_video_context),
	.init = acpi_video_init,
	.load_conf = acpi_video_load_conf,
	.save_conf = acpi_video_save_conf,
#ifdef USE_CAPSICUM
	.cap_set_rights = acpi_video_cap_set_rights,
#endif
	.cleanup = acpi_video_cleanup,
	.acpi_event = acpi_video_event,
	.up = acpi_video_up,
	.down = acpi_video_down
};
