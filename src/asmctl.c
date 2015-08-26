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
#include <errno.h>

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

/* Video backlight current level (one of video_level)*/
int video_current_level;

/* set 1 if AC powered else 0 */
int ac_powered=0;

int set_keyboard_backlight_level(int val)
{
	char *key;
	int rc;
	char buf[sizeof(int)];
	size_t buflen=sizeof(int);

	memcpy(buf,&val,sizeof(int));

	rc=sysctlbyname(KB_CUR_LEVEL,
			NULL, NULL, buf, sizeof(int));
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			KB_CUR_LEVEL,strerror(errno));
		return -1;
	}

	printf("set keyboard backlight brightness: %d\n",val);

	return 0;
}

int get_keyboard_backlight_level()
{
	int rc;
	char buf[128];
	size_t buflen=sizeof(buf);

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
	size_t buflen=sizeof(int);

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
			VIDEO_CUR_LEVEL,strerror(errno));
		return -1;
	}

	return 0;
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

int get_video_current_level()
{
	int rc;
	char *key;
	char buf[128];
	size_t buflen=sizeof(buf);

	rc=sysctlbyname(VIDEO_CUR_LEVEL,
			buf, &buflen, NULL,0);
	if (rc<0) {
		fprintf(stderr,"sysctl %s :%s\n",
			VIDEO_CUR_LEVEL,strerror(errno));
		return -1;
	}

	video_current_level = *((int*)buf);

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


static int
int_compare(const void *p1, const void *p2)
{
	int left = *(const int *)p1;
	int right = *(const int *)p2;

	return ((left > right) - (left < right));
}

int get_video_levels()
{
	int i,j, rc, *v,n;
	char buf[128];
	size_t buflen=sizeof(buf);

	rc=sysctlbyname(VIDEO_LEVELS,
			buf, &buflen, NULL,0);
	if (rc<0) {
		fprintf(stderr,"sysctl %s : %s\n",
			VIDEO_LEVELS,strerror(errno));
		return -1;
	}

	v=(int*)realloc(video_levels,buflen);
	if (v==NULL)
	{
		fprintf(stderr,"failed to allocate %ld bytes memory\n",buflen);
		return -1;
	}
	memcpy(v,buf,buflen);
	n=buflen/sizeof(int);

	/* sort and uniq levels */
	qsort(v,n,sizeof(int),int_compare);
	for (i=1,j=0;i<n;i++)
		if (v[i]!=v[j])
			v[++j]=v[i];

	num_of_video_levels=j+1;
	video_levels=v;

	return 0;
}


int main(int argc, char *argv[])
{
	int d;

	/* initialize */
	if (get_ac_powered()<0) return 1;
	if (get_video_current_level()<0) return 1;
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
