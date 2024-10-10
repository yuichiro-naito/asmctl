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

struct acpi_video_context {
	int avc_ac_powered;
	int avc_battery_powered;
	int avc_nvalues;
	int *avc_values;
};

static int
acpi_video_init(void *context)
{
	struct acpi_video_context *c = context;

	return 0;
}

static int
acpi_video_load_conf(void *context, nvlist_t *conf)
{
	struct acpi_video_context *c = context;

	return 0;
}

static int
acpi_video_save_conf(void *context, nvlist_t *conf)
{
	struct acpi_video_context *c = context;

	return 0;
}

#ifdef USE_CAPSICUM
static int
acpi_video_cap_set_rights(void *context, cap_sysctl_limit_t *limits);

{
	struct acpi_video_context *c = context;

	return 0;
}
#endif

static int
acpi_video_cleanup(void *context)
{
	struct acpi_video_context *c = context;

	return 0;
}

static int
acpi_video_event(void *context, int event)
{
	struct acpi_video_context *c = context;

	return 0;
}

static int
acpi_video_up(void *context)
{
	struct acpi_video_context *c = context;

	return 0;
}

static int
acpi_video_down(void *context)
{
	struct acpi_video_context *c = context;

	return 0;
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
	.cap_set_rights = acpi_video_cap_enter,
#endif
	.cleanup = acpi_video_cleanup,
	.acpi_event = acpi_video_event,
	.up = acpi_video_up,
	.down = acpi_video_down
};
