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
#include <string.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include "asmctl.h"

#define KB_CUR_LEVEL "dev.asmc.0.light.control"

struct acpi_keyboard_context {
#ifdef USE_CAPSICUM
	cap_channel_t *akc_sysctl;
#endif
	int akc_ac_powered;
	int akc_battery_powered;
	int akc_current_level;
	int akc_nvalues;
	int *akc_values;
};

static int
get_keyboard_backlight_level(struct acpi_keyboard_context *c)
{
	int val;
	size_t buflen = sizeof(val);

	if (sysctlbyname(KB_CUR_LEVEL, &val, &buflen, NULL, 0) < 0) {
		fprintf(stderr, "sysctl %s : %s\n", KB_CUR_LEVEL,
			strerror(errno));
		return -1;
	}

	c->akc_current_level = val;
	return 0;
}

static int
acpi_keyboard_init(void *context)
{
	struct acpi_keyboard_context *c = context;

	return 0;
}

static int
acpi_keyboard_load_conf(void *context, nvlist_t *conf)
{
	struct acpi_keyboard_context *c = context;

	if (nvlist_exists_number(conf, KB_CUR_LEVEL))
		c->akc_current_level = nvlist_get_number(conf, KB_CUR_LEVEL);

	return 0;
}

static int
acpi_keyboard_save_conf(void *context, nvlist_t *conf)
{
	struct acpi_keyboard_context *c = context;

	nvlist_add_number(conf, KB_CUR_LEVEL, c->akc_current_level);
	return 0;
}

#ifdef USE_CAPSICUM
static int
acpi_keyboard_cap_set_rights(void *context, cap_channel_t *ch_sysctl,
			     cap_sysctl_limit_t *limits);

{
	struct acpi_keyboard_context *c = context;

	c->akc_sysctl = ch_sysctl;

	cap_sysctl_limit_name(limits, KB_CUR_LEVEL, CAP_SYSCTL_RDWR);

	return 0;
}
#endif

static int
acpi_keyboard_cleanup(void *context)
{
	struct acpi_keyboard_context *c = context;
	// nothing to do
	return 0;
}

static int
set_keyboard_backlight_level(int val)
{
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

	return 0;
}

static int
acpi_keyboard_event(void *context, int event)
{
	struct acpi_keyboard_context *c = context;

	if (get_keyboard_backlight_level(c) < 0)
		return -1;
	return set_keyboard_backlight_level(c->akc_current_level);
}

static int
acpi_keyboard_up(void *context)
{
	struct acpi_keyboard_context *c = context;
	int d;

	if (get_keyboard_backlight_level(c) < 0)
		return -1;

	d = MIN(c->akc_current_level + 10, 100);
	if (set_keyboard_backlight_level(d) < 0)
		return -1;
	c->akc_current_level = d;

	return 0;
}

static int
acpi_keyboard_down(void *context)
{
	struct acpi_keyboard_context *c = context;
	int d;

	if (get_keyboard_backlight_level(c) < 0)
		return -1;

	d = MAX(c->akc_current_level - 10, 0);
	if (set_keyboard_backlight_level(d) < 0)
		return -1;
	c->akc_current_level = d;

	return 0;
}

struct asmc_driver acpi_keyboard_driver =
{
	.name = "acpi_keyboard",
	.category = KEYBOARD,
	.ctx_size = sizeof(struct acpi_keyboard_context),
	.init = acpi_keyboard_init,
	.load_conf = acpi_keyboard_load_conf,
	.save_conf = acpi_keyboard_save_conf,
#ifdef USE_CAPSICUM
	.cap_set_rights = acpi_keyboard_cap_enter,
#endif
	.cleanup = acpi_keyboard_cleanup,
	.acpi_event = acpi_keyboard_event,
	.up = acpi_keyboard_up,
	.down = acpi_keyboard_down
};
