/*-
 * Copyright (c) 2024 Yuichiro NAITO <naito.yuichiro@gmail.com>
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
#ifndef _ASMCTL_H_
#define _ASMCTL_H_

#if (defined(HAVE_SYS_CAPSICUM_H) && (HAVE_LIBCASPER_H))
#define WITH_CASPER 1 // WITH_CASPER is needed since 12.0R
#define USE_CAPSICUM 1
#include <libcasper.h>
#include <sys/capsicum.h>
#include <casper/cap_sysctl.h>
#ifdef HAVE_CAPSICUM_HELPERS_H
#include "capsicum_helpers.h"
#else
#include <nl_types.h>
#endif
#endif

#include <sys/nv.h>

#ifdef USE_CAPSICUM
#define sysctlbyname(A, B, C, D, E)                                            \
	cap_sysctlbyname(ch_sysctl, (A), (B), (C), (D), (E))
#endif

#define ARRAY_FOREACH(p, a) \
	for (p = &a[0]; p < &a[nitems(a)]; p++)

enum CATEGORY {
	NONE = 0,
	VIDEO,
	KEYBOARD
};

#ifdef HAVE_CAP_SYSCTL_LIMIT_NAME
#define cap_sysctl_limit_destroy(l)
#else
#define cap_sysctl_limit_init(ch)    nvlist_create(0)
#define cap_sysctl_limit_t           nvlist_t
#define cap_sysctl_limit_name        nvlist_add_number
#define cap_sysctl_limit(l)          cap_limit_set(ch_sysctl, (l))
#define cap_sysctl_limit_destroy(l)  nvlist_destroy((l))
#endif

struct asmc_driver {
	char *name;
	enum CATEGORY category;
	size_t ctx_size;
	int (*init)(void *);
	int (*load_conf)(void *, nvlist_t *);
	int (*save_conf)(void *, nvlist_t *);
#ifdef USE_CAPSICUM
	int (*cap_set_rights)(void *, cap_channel_t *, cap_sysctl_limit_t *);
#endif
	int (*cleanup)(void *);
	int (*acpi_event)(void *, int);
	int (*up)(void *);
	int (*down)(void *);
};

struct asmc_driver_context {
	struct asmc_driver *driver;
	void *context;
};

#define ASMC_INIT(c)  (c)->driver->init((c)->context)
#define ASMC_LOAD(c, v)  (c)->driver->load_conf((c)->context, (v))
#define ASMC_SAVE(c, v)  (c)->driver->save_conf((c)->context, (v))
#define ASMC_SET_RIGHTS(c, ch, l)  \
	(c)->driver->cap_set_rights((c)->context, (ch), (l))
#define ASMC_CLEANUP(c)  (c)->driver->cleanup((c)->context)
#define ASMC_ACPI(c, v)  (c)->driver->acpi_event((c)->context, (v))
#define ASMC_UP(c)  (c)->driver->up((c)->context)
#define ASMC_DOWN(c)  (c)->driver->down((c)->context)

extern struct asmc_driver acpi_video_driver;
extern struct asmc_driver acpi_keyboard_driver;
extern struct asmc_driver backlight_driver;
extern int ac_powered;

#endif
