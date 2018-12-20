/*-
 * Copyright (c) 2015 Yuichiro NAITO <naito.yuichiro@gmail.com>
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
 * This command requires following sysctl variables.
 *
 *  hw.acpi.video.lcd0.*    (acpi_video(4))
 *  hw.asmc.0.light.control (asmc(4))
 *  hw.acpi.acline          (acpi(4))
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if (defined(HAVE_SYS_CAPSICUM_H) && (HAVE_LIBCASPER_H))
#  define WITH_CASPER  1      // WITH_CASPER is needed since 12.0R
#  define USE_CAPSICUM 1
#  include <sys/nv.h>
#  include <sys/capsicum.h>
#  include <libcasper.h>
#  include <casper/cap_sysctl.h>
#endif

#define KB_CUR_LEVEL "dev.asmc.0.light.control"

#define VIDEO_LEVELS "hw.acpi.video.lcd0.levels"
#define VIDEO_ECO_LEVEL "hw.acpi.video.lcd0.economy"
#define VIDEO_FUL_LEVEL "hw.acpi.video.lcd0.fullpower"
#define VIDEO_CUR_LEVEL "hw.acpi.video.lcd0.brightness"

#define AC_POWER "hw.acpi.acline"

/* Keyboard backlight current level (0-100) */
int kb_current_level;

/* Number of video levels */
int num_of_video_levels=0;

/* Array of video level values */
int *video_levels=NULL;

/* Video backlight current level (one of video_levels)*/
int video_current_level;

/* Video backlight economy level (one of video_levels)*/
int video_economy_level=-1;

/* Video backlight fullpower level (one of video_levels)*/
int video_fullpower_level=-1;

/* set 1 if AC powered else 0 */
int ac_powered=0;

/* file name to save state */
static char *conf_filename="/var/lib/asmctl.conf";

/* file descriptor for the state file */
int conf_fd;

#ifdef USE_CAPSICUM
cap_channel_t *ch_sysctl;
#define sysctlbyname(A,B,C,D,E) cap_sysctlbyname(ch_sysctl,(A),(B),(C),(D),(E))
#endif

/**
   Store backlight levels to file.
   Write in sysctl.conf(5) format to restore by sysctl(1)
 */
int store_conf_file()
{
	int rc;
	FILE *fp;

	ftruncate(conf_fd, 0);
	lseek(conf_fd, 0, SEEK_SET);
	fp=fdopen(conf_fd,"w");
	if (fp==NULL)
	{
		fprintf(stderr,"can not write %s\n",conf_filename);
		return -1;
	}
	fprintf(fp,
		"# DO NOT EDIT MANUALLY!\n"
		"# This file is written by asmctl.\n");
	fprintf(fp,"%s=%d\n",VIDEO_ECO_LEVEL,video_economy_level);
	fprintf(fp,"%s=%d\n",VIDEO_FUL_LEVEL,video_fullpower_level);
	fprintf(fp,"%s=%d\n",VIDEO_CUR_LEVEL,video_current_level);
	fprintf(fp,"%s=%d\n",KB_CUR_LEVEL,kb_current_level);
	rc=fclose(fp);
	if (rc<0) {
		fprintf(stderr,"can not write %s\n",conf_filename);
		return -1;
	}

	return 0;
}

int set_keyboard_backlight_level(int val)
{
	int rc;
	char buf[sizeof(int)];

	memcpy(buf,&val,sizeof(int));

	rc=sysctlbyname(KB_CUR_LEVEL,
			NULL, NULL, buf, sizeof(int));
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			KB_CUR_LEVEL,strerror(errno));
		return -1;
	}

	printf("set keyboard backlight brightness: %d\n",val);

	kb_current_level=val;

	return store_conf_file();
}

int get_keyboard_backlight_level()
{
	int rc;
	char buf[sizeof(int)];
	size_t buflen=sizeof(int);

	rc=sysctlbyname(KB_CUR_LEVEL,
			buf, &buflen, NULL,0);
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			KB_CUR_LEVEL,strerror(errno));
		return -1;
	}

	rc=*((int*)buf);
	kb_current_level=rc;

	return rc;
}

int set_video_level(int val)
{
	char *key;
	int rc;
	char buf[sizeof(int)];

	memcpy(buf,&val,sizeof(int));

	rc=sysctlbyname(VIDEO_CUR_LEVEL,
			NULL, NULL, buf, sizeof(int));
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			VIDEO_CUR_LEVEL,strerror(errno));
		return -1;
	}

	printf("set video brightness: %d\n",val);

	key = (ac_powered)?VIDEO_FUL_LEVEL:VIDEO_ECO_LEVEL;

	rc=sysctlbyname(key,
			NULL, NULL, buf, sizeof(int));
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			key,strerror(errno));
		return -1;
	}

	video_current_level=val;

	if (ac_powered)
		video_fullpower_level=val;
	else
		video_economy_level=val;

	return store_conf_file();
}

int set_acpi_video_level()
{
	int rc;
	char buf[sizeof(int)];
	int *ptr;

	ptr = (ac_powered)?&video_fullpower_level:&video_economy_level;
	memcpy(buf,ptr,sizeof(int));

	rc=sysctlbyname(VIDEO_CUR_LEVEL,
			NULL, NULL, buf, sizeof(int));
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			VIDEO_CUR_LEVEL,strerror(errno));
		return -1;
	}

	printf("set video brightness: %d\n",*((int*)buf));

	video_current_level=*((int*)buf);

	return store_conf_file();
}

int get_video_up_level()
{
	int v = video_current_level;
	int i;

	for (i=0;i<num_of_video_levels;i++)
	{
		if (video_levels[i]==v)
		{
			if (i==num_of_video_levels-1)
				return video_levels[i];
			else
				return video_levels[i+1];
		}
	}
	return -1;
}

int get_video_down_level()
{
	int v = video_current_level;
	int i;

	for (i=num_of_video_levels-1;i>=0;i--)
	{
		if (video_levels[i]==v)
		{
			if (i==0)
				return video_levels[0];
			else
				return video_levels[i-1];
		}
	}
	return -1;
}

int get_video_level()
{
	int rc;
	FILE *fp;
	char buf[128];
	char name[80];
	char *p;
	int value, len;

	lseek(conf_fd, 0, SEEK_SET);

	fp=fdopen(conf_fd,"r");
	if (fp==NULL)
	{
		fprintf(stderr,"can not read %s\n",conf_filename);
		return -1;
	}
	while(fgets(buf,sizeof(buf),fp)!=NULL) {
		if (buf[0]=='#') continue;
		p = strchr(buf, '=');
		if (p == NULL) continue;
		len = p - buf;
		len = (len < sizeof(name)-1) ? len : sizeof(name)-1;
		strncpy(name, buf, len);
		name[len]='\0';
		value = strtol(p+1, &p, 10);
		if (*p != '\n') continue;

		if (strcmp(name,VIDEO_ECO_LEVEL)==0) {
			video_economy_level=value;
		} else if (strcmp(name,VIDEO_FUL_LEVEL)==0) {
			video_fullpower_level=value;
		} else if (strcmp(name,VIDEO_CUR_LEVEL)==0) {
			video_current_level=value;
		}
	}

	fdclose(fp, NULL);

	return 0;
}

int get_ac_powered()
{
	int rc;
	char buf[128];
	size_t buflen=sizeof(buf);

	rc=sysctlbyname(AC_POWER,
			buf, &buflen, NULL,0);
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			AC_POWER,strerror(errno));
		return -1;
	}

	ac_powered=*((int*)buf);

	return 0;
}

int get_video_levels()
{
	int rc, *v, n;
	int buf[32];
	size_t buflen=sizeof(buf);

	rc=sysctlbyname(VIDEO_LEVELS,
			(void*)buf, &buflen, NULL,0);
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			VIDEO_LEVELS,strerror(errno));
		return -1;
	}

	n=buflen/sizeof(int);
	/* ignore fist two elements */
	n-=2;
	v=(int*)realloc(video_levels,n*sizeof(int));
	if (v==NULL)
	{
		fprintf(stderr,"failed to allocate %zu bytes memory\n",buflen);
		return -1;
	}
	memcpy(v,&buf[2],n*sizeof(int));

	num_of_video_levels=n;
	video_levels=v;

    /* if conf_file is empty or not created,
       use default value */
	if (video_economy_level < 0) {
		video_economy_level = n > 3 ? v[3] : v[n];
	}
	if (video_fullpower_level < 0) {
		video_fullpower_level = n > 3 ? v[3] : v[n];
	}

	return 0;
}


#ifdef USE_CAPSICUM
int init_capsicum()
{
	int rc;
	nvlist_t *limits;
	cap_channel_t *ch_casper;
	cap_rights_t conf_fd_rights;

	/* Open a channel to casperd */
	ch_casper = cap_init();
	if (ch_casper == NULL) {
		fprintf(stderr,"cap_init() failed\n");
		return -1;
	}

	/* Enter capability mode */
	if (cap_enter() < 0) {
		fprintf(stderr,"capability is not supported\n");
		cap_close(ch_casper);
		return -1;
	}

	/* limit conf_fd to read/write/seek/fcntl */
	/* fcntl is used in fdopen(3) */
	cap_rights_init(&conf_fd_rights, CAP_READ|CAP_WRITE|CAP_SEEK|CAP_FCNTL);
	if (cap_rights_limit(conf_fd, &conf_fd_rights) < 0) {
		fprintf(stderr,"cap_rights_limit() failed\n");
		cap_close(ch_casper);
		return -1;
	}

	/* open channel to casper sysctl */
	ch_sysctl = cap_service_open(ch_casper, "system.sysctl");
	if (ch_sysctl == NULL) {
		fprintf(stderr,"cap_service_open(\"system.sysctl\") failed\n");
		cap_close(ch_casper);
		return -1;
	}

	/* limit sysctl names as following */
	limits = nvlist_create(0);
	nvlist_add_number(limits, VIDEO_LEVELS, CAP_SYSCTL_READ);
	nvlist_add_number(limits, VIDEO_ECO_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, VIDEO_FUL_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, VIDEO_CUR_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, KB_CUR_LEVEL, CAP_SYSCTL_RDWR);
	nvlist_add_number(limits, AC_POWER, CAP_SYSCTL_READ);

	rc = cap_limit_set(ch_sysctl, limits);
	if (rc < 0) {
		fprintf(stderr,"cap_service_limit failed %s\n",strerror(errno));
		nvlist_destroy(limits);
		cap_close(ch_casper);
		cap_close(ch_sysctl);
		return -1;
	}

	nvlist_destroy(limits);

	/* close connection to casper */
	cap_close(ch_casper);

	return 0;
}
#endif


int main(int argc, char *argv[])
{
	int d;

	conf_fd=open(conf_filename, O_CREAT|O_RDWR, 0600);
	if (conf_fd<0)
	{
		fprintf(stderr,"can not open %s\n",conf_filename);
		return 1;
	}

#ifdef USE_CAPSICUM
	if (init_capsicum()<0) return 1;
#endif

	/* initialize */
	if (get_ac_powered()<0) return 1;
	if (get_video_level()<0) return 1;
	if (get_video_levels()<0) return 1;

	if (argc>2) {
		if (strcmp(argv[1],"video")==0||
		    strcmp(argv[1],"lcd")==0)
		{
			if (strcmp(argv[2],"up")==0||
			    strcmp(argv[2],"u")==0)
			{
				d=get_video_up_level();
				set_video_level(d);
			}
			if (strcmp(argv[2],"down")==0||
			    strcmp(argv[2],"d")==0)
			{
				d=get_video_down_level();
				set_video_level(d);
			}
			if (strcmp(argv[2],"acpi")==0||
			    strcmp(argv[2],"a")==0)
			{
				set_acpi_video_level();
			}
		}
		if (strcmp(argv[1],"kb")==0||
		    strcmp(argv[1],"kbd")==0||
		    strcmp(argv[1],"keyboard")==0||
		    strcmp(argv[1],"key")==0)
		{
			d=get_keyboard_backlight_level();
			if (d<0) return 1;
			if (strcmp(argv[2],"up")==0||
			    strcmp(argv[2],"u")==0)
			{
				d+=10;
				if (d>100) d=100;
			}
			if (strcmp(argv[2],"down")==0||
			    strcmp(argv[2],"d")==0)
			{
				d-=10;
				if (d<0) d=0;
			}
			set_keyboard_backlight_level(d);
		}
	} else {
		printf("usage: %s [video|key] [up|down]\n",argv[0]);
		printf("\nChange video or keyboard backlight more or less bright.\n");
	}

	return 0;
}
