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

#include "asmctl.h"

struct backlight_context {
	int bc_ac_powered;
	int bc_battery_powered;
	int bc_nvalues;
	int *bc_values;
};

static int
backlight_init(void *context)
{
	struct backlight_context *c = context;

	return 0;
}

static int
backlight_load_conf(void *context, nvlist_t *conf)
{
	struct backlight_context *c = context;

	return 0;
}

static int
backlight_save_conf(void *context, nvlist_t *conf)
{
	struct backlight_context *c = context;

	return 0;
}

#ifdef USE_CAPSICUM
static int
backlight_cap_set_rights(void *context, cap_sysctl_limit_t *limits);

{
	struct backlight_context *c = context;

	return 0;
}
#endif

static int
backlight_cleanup(void *context)
{
	struct backlight_context *c = context;

	return 0;
}

static int
backlight_event(void *context, int event)
{
	struct backlight_context *c = context;

	return 0;
}

static int
backlight_up(void *context)
{
	struct backlight_context *c = context;

	return 0;
}

static int
backlight_down(void *context)
{
	struct backlight_context *c = context;

	return 0;
}

struct asmc_driver backlight_driver =
{
	.name = "backlight",
	.category = VIDEO,
	.ctx_size = sizeof(struct backlight_context),
	.init = backlight_init,
	.load_conf = backlight_load_conf,
	.save_conf = backlight_save_conf,
#ifdef USE_CAPSICUM
	.cap_set_rights = backlight_cap_enter,
#endif
	.cleanup = backlight_cleanup,
	.acpi_event = backlight_event,
	.up = backlight_up,
	.down = backlight_down
};
