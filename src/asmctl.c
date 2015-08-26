#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define VIDEO_LEVELS "hw.acpi.video.lcd0.levels"
#define VIDEO_ECO_LEVEL "hw.acpi.video.lcd0.economy"
#define VIDEO_FUL_LEVEL "hw.acpi.video.lcd0.fullpower"
#define VIDEO_CUR_LEVEL "hw.acpi.video.lcd0.brightness"

#define AC_POWER "hw.acpi.acline"

int num_of_video_levels=0;
int *video_levels=NULL;
int video_current_level;

/* set 1 if AC powered else 0 */
int ac_powered=0;

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

	if (ac_powered)
		key = VIDEO_FUL_LEVEL;
	else
		key = VIDEO_ECO_LEVEL;

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
	int i, rc, *v,n;
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
	num_of_video_levels=n=buflen/sizeof(int);
	video_levels=v;

	qsort(v,n,sizeof(int),int_compare);

	return 0;
}


int main(int argc, char *argv[])
{
	int d;

	/* initialize */
	if (get_ac_powered()<0) return 1;
	if (get_video_current_level()<0) return 1;
	if (get_video_levels()<0) return 1;

	if (argc>1) {
		if (strcmp(argv[1],"up")==0)
		{
			d=get_video_up_level();
			set_video_level(d);
		}
		if (strcmp(argv[1],"down")==0)
		{
			d=get_video_down_level();
			set_video_level(d);
		}
	}
	return 0;
}
