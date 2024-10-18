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
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/backlight.h>
#include <sys/ioctl.h>
#include <sys/param.h>

#include "asmctl.h"

#define BACKLIGHT_ECO_LEVEL "backlight_economy_level"
#define BACKLIGHT_FUL_LEVEL "backlight_full_level"
#define BACKLIGHT_CUR_LEVEL "backlight_current_level"

/* file name of default backlight device */
static char *backlight_device = "/dev/backlight/backlight0";

struct backlight_context {
	int bc_economy_level;
	int bc_fullpower_level;
	int bc_current_level;
	int bc_fd;
	bool bc_levels_are_generated;
	int bc_nlevels;
	int *bc_levels;
};

static int
get_backlight_video_levels(struct backlight_context *c) {
	int i;
	/* struct containing backlight(9) properties */
	struct backlight_props props;

	if (c->bc_fd < 0)
		return -1;

	if (ioctl(c->bc_fd, BACKLIGHTGETSTATUS, &props) < 0) {
		fprintf(stderr, "ioctl BACKLIGHTGETSTATUS : %s\n",
			strerror(errno));
		return -1;
	}

	c->bc_nlevels = (props.nlevels != 0) ?
		props.nlevels : BACKLIGHTMAXLEVELS + 1;
	c->bc_levels_are_generated = (props.nlevels == 0);

	c->bc_levels = malloc(c->bc_nlevels * sizeof(int));
	if (c->bc_levels == NULL) {
		fprintf(stderr, "failed to allocate %zu bytes memory\n",
			c->bc_nlevels * sizeof(int));
		return -1;
	}

	for (i = 0; i < c->bc_nlevels; i++)
		c->bc_levels[i] = (props.nlevels != 0) ? props.levels[i] : i;

	if (c->bc_current_level < 0)
		c->bc_current_level = props.brightness;
	if (c->bc_economy_level < 0)
		c->bc_economy_level = 60; // arbitrary value
	if (c->bc_fullpower_level < 0)
		c->bc_fullpower_level = 100; // arbitrary value

	return 0;
}

static int
backlight_init(void *context)
{
	int fd;
	struct backlight_context *c = context;

	/* may fail */
	if ((c->bc_fd = open(backlight_device, O_RDWR)) < 0)
		return -1;

	c->bc_economy_level = -1;
	c->bc_fullpower_level = -1;
	c->bc_current_level = -1;

	return 0;
}

static int
backlight_load_conf(void *context, nvlist_t *cf)
{
	struct backlight_context *c = context;

	if (conf_get_int(cf, BACKLIGHT_ECO_LEVEL, &c->bc_economy_level) < 0 ||
	    conf_get_int(cf, BACKLIGHT_FUL_LEVEL, &c->bc_fullpower_level) < 0 ||
	    conf_get_int(cf, BACKLIGHT_CUR_LEVEL, &c->bc_current_level) < 0)
		return -1;

	return 0;
}

static int
backlight_save_conf(void *context, nvlist_t *cf)
{
	struct backlight_context *c = context;

	nvlist_add_number(cf, BACKLIGHT_ECO_LEVEL, c->bc_economy_level);
	nvlist_add_number(cf, BACKLIGHT_FUL_LEVEL, c->bc_fullpower_level);
	nvlist_add_number(cf, BACKLIGHT_CUR_LEVEL, c->bc_current_level);

	return 0;
}

#ifdef USE_CAPSICUM
static int
backlight_cap_set_rights(void *context, cap_sysctl_limit_t *limits)
{
	struct backlight_context *c = context;
	cap_rights_t bc_fd_rights;
	static const unsigned long backlightcmds[] = {BACKLIGHTUPDATESTATUS,
						      BACKLIGHTGETSTATUS};

	if (c->bc_fd == -1)
 		return 0;

	/* limit bc_fd to ioctl */
	cap_rights_init(&bc_fd_rights, CAP_IOCTL);
	if (cap_rights_limit(c->bc_fd, &bc_fd_rights) < 0) {
		fprintf(stderr, "cap_rights_limit() failed\n");
		return -1;
	}

	/* limit allowed bc_fd ioctl commands */
	if (cap_ioctls_limit(c->bc_fd, backlightcmds, nitems(backlightcmds)) < 0) {
		fprintf(stderr, "cap_ioctls_limit() failed\n");
		return -1;
	}

	return 0;
}
#endif

static int
backlight_cleanup(void *context)
{
	struct backlight_context *c = context;

	if (c->bc_fd != -1) {
		close(c->bc_fd);
		c->bc_fd = -1;
	}
	free(c->bc_levels);

	return 0;
}

static int
set_backlight_video_level(struct backlight_context *c, int val) {
	/* struct containing backlight(9) properties */
	struct backlight_props props;

	if (val < 0 || val > 100)
		return -1;

	props.brightness = val;

	if (ioctl(c->bc_fd, BACKLIGHTUPDATESTATUS, &props) < 0) {
		fprintf(stderr, "ioctl BACKLIGHTUPDATESTATUS : %s\n",
			strerror(errno));
		return -1;
	}

	printf("set backlight brightness: %d\n", val);

	c->bc_current_level = val;

	if (ac_powered)
		c->bc_fullpower_level = val;
	else
		c->bc_economy_level = val;

	return 0;
}

static int
backlight_event(void *context)
{
	struct backlight_context *c = context;
	int alv = choose_acpi_level(c->bc_economy_level,
				    c->bc_fullpower_level);

	return set_backlight_video_level(c, alv);
}

static int
get_video_up_level(struct backlight_context *c)
{
	int i, v = c->bc_current_level;

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
	if (c->bc_levels_are_generated && v < 100)
		v++;

	for (i = 0; i < c->bc_nlevels; i++)
		if (c->bc_levels[i] == v)
			return c->bc_levels[MIN(i + 1, c->bc_nlevels - 1)];

	return -1;
}

static int
backlight_up(void *context)
{
	struct backlight_context *c = context;

	if (get_backlight_video_levels(c) < 0)
		return -1;

	return set_backlight_video_level(c, get_video_up_level(c));

}

static int
get_video_down_level(struct backlight_context *c)
{
	int i, v = c->bc_current_level;

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

	if (c->bc_levels_are_generated && v >= 2)
		v--;

	for (i = c->bc_nlevels - 1; i >= 0; i--)
		if (c->bc_levels[i] == v)
			return c->bc_levels[MAX(0, i - 1)];

	return -1;
}

static int
backlight_down(void *context)
{
	struct backlight_context *c = context;

	if (get_backlight_video_levels(c) < 0)
		return -1;

	return set_backlight_video_level(c, get_video_down_level(c));
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
	.cap_set_rights = backlight_cap_set_rights,
#endif
	.cleanup = backlight_cleanup,
	.acpi_event = backlight_event,
	.up = backlight_up,
	.down = backlight_down
};
