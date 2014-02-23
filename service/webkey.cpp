/*
Copyright (C) 2010  Peter Mora, Zoltan Papp

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "webkey.h"

#ifndef _WIN32_WCE /* Some ANSI #includes are not available on Windows CE */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#endif /* !_WIN32_WCE */

#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>

#include <vector>
#include <map>
#include <algorithm>


#include <netdb.h>

#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "suinput.h"
#include <stdio.h>
//#include <sys/types.h>
#include <sys/socket.h>
//#include <netinet/in.h>
#include <stdlib.h>
#include <strings.h>
#include "KeycodeLabels.h"
#include "kcm.h"
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/if.h>
#include "png.h"
#include "jpeglib.h"
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <netdb.h>
//#include <sys/stat.h>

#include <limits.h>
#include "mongoose.h"
#include "base64.h"

//#include <sys/wait.h>
//#include <unistd.h>
#include <linux/input.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <termios.h>
#include <dirent.h>

#include "minizip/zip.h"


#ifdef ANDROID
#include <android/log.h>
#else
#define PROP_VALUE_MAX 32
#endif

#undef stderr
FILE *stderr = stderr;

#ifdef MYDEB
#define FB_DEVICE "/android/dev/graphics/fb0"
#else
#define FB_DEVICE "/dev/graphics/fb0"
#endif

struct structfilename{
	const char* name;
};

static struct structfilename screencapbinaries[]{
	{"/system/bin/screencap"},
	{"/system/sbin/screencap"},
	{"/vendor/bin/screencap"},
	{"/sbin/screencap"},
	{"/data/data/com.magicandroidapps.bettertermpro/bin/screencap"},
	{"/system/bin/fbread 2>&1"}, //5
	{"/system/sbin/fbread 2>&1"},
	{"/vendor/bin/fbread 2>&1"},
	{"/sbin/fbread 2>&1"},
	{"/data/data/com.magicandroidapps.bettertermpro/bin/fbread 2>&1"},
//	{"/data/data/com.webkey/fbread"},
	{NULL}
};

#define FILENAME_MAX 4096
//#define LINESIZE 4096

//static HashMap* sessions;

static struct pid {
	struct pid *next;
	FILE *fp;
	pid_t pid;
} *pidlist;

struct stat info;


FILE *
mypopen(const char *program, const char *type)
{
	struct pid * volatile cur;
	FILE *iop;
	int pdes[2];
	pid_t pid;

	if ((*type != 'r' && *type != 'w') || type[1] != '\0') {
		errno = EINVAL;
		return (NULL);
	}

	if ((cur = (struct pid*)malloc(sizeof(struct pid))) == NULL)
		return (NULL);

	if (pipe(pdes) < 0) {
		free(cur);
		return (NULL);
	}

	switch (pid = fork()) {
	case -1:			/* Error. */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		free(cur);
		return (NULL);
		/* NOTREACHED */
	case 0:				/* Child. */
	    {
		struct pid *pcur;
		/*
		 * because vfork() instead of fork(), must leak FILE *,
		 * but luckily we are terminally headed for an execl()
		 */
		for (pcur = pidlist; pcur; pcur = pcur->next)
			close(fileno(pcur->fp));

		if (*type == 'r') {
			int tpdes1 = pdes[1];

			(void) close(pdes[0]);
			/*
			 * We must NOT modify pdes, due to the
			 * semantics of vfork.
			 */
			if (tpdes1 != STDOUT_FILENO) {
				(void)dup2(tpdes1, STDOUT_FILENO);
				(void)close(tpdes1);
				tpdes1 = STDOUT_FILENO;
			}
		} else {
			(void)close(pdes[1]);
			if (pdes[0] != STDIN_FILENO) {
				(void)dup2(pdes[0], STDIN_FILENO);
				(void)close(pdes[0]);
			}
		}
		execl("/system/bin/sh", "sh", "-c", program, (char *)NULL);
		_exit(127);
		/* NOTREACHED */
	    }
	}

//	usleep(100000);
	/* Parent; assume fdopen can't fail. */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void)close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		(void)close(pdes[0]);
	}

	/* Link into list of file descriptors. */
	cur->fp = iop;
	cur->pid =  pid;
	cur->next = pidlist;
	pidlist = cur;

	return (iop);
}

/*
 * pclose --
 *	Pclose returns -1 if stream is not associated with a `popened' command,
 *	if already `pclosed', or waitpid returns an error.
 */
int
mypclose(FILE *iop)
{
	struct pid *cur, *last;
	int pstat;
	pid_t pid;

	/* Find the appropriate file pointer. */
	for (last = NULL, cur = pidlist; cur; last = cur, cur = cur->next)
		if (cur->fp == iop)
			break;

	if (cur == NULL)
		return (-1);

	 signal(SIGPIPE,SIG_IGN);
	(void)fclose(iop);

	do {
		pid = waitpid(cur->pid, &pstat, 0);
	} while (pid == -1 && errno == EINTR);

	/* Remove the entry from the linked list. */
	if (last == NULL)
		pidlist = cur->next;
	else
		last->next = cur->next;
	free(cur);

	return (pid == -1 ? -1 : pstat);
}


struct SESSION
{
	int ptm;
	int oldwidth;
	int oldheight;
	int alive;
	pid_t pid;
	time_t lastused;
	pthread_mutex_t mutex;
};

struct eqstr
{
	bool operator()(const char* s1, const char* s2) const
	{
		return strcmp(s1, s2) == 0;
	}
};

static std::map<std::string, time_t> access_times;
static std::string logfile;

char modelname[PROP_VALUE_MAX];
char manufacturer[PROP_VALUE_MAX];

static char deflanguage[PROP_VALUE_MAX];//PROP_VALUE_MAX == 92

static bool has_ssl = false;

char* humandate(char* buff, long int epoch, int dateformat, int datesep, int datein, int datetimezone);
static pthread_mutex_t logmutex;
static pthread_mutex_t initfbmutex;
static void
access_log(const struct mg_request_info *ri, const char* s)
{
	pthread_mutex_lock(&logmutex);
	std::string log;
	if (ri && ri->remote_user)
	{
		log = ri->remote_user;
		log += ": ";
	}
	log += s;
	time_t last = access_times[log];

	struct timeval tv;
	gettimeofday(&tv,0);
	time_t now = tv.tv_sec;
	if (last == 0 || now > last+1800)
	{
		FILE* f = fopen(logfile.c_str(),"a");
		if (f)
		{
			char conv[LINESIZE];
			fprintf(f,"[%s] %s\n",humandate(conv,now,0,0,0,0),log.c_str());
			fclose(f);
		}
		access_times[log] = now;
//		printf("%d\n",now);
//		printf(" %s %d\n",log.c_str(),access_times[log.c_str()]);
	}
	pthread_mutex_unlock(&logmutex);
}

static void read_post_data(struct mg_connection *conn,
                const struct mg_request_info *ri, char** post_data, int* post_data_len)
{
	int l = contentlen(conn);
	if (l > 65536)
		l = 65536;
	*post_data = new char[l+1];
	*post_data_len = mg_read(conn,*post_data,l);
	(*post_data)[*post_data_len] = 0;
	//printf("!%s!\n",*post_data);
}

static std::vector<SESSION*> sessions;

static bool samsung = false;
static bool ignore_framebuffer = false;
static bool force_240 = false;
static bool force_544 = false;
static bool flip_touch = false;
static bool rotate_touch = false;
static bool touch_mxt224_ts_input = false;
static bool is_handle_m = false;
static bool is_reverse_colors = false;
static bool disable_mouse_pointer = false;
static bool is_icecreamsandwich = false;
static bool use_generic = false;
static bool run_load_keys = true;
static bool geniatech = false;
static bool use_uinput_mouse = false;

static int pipeforward[2];
static int pipeback[2];
static FILE* pipein;
static FILE* pipeout;

static char* server_username = NULL;
static char* server_random = NULL;
static bool server;
static int server_changes = 0;

static std::string requested_username;
static std::string requested_password;
static __u32 requested_ip;

static int port=80;
static int sslport=443;
static char* token;
//static int touchcount=0;
static std::string dir;
static int dirdepth = 0;
static std::string passfile;
//static std::string admin_password;
struct mg_context       *ctx;
static char position_value[512];
static std::string mimetypes[32];
//static std::string contacts;
//static std::string sms;
volatile static bool firstfb = false;

volatile static int up = 0;
volatile static int shutdownkey_up = -1;

static void
signal_handler(int sig_num)
{
        if (sig_num == SIGCHLD)
                while (waitpid(-1, &sig_num, WNOHANG) > 0);
        else
                exit_flag = sig_num;
}

volatile static bool wakelock;
volatile static time_t wakelock_lastused;
static pthread_mutex_t wakelockmutex;

static void lock_wakelock()
{
	pthread_mutex_lock(&wakelockmutex);
	if (!wakelock)
	{
		wakelock = true;
		int fd = open("/sys/power/wake_lock", O_WRONLY);
		if(fd >= 0)
		{
			write(fd, "Webkey\n", 7);
			close(fd);
		}
	}
	struct timeval tv;
	gettimeofday(&tv,0);
	wakelock_lastused = tv.tv_sec;
	pthread_mutex_unlock(&wakelockmutex);
}
static void unlock_wakelock(bool force)
{
	pthread_mutex_lock(&wakelockmutex);
	if (wakelock)
	{
		struct timeval tv;
		gettimeofday(&tv,0);
		if (force || wakelock_lastused +30 < tv.tv_sec)
		{
			int fd = open("/sys/power/wake_unlock", O_WRONLY);
			if(fd >= 0)
			{
				wakelock = false;
				write(fd, "Webkey\n", 7);
				close(fd);
			}
		}
	}
	pthread_mutex_unlock(&wakelockmutex);
}

//from android source tree
static int sendit(int timeout_ms)
{
    int nwr, ret, fd;
    char value[20];

    fd = open("/sys/class/timed_output/vibrator/enable", O_RDWR);
    if(fd < 0)
        return errno;

    nwr = sprintf(value, "%d\n", timeout_ms);
    ret = write(fd, value, nwr);

    close(fd);

    return (ret == nwr) ? 0 : -1;
}

static int vibrator_on(int timeout_ms)
{
    /* constant on, up to maximum allowed time */
    return sendit(timeout_ms);
}

static int vibrator_off()
{
    return sendit(0);
}

static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = sprintf(buffer, "%d\n", value);
        int amt = write(fd, buffer, bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            printf("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}


static void set_blink(bool blink,int onMS,int offMS)
{
	//from android source tree
	int freq, pwm;
	if (onMS > 0 && offMS > 0) {
		int totalMS = onMS + offMS;

		// the LED appears to blink about once per second if freq is 20
		// 1000ms / 20 = 50
		freq = totalMS / 50;
		// pwm specifies the ratio of ON versus OFF
		// pwm = 0 -> always off
		// pwm = 255 => always on
		pwm = (onMS * 255) / totalMS;

		// the low 4 bits are ignored, so round up if necessary
		if (pwm > 0 && pwm < 16)
			pwm = 16;
	} else {
		blink = 0;
		freq = 0;
		pwm = 0;
	}
	write_int("/sys/class/leds/red/device/blink",blink?1:0);
	write_int("/sys/class/leds/red/device/grpfreq",freq);
	write_int("/sys/class/leds/red/device/grppwm",pwm);

}

static int fbfd = -1;
static int screencap = -1;
static int screencap_skipbytes = 0;
static void *fbmmap = MAP_FAILED;
static int fbmmapsize = 0;
static int bytespp = 0;
static int lowest_offset = 0;
static struct fb_var_screeninfo scrinfo;
static png_byte* pict = NULL;
static png_byte** graph = NULL;
unsigned int* copyfb = NULL;
static int* lastpic = NULL;
static bool lastorient = 0;
static bool lastflip = 0;
static bool picchanged = true;
static int touchfd = -1;
static __s32 xmin;
static __s32 xmax;
static __s32 ymin;
static __s32 ymax;
static int newsockfd = -1;

static int max_brightness;

static bool dyndns = false;
static __u32 dyndns_last_updated_ip = 0;
static std::string dyndns_host;
static std::string dyndns_base64;


static pthread_mutex_t pngmutex;
static pthread_mutex_t fbmutex;
static pthread_mutex_t diffmutex;
static pthread_mutex_t popenmutex;
static pthread_mutex_t uinputmutex;
//static pthread_mutex_t terminalmutex;
static pthread_cond_t diffcond;
static pthread_cond_t diffstartcond;
volatile static int diffcondWaitCounter = 0;
volatile static int diffcondDiffCounter = 0;
//static pthread_mutex_t smsmutex;
//static pthread_cond_t smscond;
//static pthread_mutex_t contactsmutex;
//static pthread_cond_t contactscond;

/*
static bool dyndns = true;
static __u32 dyndns_last_updated_ip = 0;
static std::string dyndns_host = "g1.homeip.net";
static std::string dyndns_base64 = "b3BlcmF0Omxha2F0MTIz";
*/

//base
static const char* KCM_BASE[] ={
"A 'A' '2' 'a' 'A'",
"B 'B' '2' 'b' 'B'",
"C 'C' '2' 'c' 'C'",
"D 'D' '3' 'd' 'D'",
"E 'E' '3' 'e' 'E'",
"F 'F' '3' 'f' 'F'",
"G 'G' '4' 'g' 'G'",
"H 'H' '4' 'h' 'H'",
"I 'I' '4' 'i' 'I'",
"J 'J' '5' 'j' 'J'",
"K 'K' '5' 'k' 'K'",
"L 'L' '5' 'l' 'L'",
"M 'M' '6' 'm' 'M'",
"N 'N' '6' 'n' 'N'",
"O 'O' '6' 'o' 'O'",
"P 'P' '7' 'p' 'P'",
"Q 'Q' '7' 'q' 'Q'",
"R 'R' '7' 'r' 'R'",
"S 'S' '7' 's' 'S'",
"T 'T' '8' 't' 'T'",
"U 'U' '8' 'u' 'U'",
"V 'V' '8' 'v' 'V'",
"W 'W' '9' 'w' 'W'",
"X 'X' '9' 'x' 'X'",
"Y 'Y' '9' 'y' 'Y'",
"Z 'Z' '9' 'z' 'Z'",
"COMMA ',' ',' ',' '?'",
"PERIOD '.' '.' '.' '/'",
"AT '@' 0x00 '@' '~'",
"SPACE 0x20 0x20 0x20 0x20",
"ENTER 0xa 0xa 0xa 0xa",
"0 '0' '0' '0' ')'",
"1 '1' '1' '1' '!'",
"2 '2' '2' '2' '@'",
"3 '3' '3' '3' '#'",
"4 '4' '4' '4' '$'",
"5 '5' '5' '5' '%'",
"6 '6' '6' '6' '^'",
"7 '7' '7' '7' '&'",
"8 '8' '8' '8' '*'",
"9 '9' '9' '9' '('",
"TAB 0x9 0x9 0x9 0x9",
"GRAVE '`' '`' '`' '~'",
"MINUS '-' '-' '-' '_'",
"EQUALS '=' '=' '=' '+'",
"LEFT_BRACKET '[' '[' '[' '{'",
"RIGHT_BRACKET ']' ']' ']' '}'",
"BACKSLASH '\\' '\\' '\\' '|'",
"SEMICOLON ';' ';' ';' ':'",
"APOSTROPHE '\'' '\'' '\'' '\"'",
"STAR '*' '*' '*' '<'",
"POUND '#' '#' '#' '>'",
"PLUS '+' '+' '+' '+'",
"SLASH '/' '/' '/' '?'"
};

static const char* KCM_BASE2[] ={
"A 'A' '2' 'a' 'A'",
"B 'B' '2' 'b' 'B'",
"C 'C' '2' 'c' 'C'",
"D 'D' '3' 'd' 'D'",
"E 'E' '3' 'e' 'E'",
"F 'F' '3' 'f' 'F'",
"G 'G' '4' 'g' 'G'",
"H 'H' '4' 'h' 'H'",
"I 'I' '4' 'i' 'I'",
"J 'J' '5' 'j' 'J'",
"K 'K' '5' 'k' 'K'",
"L 'L' '5' 'l' 'L'",
"M 'M' '6' 'm' 'M'",
"N 'N' '6' 'n' 'N'",
"O 'O' '6' 'o' 'O'",
"P 'P' '7' 'p' 'P'",
"Q 'Q' '7' 'q' 'Q'",
"R 'R' '7' 'r' 'R'",
"S 'S' '7' 's' 'S'",
"T 'T' '8' 't' 'T'",
"U 'U' '8' 'u' 'U'",
"V 'V' '8' 'v' 'V'",
"W 'W' '9' 'w' 'W'",
"X 'X' '9' 'x' 'X'",
"Y 'Y' '9' 'y' 'Y'",
"Z 'Z' '9' 'z' 'Z'",
"COMMA ',' ',' ',' '?'",
"PERIOD '.' '.' '.' '/'",
"AT '@' '@' '@' '~'",
"SPACE ' ' ' ' ' ' ' '",
"ENTER '\\n' '\\n' '\\n' '\\n'",
"0 '0' '0' '0' ')'",
"1 '1' '1' '1' '!'",
"2 '2' '2' '2' '@'",
"3 '3' '3' '3' '#'",
"4 '4' '4' '4' '$'",
"5 '5' '5' '5' '%'",
"6 '6' '6' '6' '^'",
"7 '7' '7' '7' '&'",
"8 '8' '8' '8' '*'",
"9 '9' '9' '9' '('",
"TAB '\\t' '\\t' '\\t' '\\t'",
"GRAVE '`' '`' '`' '~'",
"MINUS '-' '-' '-' '_'",
"EQUALS '=' '=' '=' '+'",
"LEFT_BRACKET '[' '[' '[' '{'",
"RIGHT_BRACKET ']' ']' ']' '}'",
"BACKSLASH '\\\\' '\\\\' '\\\\' '|'",
"SEMICOLON ';' ';' ';' ':'",
"APOSTROPHE '\\\'' '\\\'' '\\\'' '\"'",
"STAR '*' '*' '*' '<'",
"POUND '#' '#' '#' '>'",
"PLUS '+' '+' '+' '+'",
"SLASH '/' '/' '/' '?'"
};


//  ,!,",#,$,%,&,',(,),*,+,,,-,.,/
int spec1[] = {62,8,75,10,11,12,14,75,16,7,15,70,55,69,56,56,52};
int spec1sh[] = {0,1,1,1,1,1,1,0,1,1,1,1,0,0,0,1};
// :,;,<,=,>,?,@
int spec2[] = {74,74,17,70,18,55,77};
int spec2sh[] = {1,0,1,0,1,1,0};
// [,\,],^,_,`
int spec3[] = {71,73,72,13,69,68};
int spec3sh[] = {0,0,0,1,1,0};
// {,|,},~
int spec4[] = {71,73,72,68};
int spec4sh[] = {1,1,1,1,0};


struct BIND{
	int ajax;
	int disp;
	int kcm;
	bool kcm_sh;
	bool sms;
};

struct FAST{
	bool show;
	char* name;
	int  ajax;
};

struct DEVSPEC{
	int dev;
	int type;
	int code;
};

static std::map<std::string, std::string> device_names;
static std::vector<BIND*> speckeys;
static std::vector<FAST*> fastkeys;
//static std::map<int, DEVSPEC> device_specific_buttons;
static int uinput_fd = -1;

bool startswith(const char* st, char* patt)
{
	int i = 0;
	while(patt[i])
	{
		if (st[i] == 0 || st[i]-patt[i])
			return false;
		i++;
	}
	return true;
}
bool urlcompare(const char* url, char* patt)
{
	int i = 0;
	while(true)
	{
		if (patt[i] == '*')
			return true;
		if ((!patt[i] && url[i]) || (patt[i] && !url[i]))
			return false;
		if (!patt[i] && !url[i])
			return true;
		if (url[i]-patt[i])
			return false;
		i++;
	}
}
bool cmp(const char* st, char* patt)
{
	int i = 0;
	while(patt[i] && st[i])
	{
		if (st[i]-patt[i])
			return false;
		i++;
	}
	if (st[i]-patt[i])
		return false;
	return true;
}


int contains(const char* st, const char* patt)
{
	int n = strlen(st);
	int m = strlen(patt);
	int i,j;
	for (i = 0; i<n-m+1; i++)
	{
		for (j=0;j<m;j++)
			if (st[i+j]!=patt[j])
				break;
		if (j==m)
			return i;
	}
	return 0;
}

static void init_fb_for_test()
{
//	scrinfo.xres_virtual = 544;
//	scrinfo.xres = 540;
//	scrinfo.yres_virtual = scrinfo.yres = 960;
//	scrinfo.xoffset = scrinfo.yoffset = 0;
}
char* humandur(char* buff, int secs)
{
	if (secs < 60)
		sprintf(buff,"%d secs",secs);
	else
	if (secs < 120)
		sprintf(buff,"1 min %d secs",secs%60);
	else
	if (secs < 3600)
		sprintf(buff,"%d mins %d secs",secs/60,secs%60);
	else
		sprintf(buff,"%d hours %d mins %d secs",secs/3600,(secs/60)%60,secs%60);
	return buff;
};

char* humandate(char* buff, long int epoch, int dateformat, int datesep, int datein, int datetimezone)
{
	if (datein == 1)
	{
		sprintf(buff,"%d",epoch);
		return buff;
	}
	time_t d = epoch + datetimezone*3600;
	struct tm* timeinfo;
	timeinfo = gmtime( &d );
	int a,b,c;
	if (dateformat == 0)
	{
		a = timeinfo->tm_mday; b = timeinfo->tm_mon+1; c = timeinfo->tm_year+1900;
	}
	if (dateformat == 1)
	{
		a = timeinfo->tm_mon+1; b = timeinfo->tm_mday; c = timeinfo->tm_year+1900;
	}
	if (dateformat == 2)
	{
		a = timeinfo->tm_year+1900; b = timeinfo->tm_mon+1; c = timeinfo->tm_mday;
	}
	if (datesep == 0) sprintf(buff,"%.2d/%.2d/%.2d %d:%.2d",a,b,c, timeinfo->tm_hour,timeinfo->tm_min);
	if (datesep == 1) sprintf(buff,"%.2d-%.2d-%.2d %d:%.2d",a,b,c, timeinfo->tm_hour,timeinfo->tm_min);
	if (datesep == 2) sprintf(buff,"%.2d.%.2d.%.2d. %d:%.2d",a,b,c, timeinfo->tm_hour,timeinfo->tm_min);
	return buff;
}


char* itoa(char* buff, int value)
{
	unsigned int i = 0;
	bool neg = false;
	if (value<0)
	{
		neg = true;
		value=-value;
	}
	if (value==0)
		buff[i++] = '0';
	else
		while(value)
		{
			buff[i++] = '0'+(char)(value%10);
			value = value/10;
		}
	if (neg)
		buff[i++] = '-';
	unsigned int j = 0;
	for(j = 0; j < (i>>1); j++)
	{
		char t = buff[j];
		buff[j] = buff[i-j-1];
		buff[i-j-1] = t;
	}
	buff[i++]=0;
	return buff;
}

long int getnum(const char* st)
{
	long int r = 0;
	long int i = 0;
	bool neg = false;
	if (st[i]=='-')
	{
		neg = true;
		i++;
	}
	while ('0'<=st[i] && '9'>=st[i])
	{
		r = r*10+(long int)(st[i]-'0');
		i++;
	}
	if (neg)
		return -r;
	else
		return r;
}
static void syst(const char* cmd)
{
//	pthread_mutex_lock(&popenmutex);
//	fprintf(pipeout,"S%s\n",cmd);
//	pthread_mutex_unlock(&popenmutex);
	system(cmd);
}
int getnum(char* st)
{
	int r = 0;
	int i = 0;
	bool neg = false;
	if (st[i]=='-')
	{
		neg = true;
		i++;
	}
	while ('0'<=st[i] && '9'>=st[i])
	{
		r = r*10+(int)(st[i]-'0');
		i++;
	}
	if (neg)
		return -r;
	else
		return r;
}

char* removesemicolon(char* to, const char* from)
{
	int i = 0;
	while(from[i] && i < LINESIZE - 1)
	{
		if(from[i] == ';')
			to[i] = ',';
		else
			to[i] = from[i];
		i++;
	}
	to[i++] = 0;
	return to;
}

bool check_type(const char* type1, const char* type2)
{
	int i = 0;
	int j = 0;
	while (type1[i] || type2[j])
	{
		while(type1[i] && (type1[i]<'a' || type1[i]>'z') && (type1[i]<'A' || type1[i]>'Z') && (type1[i]<'0' || type1[i]>'9')) i++;
		while(type2[j] && (type2[j]<'a' || type2[j]>'z') && (type2[j]<'A' || type2[j]>'Z') && (type2[j]<'0' || type2[j]>'9')) j++;
		if (!type1[i] && !type2[j])
			return true;
		char n1 = type1[i];
		char n2 = type2[j];
		if (n1 >= 'A' && n1 <= 'Z')
			n1 = n1 - 'A' + 'a';
		if (n2 >= 'A' && n2 <= 'Z')
			n2 = n2 - 'A' + 'a';
		if (n1 != n2)
			return false;
		i++;
		j++;
	}
}

void send_ok(struct mg_connection *conn, const char* extra = NULL,int size = 0)

{
	if (size)
	{
		if (extra)
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nConnection: close\r\nContent-Length: %d\r\n%s\r\n\r\n",size,extra);
		else
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nConnection: close\r\nContent-Length: %d\r\n\r\n",size);
	}
	else
	{
		if (extra)
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nConnection: close\r\n%s\r\n\r\n",extra);
		else
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nConnection: close\r\n\r\n");
	}
}
void clear(bool exit = true)
{
	int i;
	for(i=0; i < speckeys.size(); i++)
		if (speckeys[i])
			delete speckeys[i];
	speckeys.clear();
	for(i=0; i < fastkeys.size(); i++)
		if (fastkeys[i])
		{
			delete[] fastkeys[i]->name;
			delete fastkeys[i];
		}
	fastkeys.clear();
	if (uinput_fd != -1)
	{
		pthread_mutex_lock(&uinputmutex);
		suinput_close(uinput_fd);
		uinput_fd = -1;
		pthread_mutex_unlock(&uinputmutex);
	}
	if (exit)
	{
		if (pict)
			delete[] pict;
		pict = NULL;
		if (lastpic)
			delete[] lastpic;
		lastpic = NULL;
		if (graph)
			delete[] graph;
		graph = NULL;
		if (copyfb)
			delete[] copyfb;
		copyfb = NULL;
		if (server_username)
			delete[] server_username;
		server_username = NULL;
		if (server_random)
			delete[] server_random;
		if (fbmmap != MAP_FAILED)
			munmap(fbmmap, fbmmapsize);

		server_random = NULL;
		pthread_mutex_lock(&diffmutex);
		pthread_cond_broadcast(&diffstartcond);
		pthread_cond_broadcast(&diffcond);
		pthread_mutex_unlock(&diffmutex);
		sleep(1);
		pthread_mutex_destroy(&diffmutex);
		pthread_mutex_destroy(&popenmutex);
		pthread_mutex_destroy(&logmutex);
		pthread_mutex_destroy(&initfbmutex);
		pthread_mutex_destroy(&uinputmutex);
		pthread_mutex_destroy(&wakelockmutex);
		pthread_cond_destroy(&diffcond);
		pthread_cond_destroy(&diffstartcond);
		for(i=0; i < sessions.size(); i++)
		{
			pthread_mutex_destroy(&(sessions[i]->mutex));
			if (sessions[i]->alive)
			{
				kill(sessions[i]->pid,SIGKILL);
				close(sessions[i]->ptm);
			}
			delete sessions[i];
		}
		if (fbfd >= 0)
			close(fbfd);
//		pthread_mutex_destroy(&smsmutex);
//		pthread_cond_destroy(&smscond);
//		pthread_mutex_destroy(&contactsmutex);
//		pthread_cond_destroy(&contactscond);
	}
}

void error(const char *msg,const char *msg2 = NULL, const char *msg3=NULL, const char * msg4=NULL)
{
    perror(msg);
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO,"Webkey C++","service stoped");
#endif
    access_log(NULL,msg);
    if (msg2)
    {
	    perror(msg2);
#ifdef ANDROID
	    __android_log_print(ANDROID_LOG_ERROR,"Webkey C++",msg2);
#endif
	    access_log(NULL,msg2);
    }
    if (msg3)
    {
	    perror(msg3);
#ifdef ANDROID
	    __android_log_print(ANDROID_LOG_ERROR,"Webkey C++",msg3);
#endif
	    access_log(NULL,msg3);
    }
    if (msg4)
    {
	perror(msg4);
#ifdef ANDROID
	    __android_log_print(ANDROID_LOG_ERROR,"Webkey C++",msg4);
#endif
	    access_log(NULL,msg4);
    }
    clear();
    exit(1);
}
FILE* fo(const char* filename, const char* mode)
{
	std::string longname = dir + filename;
	FILE* ret = fopen(longname.c_str(),mode);
	if (!ret)
		printf("Couldn't open %s with mode %s\n",longname.c_str(), mode);
//	else
//		printf("%s is opened.\n",longname.c_str());
	return ret;
}

void add_key(bool sms, int ajax, int disp)
{
	int i;
	for (i = 0; i < speckeys.size(); i++)
	{
		if (speckeys[i]->ajax == ajax && speckeys[i]->disp == disp && speckeys[i]->sms == sms)
			return;
	}
	BIND* load = new BIND;
	load->ajax = ajax;
	load->disp = disp;
	load->sms = sms;
	load->kcm = 0;
	speckeys.push_back(load);
}

static bool load_keys()
{
	clear(false);
	usleep(10000);
	int i;
//	FILE* kl = fopen("/android/system/usr/keylayout/Webkey.kl","w");
	FILE* kl;
	if (is_icecreamsandwich && geniatech == false)
	{
		kl = fopen("/system/usr/keylayout/Webkey.kl","w");
	}
	else
	{
		kl = fopen("/dev/Webkey.kl","w");
	}
	if (!kl)
	{
		printf("Couldn't open Webkey.kl for writing.\n");
		return false;
	}
	printf("Webkey.kl is opened.\n");
	i = 0;
	while (KEYCODES[i].value)
	{
		fprintf(kl,"key %d   %s   WAKE\n",KEYCODES[i].value,KEYCODES[i].literal);
		FAST* load = new FAST;
		load->show = 0;
		load->name = new char[strlen(KEYCODES[i].literal)+1];
		strcpy(load->name,KEYCODES[i].literal);
		load->ajax = 0;
		fastkeys.push_back(load);
		i++;
	}
	fclose(kl);

	FILE* fk = fo("fast_keys.txt","r");
	if (!fk)
		return false;
	int ajax, show, id;
	while(fscanf(fk,"%d %d %d\n",&id,&show,&ajax) == 3)
	{
		if (id-1 < i)
		{
			fastkeys[id-1]->ajax = ajax;
			fastkeys[id-1]->show = show;
		}
	}
	fclose(fk);

	FILE* sk = fo("spec_keys.txt","r");
	if (!sk)
		return false;
	int disp, sms;
	while(fscanf(sk,"%d %d %d\n",&sms,&ajax,&disp) == 3)
	{
		add_key(sms,ajax,disp);
	}
	fclose(sk);

	if (samsung) //don't ask, it's not my mess
	{
		add_key(0,103,103); //g
		add_key(0,71,71); //G
		add_key(0,100,100); //d
		add_key(0,68,68); //D
	}
	if (is_handle_m)
	{
		add_key(0,109,109); //m
		add_key(0,77,77); //M
	}

//	FILE* kcm = fopen("/android/system/usr/keychars/Webkey.kcm","w");
	FILE* kcm;
	if (is_icecreamsandwich && geniatech == false)
		kcm = fopen("/system/usr/keychars/Webkey.kcm","w");
	else
		kcm = fopen("/dev/Webkey.kcm","w");
	if (!kcm)
	{
		printf("Couldn't open Webkey.kcm for writing.\n");
		return false;
	}
//	printf("Webkey.kcm is opened.\n");
	if (is_icecreamsandwich && geniatech == false)
		fprintf(kcm,"type FULL\n");
	else
		fprintf(kcm,"[type=QWERTY]\n# keycode       display number  base    caps    alt     caps_alt\n");
	if (is_icecreamsandwich && geniatech == false)
	{
		for (int i=0; i<54;i++)
		{
			int j = 0;
			int q = 0;

			char st1[10];
			st1[0] = 0;
			q = 0;
			while (KCM_BASE2[i][j] != ' ' && KCM_BASE2[i][j])
				st1[q++] = KCM_BASE2[i][j++];
			st1[q] = 0;
			if (KCM_BASE2[i][j]) j++;

			char st2[10];
			st2[0] = 0;
			q = 0;
			while (KCM_BASE2[i][j] != ' ' && KCM_BASE2[i][j])
				st2[q++] = KCM_BASE2[i][j++];
			st2[q] = 0;
			if (KCM_BASE2[i][j]) j++;

			char st3[10];
			st3[0] = 0;
			q = 0;
			while (KCM_BASE2[i][j] != ' ' && KCM_BASE2[i][j])
				st3[q++] = KCM_BASE2[i][j++];
			st3[q] = 0;
			if (KCM_BASE2[i][j]) j++;

			char st4[10];
			st4[0] = 0;
			q = 0;
			while (KCM_BASE2[i][j] != ' ' && KCM_BASE2[i][j])
				st4[q++] = KCM_BASE2[i][j++];
			st4[q] = 0;
			if (KCM_BASE2[i][j]) j++;

			char st5[10];
			st5[0] = 0;
			q = 0;
			while (KCM_BASE2[i][j] != ' ' && KCM_BASE2[i][j])
				st5[q++] = KCM_BASE2[i][j++];
			st5[q] = 0;
			if (KCM_BASE2[i][j]) j++;

			if (strcmp(st1,"SPACE") == 0)
			{
				strcpy(st2,"' '");
				strcpy(st3,"' '");
				strcpy(st4,"' '");
				strcpy(st5,"' '");
			}

			fprintf(kcm,"key %s {\n",st1);
			fprintf(kcm," label: %s\n",st2);
			fprintf(kcm," base: %s\n",st4);
			if (i < 26)
				fprintf(kcm," shift, capslock: %s\n",st5);
			else
				fprintf(kcm," shift: %s\n",st5);

			int k = 0;
			if (2*i < speckeys.size())
			{
				k = speckeys[2*i]->disp;
				speckeys[2*i]->kcm_sh = false;
				if (i<28)
					speckeys[2*i]->kcm = 29+i; //A-Z, COMMA, PERIOD
				if (28==i)
					speckeys[2*i]->kcm = 77; //AT
				if (29==i)
					speckeys[2*i]->kcm = 62; //SPACE
				if (30==i)
					speckeys[2*i]->kcm = 66; //ENTER
				if (30<i && i<41)
					speckeys[2*i]->kcm = i-31+7; //0-9
				if (41==i)
					speckeys[2*i]->kcm = 61; //TAB
				if (42<=i && i < 50)
					speckeys[2*i]->kcm = i-42+68; //GRAVE, MINUS, EQUALS, LEFT/RIGHT_BRACKET, BACKSLASH, SEMICOLON, APOSTROPHE
				if (50==i)
					speckeys[2*i]->kcm = 17; // STAR
				if (51==i)
					speckeys[2*i]->kcm = 18; // POUND
				if (52==i)
					speckeys[2*i]->kcm = 81; // PLUS
				if (53==i)
					speckeys[2*i]->kcm = 76; // SLASH
			}
			int l = 0;
			if (2*i+1 < speckeys.size())
			{
				l = speckeys[2*i+1]->disp;
				speckeys[2*i+1]->kcm_sh = true;
				if (i<28)
					speckeys[2*i+1]->kcm = 29+i; //A-Z, COMMA, PERIOD
				if (28==i)
					speckeys[2*i+1]->kcm = 77; //AT
				if (29==i)
					speckeys[2*i+1]->kcm = 62; //SPACE
				if (30==i)
					speckeys[2*i+1]->kcm = 66; //ENTER
				if (30<i && i<41)
					speckeys[2*i+1]->kcm = i-31+7; //0-9
				if (41==i)
					speckeys[2*i+1]->kcm = 61; //TAB
				if (42<=i && i < 50)
					speckeys[2*i+1]->kcm = i-42+68; //GRAVE, MINUS, EQUALS, LEFT/RIGHT_BRACKET, BACKSLASH, SEMICOLON, APOSTROPHE
				if (50==i)
					speckeys[2*i+1]->kcm = 17; // STAR
				if (51==i)
					speckeys[2*i+1]->kcm = 18; // POUND
				if (52==i)
					speckeys[2*i+1]->kcm = 81; // PLUS
				if (53==i)
					speckeys[2*i+1]->kcm = 76; // SLASH
			}
			if (k)
				fprintf(kcm,"alt: '\\u%04X'\n",k);
			if (l)
				fprintf(kcm,"alt+shift: '\\u%04X'\n",l);
			fprintf(kcm,"}\n",l);
		}
	}
	else
	{
		int ii = 0;
		for(i=0; i<54; i++)
		{
			if (samsung && (i == 3 || i == 6))
			{
				fprintf(kcm,"%s %d %d\n",KCM_BASE[i],0,0);
				continue;
			}
			if (is_handle_m && (i == 12))
			{
				fprintf(kcm,"%s %d %d\n",KCM_BASE[i],0,0);
				continue;
			}
			int k = 0;
			if (2*ii < speckeys.size())
			{
				k = speckeys[2*ii]->disp;
				speckeys[2*ii]->kcm_sh = false;
				if (i<28)
					speckeys[2*ii]->kcm = 29+i; //A-Z, COMMA, PERIOD
				if (28==i)
					speckeys[2*ii]->kcm = 77; //AT
				if (29==i)
					speckeys[2*ii]->kcm = 62; //SPACE
				if (30==i)
					speckeys[2*ii]->kcm = 66; //ENTER
				if (30<i && i<41)
					speckeys[2*ii]->kcm = i-31+7; //0-9
				if (41==i)
					speckeys[2*ii]->kcm = 61; //TAB
				if (42<=i && i < 50)
					speckeys[2*ii]->kcm = i-42+68; //GRAVE, MINUS, EQUALS, LEFT/RIGHT_BRACKET, BACKSLASH, SEMICOLON, APOSTROPHE
				if (50==i)
					speckeys[2*ii]->kcm = 17; // STAR
				if (51==i)
					speckeys[2*ii]->kcm = 18; // POUND
				if (52==i)
					speckeys[2*ii]->kcm = 81; // PLUS
				if (53==i)
					speckeys[2*ii]->kcm = 76; // SLASH
			}
			int l = 0;
			if (2*ii+1 < speckeys.size())
			{
				l = speckeys[2*ii+1]->disp;
				speckeys[2*ii+1]->kcm_sh = true;
				if (i<28)
					speckeys[2*ii+1]->kcm = 29+i; //A-Z, COMMA, PERIOD
				if (28==i)
					speckeys[2*ii+1]->kcm = 77; //AT
				if (29==i)
					speckeys[2*ii+1]->kcm = 62; //SPACE
				if (30==i)
					speckeys[2*ii+1]->kcm = 66; //ENTER
				if (30<i && i<41)
					speckeys[2*ii+1]->kcm = i-31+7; //0-9
				if (41==i)
					speckeys[2*ii+1]->kcm = 61; //TAB
				if (42<=i && i < 50)
					speckeys[2*ii+1]->kcm = i-42+68; //GRAVE, MINUS, EQUALS, LEFT/RIGHT_BRACKET, BACKSLASH, SEMICOLON, APOSTROPHE
				if (50==i)
					speckeys[2*ii+1]->kcm = 17; // STAR
				if (51==i)
					speckeys[2*ii+1]->kcm = 18; // POUND
				if (52==i)
					speckeys[2*ii+1]->kcm = 81; // PLUS
				if (53==i)
					speckeys[2*ii+1]->kcm = 76; // SLASH
			}
			fprintf(kcm,"%s %d %d\n",KCM_BASE[i],k,l);
			ii++;
		}
	}
	fclose(kcm);
//	if (compile("/android/system/usr/keychars/Webkey.kcm","/android/system/usr/keychars/Webkey.kcm.bin"))
	if (is_icecreamsandwich == false && compile("/dev/Webkey.kcm","/dev/Webkey.kcm.bin"))
	{
		printf("Couldn't compile kcm to kcm.bin\n");
		return false;
	}
	return true;
}

static void init_uinput()
{
	bool loaded_keys;
	if (run_load_keys)
	{
		if (is_icecreamsandwich && geniatech == false)
			syst("mount -o remount,rw /system");
		loaded_keys = load_keys();
		if (is_icecreamsandwich && geniatech == false)
		{
			FILE* f = fopen("/system/usr/idc/Webkey.idc","w");
			if (f && loaded_keys)
			{
				fprintf(f,"touch.deviceType = touchScreen\n"
					"touch.orientationAware = 0\n"
					"keyboard.layout = Webkey\n"
					"keyboard.characterMap = Webkey\n"
					"keyboard.orientationAware = 1\n"
					"keyboard.builtIn = 1\n"
					"cursor.mode = navigation\n"
					"cursor.orientationAware = 0\n");
				fclose(f);
			}
			if (f && loaded_keys == false)
			{
				fprintf(f,"touch.deviceType = touchScreen\n"
					"touch.orientationAware = 0\n"
					"keyboard.layout = Generic\n"
					"keyboard.characterMap = Generic\n"
					"keyboard.orientationAware = 1\n"
					"keyboard.builtIn = 1\n"
					"cursor.mode = navigation\n"
					"cursor.orientationAware = 0\n");
				fclose(f);
			}
		}
		if (is_icecreamsandwich && geniatech == false)
			syst("mount -o remount,ro /system");
		printf("loaded_keys = %d\n",loaded_keys);
		use_generic = !loaded_keys;
		// in case the remount fails
		if (is_icecreamsandwich)
		{
			mkdir("/data/system/devices",S_IRUSR|S_IWUSR|S_IXUSR);
			mkdir("/data/system/devices/idc",S_IRUSR|S_IWUSR|S_IXUSR);
			FILE* f = fopen("/data/system/devices/idc/Webkey.idc","w");
			if (f && loaded_keys)
			{
				fprintf(f,"touch.deviceType = touchScreen\n"
					"touch.orientationAware = 0\n"
					"keyboard.layout = Webkey\n"
					"keyboard.characterMap = Webkey\n"
					"keyboard.orientationAware = 1\n"
					"keyboard.builtIn = 1\n"
					"cursor.mode = navigation\n"
					"cursor.orientationAware = 0\n");
				fclose(f);
			}
			if (f && loaded_keys == false)
			{
				fprintf(f,"touch.deviceType = touchScreen\n"
					"touch.orientationAware = 0\n"
					"keyboard.layout = Generic\n"
					"keyboard.characterMap = Generic\n"
					"keyboard.orientationAware = 1\n"
					"keyboard.builtIn = 1\n"
					"cursor.mode = navigation\n"
					"cursor.orientationAware = 0\n");
				fclose(f);
			}
		}
	}
//	printf("kcm.bin is compiled.\n");

	struct input_id uid = {
		0x06,//BUS_VIRTUAL, /* Bus type. */
		1, /* Vendor id. */
		1, /* Product id. */
		1 /* Version id. */
	};
//	uinput_fd = suinput_open("../../../sdcard/Webkey", &uid);
	printf("suinput init...\n");
	pthread_mutex_lock(&uinputmutex);
	usleep(100000);
//	if (geniatech)
//		uinput_fd = suinput_open("geniatech4", &uid, use_uinput_mouse);
	if (is_icecreamsandwich)
		uinput_fd = suinput_open("Webkey", &uid, use_uinput_mouse);
	else
		uinput_fd = suinput_open("/../../../dev/Webkey", &uid, use_uinput_mouse);
	if (uinput_fd == -1)
	{
		//TEMP
//		uinput_fd = open("/dev/input/event4", O_WRONLY | O_NONBLOCK);
		use_uinput_mouse = false;
	}


	usleep(100000);
	run_load_keys = false;
	pthread_mutex_unlock(&uinputmutex);



/*
	device_specific_buttons.clear();
	fk = fo("keycodes.txt","r");
	if (!fk)
		return;
	char line[256];
	while (fgets(line, sizeof(line)-1, fk) != NULL)
	{
		if (line[0] && line[strlen(line)-1]==10)
			line[strlen(line)-1] = 0;
		if (line[0] && line[strlen(line)-1]==13)
			line[strlen(line)-1] = 0;
		if (strcmp(line,modelname)==0) //this is the right phone
		{
			while (fgets(line, sizeof(line)-1, fk) != NULL)
			{
				if (strlen(line) < 3)
					break;
//				printf("PROCESSING %s\n",line);
				if (line[strlen(line)-1]==10)
					line[strlen(line)-1] = 0;
				if (line[strlen(line)-1]==13)
					line[strlen(line)-1] = 0;
				int n = strlen(line);
				int pos[2]; pos[0] = pos[1] = 0;
				int i = 0;
				int j;
				for (;i<n-1;i++)
					if (line[i] == ' ')
					{
						pos[0] = i;
						break;
					}
				for (i++;i<n-1;i++)
					if (line[i] == ' ')
					{
						pos[1] = i;
						break;
					}
				line[pos[0]]=0;
				DEVSPEC q;
				q.dev = device_names[std::string(line+pos[1]+1)];
				q.type = 1;
				q.code = getnum(line+pos[0]+1);
				j = 0;
				while (KEYCODES[j].value)
				{
					if(strcmp(line,KEYCODES[j].literal)==0)
					{
						break;
					}
					j++;
				}
				device_specific_buttons[KEYCODES[j].value] = q;

			}


//		while(fscanf(fk,"%d %d %d\n",&id,&show,&ajax) == 3)
//		{
//			if (id-1 < i)
//			{
//				fastkeys[id-1]->ajax = ajax;
//				fastkeys[id-1]->show = show;
//			}
//		}
		}
		else
		{
			bool b = true;
			while (b && strlen(line)>2)
			{
				b = fgets(line, sizeof(line)-1, fk);
			}
			if (!b)
				break;
		}

	}
	fclose(fk);
*/
}

//from android-vnc-server

static void init_fb(void)
{
	pthread_mutex_lock(&initfbmutex);
	if (pict)
	{
		pthread_mutex_unlock(&initfbmutex);
		return;
	}
        size_t pixels;

        pixels = scrinfo.xres_virtual * scrinfo.yres_virtual;
	if (fbfd < 0)
	{
		if (screencap > -1)
		{
			pict  = new png_byte[scrinfo.yres*scrinfo.xres*3];
			lastpic = new int[pixels * bytespp/4/7+1];
			if (scrinfo.yres > scrinfo.xres)	//orientation might be changed
				graph = new png_byte*[scrinfo.yres];
			else
				graph = new png_byte*[scrinfo.xres];
			copyfb = new unsigned int[scrinfo.yres*scrinfo.xres_virtual*bytespp/4];
		}
		pthread_mutex_unlock(&initfbmutex);
		return;
	}
//        pixels = scrinfo.xres * scrinfo.yres;
        bytespp = scrinfo.bits_per_pixel / 8;
	if (bytespp == 3)
		bytespp = 4;
	char tmp[1024];
	int size = 0;
	int s = 0;
	close(fbfd);
	fbfd = open(FB_DEVICE, O_RDONLY);
	while (s = read(fbfd, tmp, 1024))
		size += s;

	printf("size of fb = %d\n", size);
	if (size / scrinfo.xres_virtual / (scrinfo.bits_per_pixel/8) < scrinfo.yres_virtual)
	{
		printf("overriding scrinfo.yres_virtual from %d\n", scrinfo.yres_virtual);
		scrinfo.yres_virtual = size / scrinfo.xres_virtual / (scrinfo.bits_per_pixel/8);
	}

        printf("xres=%d, yres=%d, xresv=%d, yresv=%d, xoffs=%d, yoffs=%d, bpp=%d\n",
          (int)scrinfo.xres, (int)scrinfo.yres,
          (int)scrinfo.xres_virtual, (int)scrinfo.yres_virtual,
          (int)scrinfo.xoffset, (int)scrinfo.yoffset,
          (int)scrinfo.bits_per_pixel);

	if (pixels < scrinfo.xres_virtual*scrinfo.yoffset+scrinfo.xoffset+scrinfo.xres_virtual*scrinfo.yres )//for Droid
		pixels = scrinfo.xres_virtual*scrinfo.yoffset+scrinfo.xoffset+scrinfo.xres_virtual*scrinfo.yres;

	if (scrinfo.xres_virtual == 240) //for x10 mini pro
		scrinfo.xres_virtual = 256;
	if (force_240)
		scrinfo.xres_virtual = scrinfo.xres = 240;
	if (force_544)
		scrinfo.xres_virtual = 544;
	init_fb_for_test();


	//TEMP
	//pixels = 822272;
	//TEMP

	lowest_offset = scrinfo.xres_virtual*scrinfo.yoffset+scrinfo.xoffset;

        fbmmap = mmap(NULL, pixels * bytespp, PROT_READ, MAP_SHARED, fbfd, 0);
	fbmmapsize = pixels * bytespp;

        if (fbmmap == MAP_FAILED)
        {
                perror("mmap");
                exit(EXIT_FAILURE);
        }
	pict  = new png_byte[scrinfo.yres*scrinfo.xres*3];
	lastpic = new int[pixels * bytespp/4/7+1];
	if (scrinfo.yres > scrinfo.xres)	//orientation might be changed
		graph = new png_byte*[scrinfo.yres];
	else
		graph = new png_byte*[scrinfo.xres];
	copyfb = new unsigned int[scrinfo.yres*scrinfo.xres_virtual*bytespp/4];
	pthread_mutex_unlock(&initfbmutex);
}

void init_touch()
{
	int i;
#ifdef MYDEB
	char touch_device[27] = "/android/dev/input/event0";
#else
	char touch_device[19] = "/dev/input/event0";
#endif
	for (i=0; i<50; i++)
	{
		char name[256]="Unknown";
		if (i < 10)
		{
			touch_device[sizeof(touch_device)-3] = '0'+(char)(i);
			touch_device[sizeof(touch_device)-2] = 0;
		}
		else
		{
			touch_device[sizeof(touch_device)-3] = '0'+(char)(i/10);
			touch_device[sizeof(touch_device)-2] = '0'+(char)(i%10);
			touch_device[sizeof(touch_device)-1] = 0;
		}
		struct input_absinfo info;
		if((touchfd = open(touch_device, O_RDWR)) == -1)
		{
			continue;
		}
		printf("searching for touch device, opening %s ... ",touch_device);
		if (ioctl(touchfd, EVIOCGNAME(sizeof(name)),name) < 0)
		{
			printf("failed, no name\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		device_names[std::string(name)] = touch_device;
		if(use_uinput_mouse)
			continue;
		printf("%s ",name);
//		if (contains(name,"sec_touchscreen") && is_icecreamsandwich)
//		{
//			printf("It contains sec_touchscreen in its name and it's ICS, we try the uinput solution");
//			use_uinput_mouse = true;
//			return;
//		}
		if (contains(name,"touchscreen"))
		{
			printf("There is touchscreen in its name, it must be the right device!\n");
		}
		else
		{
			printf("\n");
			continue;
		}
		// Get the Range of X and Y
		if(ioctl(touchfd, EVIOCGABS(ABS_X), &info))
		{
			printf("failed, no ABS_X\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		xmin = info.minimum;
		xmax = info.maximum;
		if (xmin == 0 && xmax == 0)
		{
			if(ioctl(touchfd, EVIOCGABS(53), &info))
			{
				printf("failed, no ABS_X\n");
				close(touchfd);
				touchfd = -1;
				continue;
			}
			xmin = info.minimum;
			xmax = info.maximum;
		}

		if(ioctl(touchfd, EVIOCGABS(ABS_Y), &info)) {
			printf("failed, no ABS_Y\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		ymin = info.minimum;
		ymax = info.maximum;
		if (ymin == 0 && ymax == 0)
		{
			if(ioctl(touchfd, EVIOCGABS(54), &info))
			{
				printf("failed, no ABS_Y\n");
				close(touchfd);
				touchfd = -1;
				continue;
			}
			ymin = info.minimum;
			ymax = info.maximum;
		}
		if (xmin < 0 || xmin == xmax)	// xmin < 0 for the compass
		{
			printf("failed, xmin<0 || xmin==xmax\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		printf("success\n");
#ifdef ANDROID
		__android_log_print(ANDROID_LOG_INFO,"Webkey C++","using touch device: %s",touch_device);
#endif
		printf("xmin = %d, xmax = %d, ymin = %d, ymax = %d\n",xmin,xmax,ymin,ymax);
		if (strcmp(name,"mxt224_ts_input")==0)
		{
			touch_mxt224_ts_input = true;
			printf("found mxt224_ts_input\n");
		}
		return;
	}
	if(use_uinput_mouse)
		return;
	for (i=0; i<50; i++)
	{
		char name[256]="Unknown";
		if (i < 10)
		{
			touch_device[sizeof(touch_device)-3] = '0'+(char)(i);
			touch_device[sizeof(touch_device)-2] = 0;
		}
		else
		{
			touch_device[sizeof(touch_device)-3] = '0'+(char)(i/10);
			touch_device[sizeof(touch_device)-2] = '0'+(char)(i%10);
			touch_device[sizeof(touch_device)-1] = 0;
		}
		struct input_absinfo info;
		if((touchfd = open(touch_device, O_RDWR)) == -1)
		{
			continue;
		}
		printf("searching for touch device, opening %s ... ",touch_device);
		if (ioctl(touchfd, EVIOCGNAME(sizeof(name)),name) < 0)
		{
			printf("failed, no name\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		printf("device name is %s\n",name);
		// Get the Range of X and Y
		if(ioctl(touchfd, EVIOCGABS(ABS_X), &info))
		{
			printf("failed, no ABS_X\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		xmin = info.minimum;
		xmax = info.maximum;
		if (xmin == 0 && xmax == 0)
		{
			if(ioctl(touchfd, EVIOCGABS(53), &info))
			{
				printf("failed, no ABS_X\n");
				close(touchfd);
				touchfd = -1;
				continue;
			}
			xmin = info.minimum;
			xmax = info.maximum;
		}

		if(ioctl(touchfd, EVIOCGABS(ABS_Y), &info)) {
			printf("failed, no ABS_Y\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		ymin = info.minimum;
		ymax = info.maximum;
		if (ymin == 0 && ymax == 0)
		{
			if(ioctl(touchfd, EVIOCGABS(54), &info))
			{
				printf("failed, no ABS_Y\n");
				close(touchfd);
				touchfd = -1;
				continue;
			}
			ymin = info.minimum;
			ymax = info.maximum;
		}
		bool t = contains(name,"touch");
		bool tk = contains(name,"touchkey");
		if (t && !tk)
			printf("there is \"touch\", but not \"touchkey\" in the name\n");
		if (!(t && !tk) && (xmin < 0 || xmin == xmax))	// xmin < 0 for the compass
		{
			printf("failed, xmin<0 || xmin==xmax\n");
			close(touchfd);
			touchfd = -1;
			continue;
		}
		printf("success2\n");
#ifdef ANDROID
		__android_log_print(ANDROID_LOG_INFO,"Webkey C++","using touch device: %s",touch_device);
#endif
		printf("xmin = %d, xmax = %d, ymin = %d, ymax = %d\n",xmin,xmax,ymin,ymax);
//		if (strcmp(name,"qt602240_ts_input")==0)
//			disable_mouse_pointer = true;
		if (strcmp(name,"mxt224_ts_input")==0)
		{
			touch_mxt224_ts_input = true;
			printf("found mxt224_ts_input\n");
		}
		return;
	}
	use_uinput_mouse = true;
}

static void
adjust_light(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
    if (ri->permissions != PERM_ROOT)
	    return;
    lock_wakelock();
    send_ok(conn);

    int i = getnum(ri->uri+14);
    int fd = open("/sys/class/leds/lcd-backlight/brightness", O_WRONLY);
    if (fd < 0)
	    fd = open("/sys/class/backlight/pwm-backlight/brightness", O_WRONLY);
    if(fd < 0)
        return;

    char value[20];
    int n = sprintf(value, "%d\n", i*max_brightness/256);
    write(fd, value, n);
    close(fd);
}
static void
waitdiff(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT && (ri->permissions&PERM_SCREENSHOT)==0)
		return;
	if (exit_flag)
		return;
	send_ok(conn);
//	printf("connected\n");
	if (!picchanged)
	{
//	printf("start\n");
		pthread_mutex_lock(&diffmutex);
		if (!exit_flag)
			pthread_cond_wait(&diffcond,&diffmutex);
		pthread_mutex_unlock(&diffmutex);
//	printf("end\n");
	}
//	printf("respond\n");
	mg_printf(conn,"changed");
}
void update_image(int orient,int lowres, bool png, bool flip, bool reread)
{
//	printf("update image\n");
//	fflush(NULL);
	if ((fbfd < 0 && screencap == -1) || scrinfo.yres == 0)
		return;
	if (fbfd >= 0)
	{
		if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0)
		{
			FILE* fp;
			if (png)
				fp = fo("tmp.png", "wb");
			else
				fp = fo("tmp.jpg", "wb");
			fclose(fp);
			return;
		}
		if (scrinfo.xres_virtual == 240) //for x10 mini pro
		       scrinfo.xres_virtual = 256;
		if (force_240)
			scrinfo.xres_virtual = scrinfo.xres = 240;
		if (force_544)
			scrinfo.xres_virtual = 544;
		init_fb_for_test();
	}
//	{
//	int i;
//	for (i = 0; i < sizeof(fb_var_screeninfo); i++)
//	{
//		printf("%d, ",((char*)&scrinfo)[i]);
//	}
//	printf("\n");
//	}

	FILE            *fp;
	png_structp     png_ptr;
	png_infop       info_ptr;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	std::string path = "tmp";

	if (png)
	{
		path = path + ".png";
		fp = fo(path.c_str(), "wb");
		if (!fp)
			return;
		png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		info_ptr = png_create_info_struct(png_ptr);
		png_init_io(png_ptr, fp);
		png_set_compression_level(png_ptr, 1);
		if (orient == 0)
			png_set_IHDR(png_ptr, info_ptr, scrinfo.xres>>lowres, scrinfo.yres>>lowres,
				8,//  scrinfo.bits_per_pixel,
				PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
		else
			png_set_IHDR(png_ptr, info_ptr, scrinfo.yres>>lowres, scrinfo.xres>>lowres,
				8,//  scrinfo.bits_per_pixel,
				PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	}
	else
	{
		path = path + ".jpg";
		fp = fo(path.c_str(), "wb");
		if (!fp)
			return;
		JSAMPROW row_pointer[1];
		cinfo.err = jpeg_std_error( &jerr );
		jpeg_create_compress(&cinfo);
		jpeg_stdio_dest(&cinfo, fp);
		if (orient == 0)
		{
			cinfo.image_width = scrinfo.xres>>lowres;
			cinfo.image_height = scrinfo.yres>>lowres;
		}
		else
		{
			cinfo.image_width = scrinfo.yres>>lowres;
			cinfo.image_height = scrinfo.xres>>lowres;
		}
		cinfo.input_components = 3;
		cinfo.in_color_space = JCS_RGB;
		jpeg_set_defaults( &cinfo );
		jpeg_set_quality(&cinfo, 40, TRUE);
		jpeg_start_compress( &cinfo, TRUE );
	}



	int rr = scrinfo.red.offset;
	int rl = 8-scrinfo.red.length;
	int gr = scrinfo.green.offset;
	int gl = 8-scrinfo.green.length;
	int br = scrinfo.blue.offset;
	int bl = 8-scrinfo.blue.length;
	if (is_reverse_colors)
	{
		rr = 24-rr;
		gr = 24-gr;
		br = 24-br;
	}
	int j;
//	printf("rr=%d, rl=%d, gr=%d, gl=%d, br=%d, bl=%d\n",rr,rl,gr,gl,br,bl);


	int x = scrinfo.xres_virtual;
	int xpic = scrinfo.xres;
	int y = scrinfo.yres;
	int m = scrinfo.yres*scrinfo.xres_virtual;
	int mpic = scrinfo.yres*scrinfo.xres;
	int k;
	int i = 0;
	int p = 0;
	int offset = scrinfo.yoffset*scrinfo.xres_virtual+scrinfo.xoffset;
	if (lowest_offset > offset)
		lowest_offset = offset;
	if (firstfb)
		offset = lowest_offset;


	if (reread)
	{

	//	int poffset = (scrinfo.yoffset*x+scrinfo.xoffset);
	//	printf("%d\n",poffset);
	//	int p = poffset;
	//	int i = 0;

	//	printf("before read\n");fflush(NULL);
		pthread_mutex_lock(&fbmutex);
		if (fbfd >= 0)
		{
			memcpy(copyfb,(char*)fbmmap+offset*bytespp,m*bytespp);
	//		FILE* f = fopen("/sdcard/fb0","rb");
	//		printf("%d, %d\n",m,bytespp);
	//		fflush(NULL);
	//		fread(copyfb, bytespp, m, f);
	//		printf("here\n");
	//		fflush(NULL);
	//		fclose(f);
		}
		else
		{
			FILE* sc;
			fflush(NULL);
	//		int i;
	//		for(i = 0; screencapbinaries[i].name!=NULL; i++)
	//		{
				if ((sc = mypopen(screencapbinaries[screencap].name,"r"))==NULL)
				{
					pthread_mutex_unlock(&fbmutex);
					return;
				}
				char temp[1024];
				if (fread(temp,1,screencap_skipbytes,sc) != screencap_skipbytes)
				{
					mypclose(sc);
					pthread_mutex_unlock(&fbmutex);
					return;
	//				continue;
				}
	//			printf("update_image read %d*%d bytes\n",
				fread(copyfb,bytespp,m,sc);
	//			,bytespp);
				mypclose(sc);
	//		}
		}
	} // if(reread)
	offset=0;
	{
		int s = m*bytespp/4/157;
		unsigned int* map = ((unsigned int*)copyfb)+(offset*bytespp/4);
		for (i=0; i < s; i++)
		{
			lastpic[22*i+0] = map[157*i+0];
			lastpic[22*i+1] = map[157*i+7];
			lastpic[22*i+2] = map[157*i+14];
			lastpic[22*i+3] = map[157*i+21];
			lastpic[22*i+4] = map[157*i+28];
			lastpic[22*i+5] = map[157*i+35];
			lastpic[22*i+6] = map[157*i+42];
			lastpic[22*i+7] = map[157*i+49];
			lastpic[22*i+8] = map[157*i+56];
			lastpic[22*i+9] = map[157*i+63];
			lastpic[22*i+10] = map[157*i+70];
			lastpic[22*i+11] = map[157*i+77];
			lastpic[22*i+12] = map[157*i+84];
			lastpic[22*i+13] = map[157*i+91];
			lastpic[22*i+14] = map[157*i+98];
			lastpic[22*i+15] = map[157*i+105];
			lastpic[22*i+16] = map[157*i+112];
			lastpic[22*i+17] = map[157*i+119];
			lastpic[22*i+18] = map[157*i+126];
			lastpic[22*i+19] = map[157*i+133];
			lastpic[22*i+20] = map[157*i+140];
			lastpic[22*i+21] = map[157*i+147];
		}
		picchanged = false;
	}
	if (flip == false)
	{
		i=0;
		if (lowres) // I use if outside the for, maybe it's faster this way
		{
			int rm = 0; for (k = 0; k < scrinfo.red.length; k++) rm = rm*2+1;
			int gm = 0; for (k = 0; k < scrinfo.green.length; k++) gm = gm*2+1;
			int bm = 0; for (k = 0; k < scrinfo.blue.length; k++) bm = bm*2+1;
			if (bytespp == 2) //16 bit
			{
				rl -= 2;
				gl -= 2;
				bl -= 2;
				unsigned short int* map = ((unsigned short int*)copyfb)+offset;
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j+=2)
					{
						for (k = 0; k < xpic; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p+1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x+1]>>rr)&rm))<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p+1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x+1]>>gr)&gm))<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p+1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x+1]>>br)&bm))<<bl;
							p+=2;
						}
						p+=x-xpic;
						p+=x;
					}
				}
				else //horizontal
				{
					for (j = 0; j < xpic; j+=2)
					{
						p = (xpic-j-1);//+poffset;
						for (k = 0; k < y; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p-1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x-1]>>rr)&rm))<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p-1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x-1]>>gr)&gm))<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p-1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x-1]>>br)&bm))<<bl;
							p += 2*x;
						}
					}
				}
			}
			if (bytespp == 4) //32 bit
			{
				unsigned int* map = ((unsigned int*)copyfb)+offset;
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j+=2)
					{
						for (k = 0; k < xpic; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p+1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x+1]>>rr)&rm))>>2<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p+1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x+1]>>gr)&gm))>>2<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p+1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x+1]>>br)&bm))>>2<<bl;
							p+=2;
						}
						p+=x-xpic;
						p+=x;
					}
				}
				else //horizontal
				{
					for (j = 0; j < xpic; j+=2)
					{
						p = (xpic-j-1);// + poffset;
						for (k = 0; k < y; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p-1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x-1]>>rr)&rm))>>2<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p-1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x-1]>>gr)&gm))>>2<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p-1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x-1]>>br)&bm))>>2<<bl;
//							pict[i++] = ((map[p]&255)+(map[p-1]&255)+(map[p+x]&255)+(map[p+x-1]&255))>>2;
//							pict[i++] = (((map[p]>>8)&255)+((map[p-1]>>8)&255)+((map[p+x]>>8)&255)+((map[p+x-1]>>8)&255))>>2;
//							pict[i++] = (((map[p]>>16)&255)+((map[p-1]>>16)&255)+((map[p+x]>>16)&255)+((map[p+x-1]>>16)&255))>>2;
							p += 2*x;
						}
					}
				}
			}
		}
		else //hires
		{
			if (bytespp == 2)
			{
				unsigned short int* map = ((unsigned short int*)copyfb)+offset;
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j++)
					{
						for (k = 0; k < xpic; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p++]>>br<<bl;
						}
						p+=x-xpic;
					}
				}
				else //horizontal
				{
					for (j = 0; j < xpic; j++)
					{
						p = (xpic-j-1);// + poffset;
						for (k = 0; k < y; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p]>>br<<bl;
							p += x;
						}
					}
				}
			}
			if (bytespp == 4)
			{
				unsigned int* map = ((unsigned int*)copyfb)+offset;
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j++)
					{
						for (k = 0; k < xpic; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p++]>>br<<bl;
						}
						p+=x-xpic;
					}
				}
				else //horizontal
				{
					for (j = 0; j < xpic; j++)
					{
						p = (xpic-j-1);// + poffset;
						for (k = 0; k < y; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p]>>br<<bl;
							p += x;
						}
					}
				}
			}
		}
	}
	else //flip == true
	{
		i=3*(scrinfo.xres>>lowres)*(scrinfo.yres>>lowres)-3;
		if (lowres) // I use if outside the for, maybe it's faster this way
		{
			int rm = 0; for (k = 0; k < scrinfo.red.length; k++) rm = rm*2+1;
			int gm = 0; for (k = 0; k < scrinfo.green.length; k++) gm = gm*2+1;
			int bm = 0; for (k = 0; k < scrinfo.blue.length; k++) bm = bm*2+1;
			if (bytespp == 2) //16 bit
			{
				rl -= 2;
				gl -= 2;
				bl -= 2;
				unsigned short int* map = ((unsigned short int*)copyfb)+offset;
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j+=2)
					{
						for (k = 0; k < xpic; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p+1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x+1]>>rr)&rm))<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p+1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x+1]>>gr)&gm))<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p+1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x+1]>>br)&bm))<<bl;
							i -= 6;
							p+=2;
						}
						p+=x-xpic;
						p+=x;
					}
				}
				else //horizontal
				{
					for (j = 0; j < xpic; j+=2)
					{
						p = (xpic-j-1);//+poffset;
						for (k = 0; k < y; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p-1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x-1]>>rr)&rm))<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p-1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x-1]>>gr)&gm))<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p-1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x-1]>>br)&bm))<<bl;
							i -= 6;
							p += 2*x;
						}
					}
				}
			}
			if (bytespp == 4) //32 bit
			{
				unsigned int* map = ((unsigned int*)copyfb)+offset;
				// I assume that each color have 1 byte.
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j+=2)
					{
						for (k = 0; k < xpic; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p+1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x+1]>>rr)&rm))>>2<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p+1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x+1]>>gr)&gm))>>2<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p+1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x+1]>>br)&bm))>>2<<bl;
//							pict[i++] = ((map[p]&255)+(map[p+1]&255)+(map[p+x]&255)+(map[p+x+1]&255))>>2;
//							pict[i++] = (((map[p]>>8)&255)+((map[p+1]>>8)&255)+((map[p+x]>>8)&255)+((map[p+x+1]>>8)&255))>>2;
//							pict[i++] = (((map[p]>>16)&255)+((map[p+1]>>16)&255)+((map[p+x]>>16)&255)+((map[p+x+1]>>16)&255))>>2;
							i -= 6;
							p+=2;
						}
						p+=x-xpic;
						p+=x;
					}
				}
				else //horizontal
				{
				// I assume that each color have 1 byte.
					for (j = 0; j < xpic; j+=2)
					{
						p = (xpic-j-1);// + poffset;
						for (k = 0; k < y; k+=2)
						{
							pict[i++] = (((map[p]>>rr)&rm)+((map[p-1]>>rr)&rm)+((map[p+x]>>rr)&rm)+((map[p+x-1]>>rr)&rm))>>2<<rl;
							pict[i++] = (((map[p]>>gr)&gm)+((map[p-1]>>gr)&gm)+((map[p+x]>>gr)&gm)+((map[p+x-1]>>gr)&gm))>>2<<gl;
							pict[i++] = (((map[p]>>br)&bm)+((map[p-1]>>br)&bm)+((map[p+x]>>br)&bm)+((map[p+x-1]>>br)&bm))>>2<<bl;
//							pict[i++] = ((map[p]&255)+(map[p-1]&255)+(map[p+x]&255)+(map[p+x-1]&255))>>2;
//							pict[i++] = (((map[p]>>8)&255)+((map[p-1]>>8)&255)+((map[p+x]>>8)&255)+((map[p+x-1]>>8)&255))>>2;
//							pict[i++] = (((map[p]>>16)&255)+((map[p-1]>>16)&255)+((map[p+x]>>16)&255)+((map[p+x-1]>>16)&255))>>2;
							i -= 6;
							p += 2*x;
						}
					}
				}
			}
		}
		else //hires
		{
			if (bytespp == 2)
			{
				unsigned short int* map = ((unsigned short int*)copyfb)+offset;
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j++)
					{
						for (k = 0; k < xpic; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p++]>>br<<bl;
							i -= 6;
						}
						p+=x-xpic;
					}
				}
				else //horizontal
				{
					for (j = 0; j < xpic; j++)
					{
						p = (xpic-j-1);// + poffset;
						for (k = 0; k < y; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p]>>br<<bl;
							i -= 6;
							p += x;
						}
					}
				}
			}
			if (bytespp == 4)
			{
				unsigned int* map = ((unsigned int*)copyfb)+offset;
				if (orient == 0) //vertical
				{
					for (j = 0; j < y; j++)
					{
						for (k = 0; k < xpic; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p++]>>br<<bl;
							i -= 6;
						}
						p+=x-xpic;
					}
				}
				else //horizontal
				{
					for (j = 0; j < xpic; j++)
					{
						p = (xpic-j-1);// + poffset;
						for (k = 0; k < y; k++)
						{
							pict[i++] = map[p]>>rr<<rl;
							pict[i++] = map[p]>>gr<<gl;
							pict[i++] = map[p]>>br<<bl;
							i -= 6;
							p += x;
						}
					}
				}
			}
		}
	}
	pthread_mutex_unlock(&fbmutex);
	if (orient == 0)
	{
		for (i = 0; i < y>>lowres; i++)
			graph[i] = pict+(i*xpic*3>>lowres);
	}
	else
	{
		for (i = 0; i < x>>lowres; i++)
			graph[i] = pict+(i*y*3>>lowres);
	}

	if (png)
	{
		png_write_info(png_ptr, info_ptr);
		png_write_image(png_ptr, graph);
		png_write_end(png_ptr, info_ptr);
		png_destroy_write_struct(&png_ptr, &info_ptr);
	}
	else
	{
		i = 0;
		while( cinfo.next_scanline < cinfo.image_height )
		{
			jpeg_write_scanlines( &cinfo, graph+i++, 1 );
		}
		jpeg_finish_compress( &cinfo );
		jpeg_destroy_compress( &cinfo );
	}
	fclose(fp);

//	pthread_mutex_lock(&diffmutex);
//	pthread_cond_broadcast(&diffstartcond);
//	pthread_mutex_unlock(&diffmutex);
}

static
void* watchscreen(void* param)
{
	if (fbfd < 0 && screencap == -1)
		return 0;
	while (1)
	{
		if (exit_flag)
			return NULL;
		pthread_mutex_lock(&diffmutex);
		pthread_cond_wait(&diffstartcond,&diffmutex);
		pthread_mutex_unlock(&diffmutex);
		int l = 0;
		int i;
		//picchanged = false;
		while(1)
		{
			if (exit_flag)
				return NULL;
			usleep(10000);
//			printf("%d\n",l);
			pthread_mutex_lock(&fbmutex);
			if (fbfd >= 0)
			{
				if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0)
				{
					pthread_mutex_unlock(&fbmutex);
					usleep(100000);
					continue;
				}
				if (scrinfo.xres_virtual == 240) //for x10 mini pro
					scrinfo.xres_virtual = 256;
				if (force_240)
					scrinfo.xres_virtual = scrinfo.xres = 240;
				if (force_544)
					scrinfo.xres_virtual = 544;
				init_fb_for_test();
			}
			int m = scrinfo.yres*scrinfo.xres;
			int offset = scrinfo.yoffset*scrinfo.xres_virtual+scrinfo.xoffset;
			if (lowest_offset > offset)
				lowest_offset = offset;
			if (firstfb)
				offset = lowest_offset;
			if (pict && lastpic);
			{
				int s = m*bytespp/4/157;
				unsigned int* map;
				if (fbfd >=0)
					memcpy(copyfb,(char*)fbmmap+offset*bytespp,m*bytespp);
					//map = ((unsigned int*)fbmmap)+(offset*bytespp/4);
				else
				{
					FILE* sc;
					if ((sc = mypopen(screencapbinaries[screencap].name,"r"))==NULL)
					{
						pthread_mutex_unlock(&fbmutex);
						usleep(100000);
						continue;
					}
					char temp[1024];
					if (fread(temp,1,screencap_skipbytes,sc) != screencap_skipbytes)
					{
						mypclose(sc);
		//				return NULL;
						pthread_mutex_unlock(&fbmutex);
						usleep(100000);
						continue;
					}
					fread(copyfb,bytespp,m,sc);
					mypclose(sc);
				}
				map = (unsigned int*)copyfb;
				int start = 0;
				int end = s;
				if (lastorient == 0)
				{
					if (lastflip)
						end = s*19/20;
					else
						start = s/20;
				}
				int rowlength = scrinfo.xres*bytespp/4;
				for (i=start; i < end; i++)
				{
					if (lastpic[22*i+0] != map[157*i+0] ||
					lastpic[22*i+1] != map[157*i+7] ||
					lastpic[22*i+2] != map[157*i+14] ||
					lastpic[22*i+3] != map[157*i+21] ||
					lastpic[22*i+4] != map[157*i+28] ||
					lastpic[22*i+5] != map[157*i+35] ||
					lastpic[22*i+6] != map[157*i+42] ||
					lastpic[22*i+7] != map[157*i+49] ||
					lastpic[22*i+8] != map[157*i+56] ||
					lastpic[22*i+9] != map[157*i+63] ||
					lastpic[22*i+10] != map[157*i+70] ||
					lastpic[22*i+11] != map[157*i+77] ||
					lastpic[22*i+12] != map[157*i+84] ||
					lastpic[22*i+13] != map[157*i+91] ||
					lastpic[22*i+14] != map[157*i+98] ||
					lastpic[22*i+15] != map[157*i+105] ||
					lastpic[22*i+16] != map[157*i+112] ||
					lastpic[22*i+17] != map[157*i+119] ||
					lastpic[22*i+18] != map[157*i+126] ||
					lastpic[22*i+19] != map[157*i+133] ||
					lastpic[22*i+20] != map[157*i+140] ||
					lastpic[22*i+21] != map[157*i+147])
					{
//						printf("CHANGED %d\n",i);
//						fflush(NULL);
						picchanged = true;
						pthread_mutex_lock(&diffmutex);
						diffcondDiffCounter++;
						pthread_cond_broadcast(&diffcond);
						pthread_mutex_unlock(&diffmutex);
						break;
					}
				}
			}
			pthread_mutex_unlock(&fbmutex);
			if (picchanged)
				break;

			l++;
			if (l > 600)
				break;
			usleep(150000);
		}
		if (picchanged == false)
		{
			pthread_mutex_lock(&diffmutex);
			diffcondDiffCounter++;
			pthread_cond_broadcast(&diffcond);
			pthread_mutex_unlock(&diffmutex);
		}
	}
}

static void
emptyresponse(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
}
static char *jsonEscape(const char *buf, int len);

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
	int i;
	struct mg_connection* conn = (struct mg_connection*)NotUsed;
	if (!conn)
		return 0;
	mg_printf(conn,"{");
	for(i=0; i<argc; i++)
	{
		if (argv[i])
		{
			char* t = jsonEscape(argv[i],strlen(argv[i]));
			mg_printf(conn,"\"%s\" : \"%s\"", azColName[i], t);
			delete[] t;
		}
		else
			mg_printf(conn,"\"%s\" : \"%s\"", azColName[i], "NULL");

		if (i<argc-1)
			mg_printf(conn,", ");
	}
	mg_printf(conn,"},\n");
	return 0;
}



static void
touch(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
//	printf("%s\n",ri->uri);
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"touch inject");
	char* s = ri->uri;
	int n = 0;
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	int i = 0;
	if (post_data_len == 0)
	{
		n = strlen(ri->uri);
		if (n<8)
			return;
		i = 7;
	}
	else
	{
		n = post_data_len;
		s = post_data;
		i = 0;
	}
	int orient = 0;
	if(use_uinput_mouse == false && (touchfd == -1 || scrinfo.xres == 0 || scrinfo.yres == 0))
		return;
//	printf("%s\n",s+i);
	while(s[i] == '_')
		i++;

	while(i < n && s[i])
	{
		if (s[i]=='h')
			orient = 1;
		i++;
		int x = getnum(s+i);
		while (i<n && s[i++]!='_');
		int y = getnum(s+i);
		while (i<n && s[i++]!='_');
		int down = getnum(s+i);
		//while (i<n && (ri->uri[i] > '9' || ri->uri[i] < '0') ) i++;
		while (i<n && s[i++]!='_');
		struct input_event ev;

		//printf("%d. injectTouch x=%d, y=%d, down=%d\n", touchcount++, x, y, down);
		//fflush(NULL);
		if (orient)
		{
			int t = x;
			x = scrinfo.xres-y;
			y = t;
		}
		if (x < 0) x = 0;
		if (y < 0) y = 0;
		if (x > scrinfo.xres) x = scrinfo.xres;
		if (y > scrinfo.yres) y = scrinfo.yres;
		if (flip_touch)
		{
			x = scrinfo.xres - x;
			y = scrinfo.yres - y;
		}
		// Calculate the final x and y
		if (use_uinput_mouse)
		{
			xmin = -2047;
			xmax = 2048;
			ymin = -2047;
			ymax = 2048;
		}
		int xx, yy;
		if (rotate_touch)
		{
			int t = y;
			yy = ymin + (x * (ymax - ymin)) / (scrinfo.xres);
			xx = xmax - (t * (xmax - xmin)) / (scrinfo.yres);
		}
		else
		{
			xx = xmin + (x * (xmax - xmin)) / (scrinfo.xres);
			yy = ymin + (y * (ymax - ymin)) / (scrinfo.yres);
		}

		memset(&ev, 0, sizeof(ev));

//		if (touch_mxt224_ts_input)
		if (use_uinput_mouse)
		{
			if (uinput_fd == -1)
			{
				init_uinput();
				shutdownkey_up = up+100;
				if (geniatech)
					shutdownkey_up+=3600;
			}
			if (uinput_fd == -1)
				return;
			suinput_mouse(uinput_fd, xx, yy, (int32_t)down);
			usleep(1000);
			continue;
		}
		if (is_icecreamsandwich)
		{
			// Then send the X
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 53;//ABS_X;
			ev.value = xx;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");
			// Then send the Y
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 54;//ABS_Y;
			ev.value = yy;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");

			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 58;
			ev.value = down>0?100:0;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");

			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 48;
			ev.value = down>0?1:0;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");


					gettimeofday(&ev.time,0);
					ev.type = EV_KEY; //1
					ev.code = BTN_TOUCH; //330
					ev.value = down;//>0?1:0;
					if(write(touchfd, &ev, sizeof(ev)) < 0)
						printf("touchfd write failed.\n");
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 57; //0
			ev.value = 0;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");
		}
		else
		{
			// non ice cream sandwich
			// Then send a BTN_TOUCH
			if (disable_mouse_pointer == false)
			{
				if (down != 2)
				{
					gettimeofday(&ev.time,0);
					ev.type = EV_KEY; //1
					ev.code = BTN_TOUCH; //330
					ev.value = down;//>0?1:0;
					if(write(touchfd, &ev, sizeof(ev)) < 0)
						printf("touchfd write failed.\n");
				}
			}
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 48;
			ev.value = down>0?100:0;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 50;
			ev.value = down>0?1:0;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");
			// Then send the X
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = ABS_X; //0
			ev.value = xx;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");
			// Then send the X
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 53;//ABS_X;
			ev.value = xx;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");

			// Then send the Y
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = ABS_Y; //1
			ev.value = yy;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");
			// Then send the Y
			gettimeofday(&ev.time,0);
			ev.type = EV_ABS; //3
			ev.code = 54;//ABS_Y;
			ev.value = yy;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");

			gettimeofday(&ev.time,0);
			ev.type = EV_SYN;//0
			ev.code = 2;
			ev.value = 0;
			if(write(touchfd, &ev, sizeof(ev)) < 0)
				printf("touchfd write failed.\n");
			}
		// Finally send the SYN
		gettimeofday(&ev.time,0);
		ev.type = EV_SYN;//0
		ev.code = 0;
		ev.value = 0;
		if(write(touchfd, &ev, sizeof(ev)) < 0)
			printf("touchfd write failed.\n");
		usleep(1000);

	}
	send_ok(conn);
	if (post_data)
		delete[] post_data;
}
static void
qwerty_press(int fd, int key)
{
	if(key=='q')
		suinput_click(fd,16);
	else if(key=='Q')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,16);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='w')
		suinput_click(fd,17);
	else if(key=='W')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,17);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='e')
		suinput_click(fd,18);
	else if(key=='E')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,18);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='r')
		suinput_click(fd,19);
	else if(key=='R')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,19);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='t')
		suinput_click(fd,20);
	else if(key=='T')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,20);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='y')
		suinput_click(fd,21);
	else if(key=='Y')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,21);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='u')
		suinput_click(fd,22);
	else if(key=='U')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,22);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='i')
		suinput_click(fd,23);
	else if(key=='I')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,23);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='o')
		suinput_click(fd,24);
	else if(key=='O')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,24);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='p')
		suinput_click(fd,25);
	else if(key=='P')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,25);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='a')
		suinput_click(fd,30);
	else if(key=='A')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,30);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='s')
		suinput_click(fd,31);
	else if(key=='S')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,31);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='d')
		suinput_click(fd,32);
	else if(key=='D')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,32);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='f')
		suinput_click(fd,33);
	else if(key=='F')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,33);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='g')
		suinput_click(fd,34);
	else if(key=='G')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,34);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='h')
		suinput_click(fd,35);
	else if(key=='H')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,35);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='j')
		suinput_click(fd,36);
	else if(key=='J')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,36);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='k')
		suinput_click(fd,37);
	else if(key=='K')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,37);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='l')
		suinput_click(fd,38);
	else if(key=='L')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,38);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='z')
		suinput_click(fd,44);
	else if(key=='Z')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,44);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='x')
		suinput_click(fd,45);
	else if(key=='X')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,45);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='c')
		suinput_click(fd,46);
	else if(key=='C')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,46);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='v')
		suinput_click(fd,47);
	else if(key=='V')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,47);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='b')
		suinput_click(fd,48);
	else if(key=='B')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,48);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='n')
		suinput_click(fd,49);
	else if(key=='N')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,49);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='m')
		suinput_click(fd,50);
	else if(key=='M')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,50);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='-')
		suinput_click(fd,12);
	else if(key=='_')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,12);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='=')
		suinput_click(fd,13);
	else if(key=='+')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,13);
		suinput_release(fd, 42); //left shift
	}
	else if(key==-8)
		suinput_click(fd,14);
	else if(key=='[')
		suinput_click(fd,26);
	else if(key=='{')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,26);
		suinput_release(fd, 42); //left shift
	}
	else if(key==']')
		suinput_click(fd,27);
	else if(key=='}')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,27);
		suinput_release(fd, 42); //left shift
	}
	else if(key==13)
		suinput_click(fd,28);
	else if(key==';')
		suinput_click(fd,39);
	else if(key==':')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,39);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='\'')
		suinput_click(fd,40);
	else if(key=='"')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,40);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='`')
		suinput_click(fd,41);
	else if(key=='~')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,41);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='\\')
		suinput_click(fd,43);
	else if(key=='|')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,43);
		suinput_release(fd, 42); //left shift
	}
	else if(key==',')
		suinput_click(fd,51);
	else if(key=='<')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,51);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='.')
		suinput_click(fd,52);
	else if(key=='>')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,52);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='/')
		suinput_click(fd,53);
	else if(key=='?')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,53);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='*')
		suinput_click(fd,55);
	else if(key==' ')
		suinput_click(fd,57);
	else if(key=='7')
		suinput_click(fd,71);
	else if(key=='8')
		suinput_click(fd,72);
	else if(key=='9')
		suinput_click(fd,73);
	else if(key=='4')
		suinput_click(fd,75);
	else if(key=='5')
		suinput_click(fd,76);
	else if(key=='6')
		suinput_click(fd,77);
	else if(key=='1')
		suinput_click(fd,79);
	else if(key=='2')
		suinput_click(fd,80);
	else if(key=='3')
		suinput_click(fd,81);
	else if(key=='0')
		suinput_click(fd,82);
	//printf("%d\n",key);

}
// for geniatech
static void
qwerty_press_usb(int fd, int key)
{
	if(key=='q')
		suinput_click(fd,16);
	else if(key=='Q')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,16);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='w')
		suinput_click(fd,17);
	else if(key=='W')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,17);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='e')
		suinput_click(fd,18);
	else if(key=='E')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,18);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='r')
		suinput_click(fd,19);
	else if(key=='R')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,19);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='t')
		suinput_click(fd,20);
	else if(key=='T')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,20);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='y')
		suinput_click(fd,21);
	else if(key=='Y')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,21);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='u')
		suinput_click(fd,22);
	else if(key=='U')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,22);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='i')
		suinput_click(fd,23);
	else if(key=='I')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,23);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='o')
		suinput_click(fd,24);
	else if(key=='O')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,24);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='p')
		suinput_click(fd,25);
	else if(key=='P')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,25);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='a')
		suinput_click(fd,30);
	else if(key=='A')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,30);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='s')
		suinput_click(fd,31);
	else if(key=='S')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,31);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='d')
		suinput_click(fd,32);
	else if(key=='D')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,32);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='f')
		suinput_click(fd,33);
	else if(key=='F')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,33);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='g')
		suinput_click(fd,34);
	else if(key=='G')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,34);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='h')
		suinput_click(fd,35);
	else if(key=='H')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,35);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='j')
		suinput_click(fd,36);
	else if(key=='J')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,36);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='k')
		suinput_click(fd,37);
	else if(key=='K')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,37);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='l')
		suinput_click(fd,38);
	else if(key=='L')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,38);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='z')
		suinput_click(fd,44);
	else if(key=='Z')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,44);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='x')
		suinput_click(fd,45);
	else if(key=='X')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,45);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='c')
		suinput_click(fd,46);
	else if(key=='C')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,46);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='v')
		suinput_click(fd,47);
	else if(key=='V')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,47);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='b')
		suinput_click(fd,48);
	else if(key=='B')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,48);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='n')
		suinput_click(fd,49);
	else if(key=='N')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,49);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='m')
		suinput_click(fd,50);
	else if(key=='M')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,50);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='-')
		suinput_click(fd,12);
	else if(key=='_')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,12);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='=')
		suinput_click(fd,13);
	else if(key=='+')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,13);
		suinput_release(fd, 42); //left shift
	}
	else if(key==-8)
		suinput_click(fd,14);
	else if(key=='[')
		suinput_click(fd,26);
	else if(key=='{')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,26);
		suinput_release(fd, 42); //left shift
	}
	else if(key==']')
		suinput_click(fd,27);
	else if(key=='}')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,27);
		suinput_release(fd, 42); //left shift
	}
	else if(key==13)
		suinput_click(fd,28);
	else if(key==';')
		suinput_click(fd,39);
	else if(key==':')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,39);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='\'')
		suinput_click(fd,40);
	else if(key=='"')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,40);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='`')
		suinput_click(fd,41);
	else if(key=='~')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,41);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='\\')
		suinput_click(fd,43);
	else if(key=='|')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,43);
		suinput_release(fd, 42); //left shift
	}
	else if(key==',')
		suinput_click(fd,51);
	else if(key=='<')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,51);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='.')
		suinput_click(fd,52);
	else if(key=='>')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,52);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='/')
		suinput_click(fd,53);
	else if(key=='?')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,53);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='(')
		suinput_click(fd,179);
	else if(key==')')
		suinput_click(fd,180);
	else if(key=='*')
		suinput_click(fd,55);
	else if(key==' ')
		suinput_click(fd,57);
	else if(key=='1')
		suinput_click(fd,2);
	else if(key=='2')
		suinput_click(fd,3);
	else if(key=='3')
		suinput_click(fd,4);
	else if(key=='4')
		suinput_click(fd,5);
	else if(key=='5')
		suinput_click(fd,6);
	else if(key=='6')
		suinput_click(fd,7);
	else if(key=='7')
		suinput_click(fd,8);
	else if(key=='8')
		suinput_click(fd,9);
	else if(key=='9')
		suinput_click(fd,10);
	else if(key=='0')
		suinput_click(fd,11);
	else if(key=='!')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,2);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='@')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,3);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='#')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,4);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='$')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,5);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='%')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,6);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='^')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,7);
		suinput_release(fd, 42); //left shift
	}
	else if(key=='&')
	{
		suinput_press(fd, 42); //left shift
		suinput_click(fd,8);
		suinput_release(fd, 42); //left shift
	}
	//printf("%d\n",key);

}
static void
key(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"key inject");
	send_ok(conn);
	if (uinput_fd == -1 && geniatech == false)
	{
		shutdownkey_up = up+100;
		init_uinput();
	}
	bool no_uinput = false;
	if (uinput_fd == -1)
		no_uinput = true;
	bool sms_mode = false;
	int key = 0;
	bool old = false;
	int orient = 0;
//	printf("%s\n",ri->uri);
		// /oldkey_hn-22 -> -22 key with normal mode, horisontal
	int n = 0;
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	if (post_data_len == 0)
	{
		delete[] post_data;
		post_data = 0;
		if (startswith(ri->uri,"/oldkey") && strlen(ri->uri)>10)
		{
			n = 9;
			old = true;
		}
		else
		if (strlen(ri->uri)>9)
		{
			n = 9;
		}
		else
			return;
		if (ri->uri[n-2] == 'h')
			orient = 1;
		if (ri->uri[n-1] == 's')
			sms_mode = true;
	}
	else
	{
		if (strlen(ri->uri)==9 && ri->uri[7] == 'h')
			orient = 1;
		if (strlen(ri->uri)==9 && ri->uri[8] == 's')
			sms_mode = true;
//		for (int i =0;i < post_data_len;i++)
//			printf("%d, ",post_data[i]);
//		printf("\n");
	}
//	if (ri->uri[++n] == 0)
//		return;

	pthread_mutex_lock(&uinputmutex);
	while(1)
	{
		usleep(1000);
		if (post_data_len == 0)
		{
			while (ri->uri[n] != '_')
			{
			       if (ri->uri[++n] == 0)
			       {
				       pthread_mutex_unlock(&uinputmutex);
				       return;
			       }
			}
			if (ri->uri[++n] == 0)
			{
				pthread_mutex_unlock(&uinputmutex);
				return;
			}
			key = getnum(ri->uri+n);
			if (old && key < 0)
				key = -key;
		}
		else
		{
			if (n>=post_data_len)
			{
				pthread_mutex_unlock(&uinputmutex);
				return;
			}
			int q = 0;
			unsigned int w = post_data[n++];
			while (w&128) {	q++; w = w << 1; }
			key = (w&255)>>q;
//			printf("%d\n",key);
			while (q>1 && n < post_data_len)
			{
				key = (key<<6)+(post_data[n++]&63);
				q--;
			}
//			printf("%d\n",key);
		}
		if (key == 0) // I don't know how, but it happens
		{
			pthread_mutex_unlock(&uinputmutex);
			return;
		}
//		printf("KEY %d\n",key);
		if (no_uinput || geniatech)
		{
			char device[19] = "/dev/input/event0";
			for (int i=0; i<50; i++)
			{
				int fd;
				char name[256]="Unknown";
				if (i < 10)
				{
					device[sizeof(device)-3] = '0'+(char)(i);
					device[sizeof(device)-2] = 0;
				}
				else
				{
					device[sizeof(device)-3] = '0'+(char)(i/10);
					device[sizeof(device)-2] = '0'+(char)(i%10);
					device[sizeof(device)-1] = 0;
				}
				struct input_absinfo info;
				if((fd = open(device, O_RDWR)) == -1)
				{
					continue;
				}
				if (ioctl(fd, EVIOCGNAME(sizeof(name)),name) < 0)
					continue;
				if (check_type(name,"usb keyboard"))
				{
					qwerty_press_usb(fd, key);
					close(fd);
					break;
				}
				if (check_type(name,"aml_keypad"))
				{
					qwerty_press_usb(fd, key);
					close(fd);
					break;
				}
				close(fd);
			}
			continue;
		}
		if (geniatech)
		{
			if (uinput_fd == -1)
			{
				shutdownkey_up = up+100;
				init_uinput();
			}
			qwerty_press_usb(uinput_fd, key);
			continue;
		}
		if (is_icecreamsandwich && use_generic) // long story, don't ask
		{
//			printf("%d\n",key);
			qwerty_press(uinput_fd, key);
/*
*/
			continue;
		}
		int i;
		int j = 0;
		if (sms_mode)
			for (i=0; i < speckeys.size(); i++)
				if ((speckeys[i]->ajax == key || (old && speckeys[i]->ajax == -key)) && speckeys[i]->sms)
				{
//					printf("pressed: %d\n",speckeys[i]->kcm);
					suinput_press(uinput_fd, 57); //left alt
					if (speckeys[i]->kcm_sh)
						suinput_press(uinput_fd, 59); //left shift
					if (speckeys[i]->kcm)
						suinput_click(uinput_fd, speckeys[i]->kcm);
					suinput_release(uinput_fd, 57); //left alt
					if (speckeys[i]->kcm_sh)
						suinput_release(uinput_fd, 59); //left shift
					j++;
				}
		if (j)
			continue;
		if (!sms_mode || i == speckeys.size())
		{
			for (i=0; i < speckeys.size(); i++)
				if ((speckeys[i]->ajax == key|| (old && speckeys[i]->ajax == -key) ) && speckeys[i]->sms == false)
				{
//					printf("pressed: %d\n",speckeys[i]->kcm);
					suinput_press(uinput_fd, 57); //left alt
					if (speckeys[i]->kcm_sh)
						suinput_press(uinput_fd, 59); //left shift
					if (speckeys[i]->kcm)
						suinput_click(uinput_fd, speckeys[i]->kcm);
					suinput_release(uinput_fd, 57); //left alt
					if (speckeys[i]->kcm_sh)
						suinput_release(uinput_fd, 59); //left shift
					j++;
				}
		}
		if (j)
			continue;
		for (i=0; i < fastkeys.size(); i++)
			if (fastkeys[i]->ajax == key || (old && fastkeys[i]->ajax == -key))
			{
				int k = i+1;
				if (orient)
				{
					if (k == 19) //up -> right
						k = 22;
					else if (k == 20) //down -> left
						k = 21;
					else if (k == 21) //left -> up
						k = 19;
					else if (k == 22) //right -> down
						k = 20;
				}
				suinput_click(uinput_fd, k);
				j++;
			}
		if (j)
			continue;
		if (48<=key && key < 58)
		{
			suinput_click(uinput_fd, key-48+7);
			j++;
		}
		if (97<=key && key < 123)
		{
			suinput_click(uinput_fd, key-97+29);
			j++;
		}
		if (65<=key && key < 91)
		{
			suinput_press(uinput_fd, 59); //left shift
			suinput_click(uinput_fd, key-65+29);
			suinput_release(uinput_fd, 59); //left shift
			j++;
		}
		if (32<=key && key <= 47)
		{
			if (spec1sh[key-32]) suinput_press(uinput_fd, 59); //left shift
			suinput_click(uinput_fd, spec1[key-32]);
			if (spec1sh[key-32]) suinput_release(uinput_fd, 59); //left shift
			j++;
		}
		if (58<=key && key <= 64)
		{
			if (spec2sh[key-58]) suinput_press(uinput_fd, 59); //left shift
			suinput_click(uinput_fd, spec2[key-58]);
			if (spec2sh[key-58]) suinput_release(uinput_fd, 59); //left shift
			j++;
		}
		if (91<=key && key <= 96)
		{
			if (spec3sh[key-91]) suinput_press(uinput_fd, 59); //left shift
			suinput_click(uinput_fd, spec3[key-91]);
			if (spec3sh[key-91]) suinput_release(uinput_fd, 59); //left shift
			j++;
		}
		if (123<=key && key <= 127)
		{
			if (spec4sh[key-123]) suinput_press(uinput_fd, 59); //left shift
			suinput_click(uinput_fd, spec4[key-123]);
			if (spec4sh[key-123]) suinput_release(uinput_fd, 59); //left shift
			j++;
		}
		if (key == -8 || (old && key == 8))
		{
			suinput_click(uinput_fd, 67); //BACKSPACE -> DEL
			j++;
		}
		if (key == -13 || key == 13 || key == -10 || key == 10)
		{
			suinput_click(uinput_fd, 66); //ENTER
			j++;
		}
		if (j == 0)
		{
			BIND* load = new BIND;
			if (key > 0)
			{
				load->ajax = key;
				load->disp = key;
			}
			else
			{
				load->ajax = -key;
				load->disp = -key;
			}
			load->sms = 0;
			speckeys.push_back(load);
			FILE* sk = fo("spec_keys.txt","w");
			if (!sk)
				continue;
			int nrem = 0;
			if (speckeys.size() > 2*52)
				nrem = speckeys.size() - 2*52;;
			for(i=0; i <speckeys.size(); i++)
				if (speckeys[i]->ajax || speckeys[i]->disp)
				{
					if (nrem == 0 || speckeys[i]->ajax != speckeys[i]->disp || speckeys[i]->sms)
						fprintf(sk,"%d %d %d\n",speckeys[i]->sms,speckeys[i]->ajax,speckeys[i]->disp);
					else
						nrem -= 1;
				}
			fclose(sk);
			pthread_mutex_unlock(&uinputmutex);
			run_load_keys = true;
			init_uinput();
			shutdownkey_up = up+100;
			pthread_mutex_lock(&uinputmutex);
			for (i=0; i < speckeys.size(); i++)
				if ((speckeys[i]->ajax == key|| (old && speckeys[i]->ajax == -key) ))
				{
//					printf("pressed: %d\n",speckeys[i]->kcm);
					suinput_press(uinput_fd, 57); //left alt
					if (speckeys[i]->kcm_sh)
						suinput_press(uinput_fd, 59); //left shift
					if (speckeys[i]->kcm)
						suinput_click(uinput_fd, speckeys[i]->kcm);
					suinput_release(uinput_fd, 57); //left alt
					if (speckeys[i]->kcm_sh)
						suinput_release(uinput_fd, 59); //left shift
					j++;
				}
			}
	}
	pthread_mutex_unlock(&uinputmutex);
	if (post_data)
		delete[] post_data;

}


static void
savebuttons(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"modify buttons");
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	int i;
	for (i=0; i < fastkeys.size(); i++)
		fastkeys[i]->show = false;
	int n = post_data_len;
	i = 0;
	while(i<n)
	{
		if (startswith(post_data+i,"show_"))
		{
			i+=5;
			int id = getnum(post_data+i);
			if (id-1<fastkeys.size())
				fastkeys[id-1]->show = true;
//			printf("SHOW %d\n",id-1);
		}
		else if (startswith(post_data+i,"keycode_"))
		{
			i+=8;
			int id = getnum(post_data+i);
			while (i<n && post_data[i++]!='=');
			int ajax = getnum(post_data+i);
			if (id-1<fastkeys.size())
				fastkeys[id-1]->ajax = ajax;
		}
		while (i<n && post_data[i++]!='&');
	}

	FILE* fk = fo("fast_keys.txt","w");
	if (!fk)
		return;
	for (i=0;i<fastkeys.size();i++)
	{
		fprintf(fk,"%d %d %d\n",i+1,fastkeys[i]->show,fastkeys[i]->ajax);
	}
	fclose(fk);
	mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"0;url=phone.html\"></head><body>redirecting</body></html>");
	if (post_data)
		delete[] post_data;
}
static void
savekeys(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"modify keys");
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	int i;
	int n = post_data_len;
	for (i=0; i < speckeys.size(); i++)
		if (speckeys[i])
			delete speckeys[i];
	speckeys.clear();
	for (i=0; i < 106; i++)
	{
		BIND* load = new BIND;
		load->ajax = load->disp = load->sms = 0;
		speckeys.push_back(load);
	}
	i = 0;
	while(i<n)
	{
		if (startswith(post_data+i,"sms_"))
		{
			i+=4;
			int id = getnum(post_data+i);
			if (id-1<speckeys.size())
				speckeys[id-1]->sms = true;
		}
		else if (startswith(post_data+i,"keycode_"))
		{
			i+=8;
			int id = getnum(post_data+i);
			while (i<n && post_data[i++]!='=');
			int ajax = getnum(post_data+i);
			if (id-1<speckeys.size())
				speckeys[id-1]->ajax = ajax;
		}
		else if (startswith(post_data+i,"tokeycode_"))
		{
			i+=10;
			int id = getnum(post_data+i);
			while (i<n && post_data[i++]!='=');
			int disp = getnum(post_data+i);
			if (id-1<speckeys.size())
				speckeys[id-1]->disp = disp;
		}
		while (i<n && post_data[i++]!='&');
	}

	FILE* sk = fo("spec_keys.txt","w");
	if (!sk)
		return;
	for(i=0; i <speckeys.size(); i++)
		if (speckeys[i]->ajax || speckeys[i]->disp || speckeys[i]->disp)
			fprintf(sk,"%d %d %d\n",speckeys[i]->sms,speckeys[i]->ajax,speckeys[i]->disp);
	fclose(sk);
	run_load_keys = true;
	init_uinput();
	shutdownkey_up = up+100;
	mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"0;url=phone.html\"></head><body>redirecting</body></html>");
	if (post_data)
		delete[] post_data;
}

std::string lang(const mg_request_info* ri, const char *key)
{
	FILE* f;
	if (ri->language[0] == 0 && strlen(deflanguage) == 2)
		f = fopen((dir+"language_"+deflanguage+".txt").c_str(),"r");
	else if (ri->language[0] == 0)
	{
		return key;
	}
	else
		f = fopen((dir+"language_"+ri->language+".txt").c_str(),"r");
	char line[1024];

	//printf("searcing for - %s -\n",key);
	if (f)
	{
		if (strcmp(key,"BEFORETIME")==0)
		{
			fclose(f);
			if (strcmp(ri->language,"es")==0 || (ri->language[0] == 0 && strcmp(deflanguage,"es")==0))
				return "hace ";
			else
				return "";
		}
		int n = strlen(key);
		while (fgets(line, sizeof(line)-1, f) != NULL)
		{
			int l = strlen(line);
			if (l && line[l-1] == 10)
				{line[l-1] = 0; l--;}
			if (l && line[l-1] == 13)
				{line[l-1] = 0; l--;}
			if (line[0] == 239 && line[1] == 187 && line[2] == 191) //unicode start
			{
				int i;
				for (i=3;i<l+1;i++)
					line[i-3]=line[i];
				l-=3;
			}
			if (strncmp(key,line,n) == 0 && line[n] == ' ' && line[n+1] == '-' && line[n+2] == '>' && line[n+3] == ' ')
			{
//				printf("found\n");
				fclose(f);
				return line+n+4;
			}
		}
		fclose(f);
		return key;
	}
//	printf("not found");
	return key;
}
static void
sendmenu(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data);
static void
cgi(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data, int lSize, char* filebuffer)
{
	int i = 0;
	int start = 0;
	while (filebuffer[i])
	{
		if (filebuffer[i] == '`')
		{
			mg_write(conn,filebuffer+start,i-start);
			i += 1;
			start = i;
			while (filebuffer[start] && filebuffer[start] != '`')
				start++;
			if (filebuffer[start] == 0)
				break;
			filebuffer[start] = 0;
			start += 1;
			mg_printf(conn,"%s",lang(ri,filebuffer+i).c_str());
//			printf("%s : %s\n",filebuffer+i, lang(ri,filebuffer+i));
			i = start;
		}
		else
		if (filebuffer[i] == '<' && filebuffer[i+1] == '<')
		{
			mg_write(conn,filebuffer+start,i-start);
			i += 2;
			start = i;
			int deep = 1;
			while (filebuffer[start])
			{
				if (filebuffer[start] == '>' && filebuffer[start+1] == '>')
					deep--;
				if (filebuffer[start] == '<' && filebuffer[start+1] == '<')
					deep++;
				if (!deep)
					break;
				start++;
			}
			if (filebuffer[start] == 0)
				break;
			filebuffer[start] = 0;
			start += 2;
			if (strcmp("MENU",filebuffer+i)==0)
			{
				sendmenu(conn,ri,data);
				i = start;
			}
			else
			if (strcmp("BUTTONS",filebuffer+i)==0)
			{
				if (ri->permissions == PERM_ROOT)
					for (int j=0; j < fastkeys.size(); j++)
						if (1 || fastkeys[j]->show)
						{
//							mg_printf(conn,"<input type=\"button\" class=\"butt\" value=\"%s\" onclick=\"makeRequest('button_%d','')\"/>",lang(ri,fastkeys[j]->name),j+1);
							mg_printf(conn,"<div class=\"widget\" id=\"button_%s\"><input type=\"button\" class=\"butt\" value=\"%s\" onclick=\"makeRequest('button_%d','')\"/></div>",fastkeys[j]->name,lang(ri,fastkeys[j]->name).c_str(),j+1);
						}
				i = start;
			}
			else
			if (strcmp("MODELNAME",filebuffer+i)==0)
			{
				if (modelname[0])
					mg_printf(conn,"%s",modelname);
				i = start;
			}
			else
			if (strcmp("XRES",filebuffer+i)==0)
			{
				mg_printf(conn,"%d",scrinfo.xres);
				i = start;
			}
			else
			if (strcmp("YRES",filebuffer+i)==0)
			{
				mg_printf(conn,"%d",scrinfo.yres);
				i = start;
			}
			else
			if (strcmp("CHAT",filebuffer+i)==0)
			{
				if (ri->permissions == PERM_ROOT || (ri->permissions&PERM_CHAT))
					mg_printf(conn,"true");
				else
					mg_printf(conn,"false");
				i = start;
			}
			else
			if (strcmp("USERNAME",filebuffer+i)==0)
			{
				if (ri->remote_user)
					mg_printf(conn,"%s",ri->remote_user);
				i = start;
			}
			else
			if (strcmp("PORT",filebuffer+i)==0)
			{
				mg_printf(conn,"%d",port);
				i = start;
			}
			else
			if (strcmp("SSLPORT",filebuffer+i)==0)
			{
				mg_printf(conn,"%d",sslport);
				i = start;
			}
			else
			if (strcmp("WEBVERSION",filebuffer+i)==0)
			{
				mg_printf(conn,VERSION);
				i = start;
			}
			else
			if (strncmp("ADMIN",filebuffer+i,5)==0)
			{
				if (ri->permissions == PERM_ROOT )
				{
					filebuffer[start-2] = ' ';
					filebuffer[start-1] = ' ';
					i = i+5;
					start = i;
				}
				else
					i = start;
			}
//			else
//			if (strncmp("IFNOTZTEBLADE",filebuffer+i,13)==0)
//			{
//				if (!is_zte_blade)
//				{
//					filebuffer[start-2] = ' ';
//					filebuffer[start-1] = ' ';
//					i = i+13;
//					start = i;
//				}
//				else
//					i = start;
//			}
//			else
//			if (strncmp("ONLYADMIN",filebuffer+i,9)==0)
//			{
//				if (ri && ri->remote_user && strcmp(ri->remote_user,"admin") == 0 )
//				{
//					filebuffer[start-2] = ' ';
//					filebuffer[start-1] = ' ';
//					i = i+9;
//					start = i;
//				}
//				else
//					i = start;
//			}
			else
			if (strncmp("CHANGE2SSL",filebuffer+i,10)==0)
			{
				if (ri->remote_ip && has_ssl && ri->is_ssl == false)
				{
					filebuffer[start-2] = ' ';
					filebuffer[start-1] = ' ';
					i = i+10;
					start = i;
				}
				else
					i = start;
			}
			else
			if (strncmp("CHANGE2NORMAL",filebuffer+i,13)==0)
			{
				if (ri->remote_ip && has_ssl && ri->is_ssl == true)
				{
					filebuffer[start-2] = ' ';
					filebuffer[start-1] = ' ';
					i = i+13;
					start = i;
				}
				else
					i = start;
			}
			else
			if (strcmp("FRAMEBUFFER_COUNT",filebuffer+i)==0)
			{
				if (scrinfo.yres == scrinfo.yres_virtual)
					mg_printf(conn,"hidden");
				else
					mg_printf(conn,"checkbox");
				i = start;
			}
			else
			if (strncmp("REGISTRATION",filebuffer+i,12)==0)
			{
				bool r = true;
				std::string sharedpref = dir + "../shared_prefs/com.webkey_preferences.xml";
				FILE* sp = fopen(sharedpref.c_str(),"r");
				if (!sp)
				      	sp = fopen("/dbdata/databases/com.webkey/shared_prefs/com.webkey_preferences.xml","r");
				if (sp)
				{
					char buff[256];
					while (fgets(buff, sizeof(buff)-1, sp) != NULL)
					{
						if (startswith(buff,"<boolean name=\"allowremotereg\" value=\"false\""))
						{
							r = false;
							break;
						}
					}
					fclose(sp);
				}
				if (r)
				{
					filebuffer[start-2] = ' ';
					filebuffer[start-1] = ' ';
					i = i+12;
					start = i;
				}
				else
					i = start;
			}
			else
			if (strncmp("HASUSER",filebuffer+i,7)==0)
			{
				bool r = false;
				std::string sharedpref = dir + "../shared_prefs/com.webkey_preferences.xml";
				FILE* pf = fopen((dir+passfile).c_str(),"r");
				char tmp[10];
				if (pf != NULL && fgets(tmp, 9, pf) != NULL)
				{
					r = true;
				}
				if (pf)
					fclose(pf);
				if (r)
				{
					filebuffer[start-2] = ' ';
					filebuffer[start-1] = ' ';
					i = i+7;
					start = i;
				}
				else
					i = start;
			}
			else
			if (strncmp("NOREGISTRATION",filebuffer+i,14)==0)
			{
				bool r = true;
				std::string sharedpref = dir + "../shared_prefs/com.webkey_preferences.xml";
				FILE* sp = fopen(sharedpref.c_str(),"r");
				if (!sp)
				      	sp = fopen("/dbdata/databases/com.webkey/shared_prefs/com.webkey_preferences.xml","r");
				if (sp)
				{
					char buff[256];
					while (fgets(buff, sizeof(buff)-1, sp) != NULL)
					{
						if (startswith(buff,"<boolean name=\"allowremotereg\" value=\"false\""))
						{
							r = false;
							break;
						}
					}
					fclose(sp);
				}
				if (!r)
				{
					filebuffer[start-2] = ' ';
					filebuffer[start-1] = ' ';
					i = i+14;
					start = i;
				}
				else
					i = start;
			}
		}
		else
			i++;
	}
//	printf("i=%d, start=%d\n");
	mg_write(conn,filebuffer+start,i-start);
}
static void
sendmenu(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	lock_wakelock();
	mg_printf(conn,"<div id=\"tabs\"><ul>");
	if (ri->permissions == PERM_ROOT || (ri->permissions&PERM_SCREENSHOT))
		mg_printf(conn,"<li><a href=\"phone.html\" target=\"_top\"><span>%s</span></a></li> ",lang(ri,"Phone").c_str());
	if (ri->permissions == PERM_ROOT)
	{
		mg_printf(conn,"<li><a href=\"terminal.html\" target=\"_top\"><span>%s</span></a></li> ",lang(ri,"Terminal").c_str());
	}
	//if (ri->permissions == PERM_ROOT)
//	mg_printf(conn,"<a href=\"help.html\" target=\"_top\">help</a> <a href=\"\" onclick=\"document.location.replace(document.location.href.replace(/:\\/\\//,':\\/\\/logout:logout@'))\"target=\"_top\">log out</a></div>");
//	mg_printf(conn,"<a href=\"help.html\" target=\"_top\">help</a> <a href=\"logout\" onclick=\"try {document.execCommand('ClearAuthenticationCache');} catch (exception) {}; document.location=document.location.href.replace(/:\\/\\//,':\\/\\/logout:logout@')\" target=\"_top\">log out</a></div>");
	mg_printf(conn,"<li id=\"menulastli\"></li></ul></div>");
//	mg_printf(conn,"<li><span>Webkey %s<br/> %s</span></li> ",VERSION, ri->remote_user);
//	mg_printf(conn,"<li> <a href=\"config\" target=\"_top\"><span>%s</span></a>",lang(ri,"Config").c_str());
/*	if ((ri->permissions == PERM_ROOT || (ri->permissions&PERM_CHAT)) && strcmp(ri->uri,"/pure_menu_nochat.html"))
	{
		pthread_mutex_lock(&chatmutex);
		pthread_cond_broadcast(&chatcond);
		pthread_mutex_unlock(&chatmutex);
		FILE* f;
		f = fo("chat.html","rb");
		if(!f)
			return;
		fseek (f , 0 , SEEK_END);
		int lSize = ftell (f);
		rewind (f);
		char* filebuffer = new char[lSize+1];
		if (filebuffer)
		{
			fread(filebuffer,1,lSize,f);
			filebuffer[lSize] = 0;
			cgi(conn,ri,data,lSize,filebuffer);
			fclose(f);
			delete[] filebuffer;
		} //what can we do with no memory?
	}
*/
}


static void
getfile(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	FILE* f;
	if (strcmp(ri->uri,"/")==0)
		f = fo("index.html","rb");
	else
		f = fo(ri->uri,"rb");
	if(!f)
		return;
	fseek (f , 0 , SEEK_END);
	int lSize = ftell (f);
//	if (saysize)
//		send_ok(conn,NULL,lSize);
//	else
		send_ok(conn,NULL);
	rewind (f);
	char* filebuffer = new char[lSize+1];
	if (filebuffer)
	{
		fread(filebuffer,1,lSize,f);
		filebuffer[lSize] = 0;
		cgi(conn,ri,data,lSize,filebuffer);
		fclose(f);
		delete[] filebuffer;
	} //what can we do with no memory?
}
static void
sendfile(const char* file,struct mg_connection *conn, bool sendok = false, char * extra = NULL)
{
	FILE* f = fopen(file,"rb");
	if(!f)
	{
		if (sendok)
			send_ok(conn);
		return;
	}
	fseek (f , 0 , SEEK_END);
	int lSize = ftell (f);
	if (sendok)
		send_ok(conn,extra,lSize);
	rewind (f);
	char* filebuffer = new char[65536];
	if (filebuffer)
	{
		while(lSize>0)
		{
			int s = min(65536,lSize);
			fread(filebuffer,1,min(65536,lSize),f);
			mg_write(conn,filebuffer,s);
			lSize -= s;
		}
		fclose(f);
		delete[] filebuffer;
	} //what can we do with no memory?
}
static void
index(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT && (ri->permissions&PERM_SCREENSHOT)==0)
	{
		mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"0;url=pure_menu.html\"></head><body>Redirecting...</body></html>");
		return;
	}
	lock_wakelock();
	getfile(conn,ri,data);
}
static void
generic_button(int fd, int key, int time)
{
	if (key == 21) // left
		key = 21;
	else if (key == 19) // up
		key = 19;
	else if (key == 20) // down
		key = 20;
	else if (key == 22) // right
		key = 22;
	else if (key == 3) // home
		key = 3;
	else if (key == 2) // call
		key = 5;
	else if (key == 4) // back
		key = 4;
	else if (key == 82) // menu
		key = 82;
	else if (key == 6) // end -> power
		key = 6;
	else if (key == 23) // center
		key = 23;
	//			else if (key == 28) // keep alive -> ctrl_left, needs test for that
	//				key = 29;
	//			printf("button %d\n",key);
	if (time)
	{
		struct input_event event;
		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, 0);
		event.type = EV_KEY;
		event.code = key;
		event.value = 1;
		write(fd, &event, sizeof(event));
		usleep(time*1000);
		event.value = 0;
		write(fd, &event, sizeof(event));
	}
	else
		suinput_click(fd, key, 0);
}
static void
qwerty_button(int fd, int key, int time)
{
	if (key == 21) // left
		key = 105;
	else if (key == 19) // up
		key = 103;
	else if (key == 20) // down
		key = 108;
	else if (key == 22) // right
		key = 106;
	else if (key == 3) // home
		key = 172;
	else if (key == 2) // call
		key = 169;
	else if (key == 4) // back
		key = 158;
	else if (key == 82) // menu
		key = 127;
	else if (key == 6) // end -> power
		key = 116;
	else if (key == 23) // center
		key = 97;
	//			else if (key == 28) // keep alive -> ctrl_left, needs test for that
	//				key = 29;
	//			printf("button %d\n",key);
	if (time)
	{
		struct input_event event;
		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, 0);
		event.type = EV_KEY;
		event.code = key;
		event.value = 1;
		write(fd, &event, sizeof(event));
		usleep(time*1000);
		event.value = 0;
		write(fd, &event, sizeof(event));
	}
	else
		suinput_click(fd, key, 0);
}
static void
geniatech_button(int fd, int key, int time)
{
	if (key == 21) // left
		key = 105;
	else if (key == 19) // up
		key = 103;
	else if (key == 20) // down
		key = 108;
	else if (key == 22) // right
		key = 106;
	else if (key == 3) // home
		key = 60;
	else if (key == 2) // call
		key = 169;
	else if (key == 4) // back
		key = 158;
	else if (key == 82) // menu
		key = 127;
	else if (key == 6) // end -> power
		key = 116;
	else if (key == 23) // center
		key = 97;
	//			else if (key == 28) // keep alive -> ctrl_left, needs test for that
	//				key = 29;
	//			printf("button %d\n",key);
	if (time)
	{
		struct input_event event;
		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, 0);
		event.type = EV_KEY;
		event.code = key;
		event.value = 1;
		write(fd, &event, sizeof(event));
		usleep(time*1000);
		event.value = 0;
		write(fd, &event, sizeof(event));
	}
	else
		suinput_click(fd, key, 0);
}
static void
qwerty_button_usb(int fd, int key, int time)
{
	if (key == 21) // left
		key = 105;
	else if (key == 19) // up
		key = 103;
	else if (key == 20) // down
		key = 108;
	else if (key == 22) // right
		key = 106;
	else if (key == 3) // home
		key = 102;
	else if (key == 2) // call
		key = 61;
	else if (key == 4) // back
		key = 1;
	else if (key == 82) // menu
		key = 59;
	else if (key == 6) // end -> power
		key = 116;
	else if (key == 28) // keep alive ->
		return;
	//				key = 29;
	//			printf("button %d\n",key);
	if (time)
	{
		struct input_event event;
		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, 0);
		event.type = EV_KEY;
		event.code = key;
		event.value = 1;
		write(fd, &event, sizeof(event));
		usleep(time*1000);
		event.value = 0;
		write(fd, &event, sizeof(event));
	}
	else
		suinput_click(fd, key, 0);
}
static void
button(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
//	TEST
//	if (ri->permissions != PERM_ROOT)
//		return;

	send_ok(conn);
	lock_wakelock();
	access_log(ri,"button inject");
	int key = getnum(ri->uri+8);
	int i = 8;
	while(ri->uri[i] && ri->uri[i] != '_') i++;
	long time = 0;
	if (i < strlen(ri->uri))
		time = getnum(ri->uri+i+1);
	if (geniatech)
	{
		if (key == 28)
			return;
		char device[19] = "/dev/input/event0";
		for (int i=0; i<50; i++)
		{
			int fd;
			char name[256]="Unknown";
			if (i < 10)
			{
				device[sizeof(device)-3] = '0'+(char)(i);
				device[sizeof(device)-2] = 0;
			}
			else
			{
				device[sizeof(device)-3] = '0'+(char)(i/10);
				device[sizeof(device)-2] = '0'+(char)(i%10);
				device[sizeof(device)-1] = 0;
			}
			struct input_absinfo info;
			if((fd = open(device, O_RDWR)) == -1)
			{
				continue;
			}
			if (ioctl(fd, EVIOCGNAME(sizeof(name)),name) < 0)
				continue;
			if (check_type(name,"aml_keypad"))
			{
				geniatech_button(fd, key, time);
				close(fd);
				break;
			}
			close(fd);
		}
		return;
	}
	if (uinput_fd == -1)
	{
		init_uinput();
		shutdownkey_up = up+100;
	}
	bool no_uinput = false;
	if (uinput_fd == -1)
		no_uinput = true;
	if (is_icecreamsandwich && no_uinput == false)
	{
		if (key == 28 && is_icecreamsandwich)
			key = 59; // SHIFT_LEFT
		generic_button(uinput_fd, key, time);
		return;
	}
//	if (samsung && key == 28)
//		key = 63;

//	if (geniatech)
//	{
//		geniatech_button(uinput_fd, key, time);
//		return;
//	}
	// for samsung galaxy s 2
	std::string devpath = device_names["sec_key"];
	if ((key == 24 || //VOL_UP
	    key == 25 || //VOL_DOWN
	    key == 3 || //HOME
	    key == 26)  //POWER
	   && devpath.size())
	{
	    int fd = open(devpath.c_str(), O_WRONLY | O_NONBLOCK);
	    if (fd > -1)
	    {
		    if (key == 24) key = 115;  //vol up
		    if (key == 25) key = 114;  //vol down
		    if (key == 3) key = 102; //home
		    if (key == 26) key = 116; //power
		    suinput_click(fd, key, 0);
		    close(fd);
		    return;
	    }
	}
	devpath = device_names["sec_touchkey"];
	if ((key == 82 || //MENU
	    key == 4) //BACK
	   && devpath.size())
	{
	    int fd = open(devpath.c_str(), O_WRONLY | O_NONBLOCK);
	    if (fd > -1)
	    {
		    if (key == 82) key = 139; // menu
		    if (key == 4) key = 158; // back
		    suinput_click(fd, key, 0);
		    close(fd);
		    return;
	    }
	}
	// for samsung galaxy s
	devpath = device_names["s3c-keypad"];
	if ((key == 3 || //HOME
	    key == 26 || //POWER
	    key == 24 || //VOL UP
	    key == 25) //VOL DOWN
	   && devpath.size())
	{
	    int fd = open(devpath.c_str(), O_WRONLY | O_NONBLOCK);
	    if (fd > -1)
	    {
		    if (key == 3) key = 50; // home
		    if (key == 26) key = 26; // power
		    if (key == 24) key = 42; // vol up
		    if (key == 25) key = 58; // vol down
		    suinput_click(fd, key, 0);
		    close(fd);
		    return;
	    }
	}

//	printf("KEY = %d\n",key);
//	for (std::map<int,DEVSPEC>::const_iterator it = device_specific_buttons.begin(); it != device_specific_buttons.end(); it++)
//	{
//		printf("%d: %d\n",it->first,it->second.dev);
//	}

//	printf("Size = %d -> ",device_specific_buttons.size());
//	DEVSPEC d = device_specific_buttons[key];
//	printf("%d\n",device_specific_buttons.size());
/*	if (d.dev || d.type || d.code)
	{
		int dev;
		char file[20];
		strcpy(file,"/dev/input/eventX");
		file[strlen(file)-1] = 48+d.dev;
//		printf("%s\n",file);
//		printf("%d\n",d.dev);
//		printf("%d\n",d.type);
//		printf("%d\n",d.code);
		if((dev = open(file, O_WRONLY)) == -1)
		{
			return;
		}
		struct input_event event;
		memset(&event, 0, sizeof(event));
		gettimeofday(&event.time, 0);
		event.type = d.type;
		event.code = d.code;
		event.value = 1;
		write(dev, &event, sizeof(event));
		usleep(time*1000000);
		event.value = 0;
		write(dev, &event, sizeof(event));
		//printf("OK\n");
		close(dev);

	}
	else
*/	{
//	printf("button: %d\n",key);
//		if (no_uinput)
		if (no_uinput)
		{
			if (key == 28 && is_icecreamsandwich)
				key = 42; // SHIFT_LEFT
			char device[19] = "/dev/input/event0";
			for (int i=0; i<50; i++)
			{
				int fd;
				char name[256]="Unknown";
				if (i < 10)
				{
					device[sizeof(device)-3] = '0'+(char)(i);
					device[sizeof(device)-2] = 0;
				}
				else
				{
					device[sizeof(device)-3] = '0'+(char)(i/10);
					device[sizeof(device)-2] = '0'+(char)(i%10);
					device[sizeof(device)-1] = 0;
				}
				struct input_absinfo info;
				if((fd = open(device, O_RDWR)) == -1)
				{
					continue;
				}
				if (ioctl(fd, EVIOCGNAME(sizeof(name)),name) < 0)
					continue;
				if (check_type(name,"usb keyboard"))
					qwerty_button_usb(fd, key, time);
				close(fd);
			}

		}
		else
		{
			if (time)
			{
				struct input_event event;
				memset(&event, 0, sizeof(event));
				gettimeofday(&event.time, 0);
				event.type = EV_KEY;
				event.code = key;
				event.value = 1;
				write(uinput_fd, &event, sizeof(event));
				usleep(time*1000);
				event.value = 0;
				write(uinput_fd, &event, sizeof(event));
			}
			else
				suinput_click(uinput_fd, key, 0);
		}
	}
}
static void
config_buttons(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	getfile(conn,ri,data);
	mg_printf(conn,"<table border=\"1\">\n");
	int i;
	for (i=0; i < fastkeys.size(); i++)
	{
		mg_printf(conn, "<tr><td>%s</td>",lang(ri,fastkeys[i]->name).c_str());
		mg_printf(conn, "<td><input type=\"checkbox\" name=\"show_%d\">%s</input></td>",i+1,lang(ri,"Show").c_str());
		mg_printf(conn, "<td>%s: <input type=\"text\" name=\"key_%d\" maxlength=\"1\" size=\"1\" onkeypress=\"var unicode=event.charCode? event.charCode : -event.keyCode;document.config_buttons.keycode_%d.value=unicode; document.config_buttons.key_%d.value=String.fromCharCode(document.config_buttons.keycode_%d.value)\"/></td>",lang(ri,"Key").c_str(),i+1,i+1,i+1,i+1);
		mg_printf(conn, "<td>%s: <input type=\"text\" name=\"keycode_%d\" maxlength=\"8\" size=\"8\" onkeyup=\"document.config_buttons.key_%d.value=String.fromCharCode(document.config_buttons.keycode_%d.value)\"/></td></tr>\n",lang(ri,"Keycode").c_str(),i+1,i+1,i+1);
	}
	mg_printf(conn,"</table></form>\n<script type=\"text/javascript\" language=\"javascript\">");

	for (i=0; i < fastkeys.size(); i++)
	{
		mg_printf(conn,"document.config_buttons.key_%d.value=String.fromCharCode(%d);document.config_buttons.keycode_%d.value='%d';document.config_buttons.show_%d",i+1,fastkeys[i]->ajax,i+1,fastkeys[i]->ajax,i+1);
		if (fastkeys[i]->show)
			mg_printf(conn,".checked='checked';\n");
		else
			mg_printf(conn,".checked='';\n");
	}
	mg_printf(conn,"</script></body></html>");
}
static void
config_keys(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	getfile(conn,ri,data);
	mg_printf(conn,"<table border=\"1\">\n");
	int i;
	for (i=0; i < 106; i++)
	{
		mg_printf(conn,"<tr><td>%s: <input type=\"text\" name=\"key_%d\" maxlength=\"1\" size=\"1\" onkeypress=\"var unicode=event.charCode? event.charCode : event.keyCode;document.config_keys.keycode_%d.value=unicode; document.config_keys.key_%d.value=String.fromCharCode(document.config_keys.keycode_%d.value)\"/></td>",lang(ri,"Char").c_str(),i+1,i+1,i+1,i+1);
		mg_printf(conn,"<td>%s: <input type=\"text\" name=\"keycode_%d\" maxlength=\"8\" size=\"8\" onkeyup=\"document.config_keys.key_%d.value=String.fromCharCode(document.config_keys.keycode_%d.value)\"/></td>",lang(ri,"Keycode").c_str(),i+1,i+1,i+1);
		mg_printf(conn,"<td> %s </td>",lang(ri,"converts to").c_str());
		mg_printf(conn,"<td>%s: <input type=\"text\" name=\"tokey_%d\" maxlength=\"1\" size=\"1\" onkeypress=\"var unicode=event.charCode? event.charCode : event.keyCode;document.config_keys.tokeycode_%d.value=unicode; document.config_keys.tokey_%d.value=String.fromCharCode(document.config_keys.tokeycode_%d.value)\"/></td>",lang(ri,"Char").c_str(),i+1,i+1,i+1,i+1);
		mg_printf(conn,"<td>%s: <input type=\"text\" name=\"tokeycode_%d\" maxlength=\"8\" size=\"8\" onkeyup=\"document.config_keys.tokey_%d.value=String.fromCharCode(document.config_keys.tokeycode_%d.value)\"/></td>",lang(ri,"Keycode").c_str(),i+1,i+1,i+1);
		mg_printf(conn,"<td><input type=\"checkbox\" name=\"sms_%d\">%s</input></td></tr>\n",i+1,lang(ri,"works in SMS mode").c_str());
	}
	mg_printf(conn,"</table></form>\n<script type=\"text/javascript\" language=\"javascript\">\n");

	for (i=0; i < speckeys.size(); i++)
	{
		mg_printf(conn,"document.config_keys.key_%d.value=String.fromCharCode(%d);document.config_keys.keycode_%d.value='%d';document.config_keys.tokey_%d.value=String.fromCharCode(%d);document.config_keys.tokeycode_%d.value='%d';document.config_keys.sms_%d",i+1,speckeys[i]->ajax,i+1,speckeys[i]->ajax,i+1,speckeys[i]->disp,i+1,speckeys[i]->disp,i+1);
		if (speckeys[i]->sms)
			mg_printf(conn,".checked='checked';\n");
		else
			mg_printf(conn,".checked='';\n");
	}
	mg_printf(conn,"</script></body></html>");
}
static std::string getoption(char* list, char* option)
{
	std::string ret;
	int n  = strlen(list);
	int i;
	for (i = 0; i < n; i++)
		if (startswith(list+i, option))
		{
			i+=strlen(option);
			while(i<n && list[i] != '&')
				ret += list[i++];
			break;
		}
	return ret;
}

static void
getreg(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->remote_ip!=2130706433 || strcmp(ri->remote_user,"JAVA_CLIENT") != 0) //localhost
		return;
	send_ok(conn);
//	printf("getreg called\n"); fflush(NULL);
//	printf("%s\n%s\n%u",requested_username.c_str(),requested_password.c_str(),requested_ip);fflush(NULL);

	mg_printf(conn,"%s\n%s\n%u",requested_username.c_str(),requested_password.c_str(),requested_ip);

	requested_username = "";
	requested_password = "";
}
static void
reg(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
//	printf("reg called\n"); fflush(NULL);
	bool r = true;
	std::string sharedpref = dir + "../shared_prefs/com.webkey_preferences.xml";
	FILE* sp = fopen(sharedpref.c_str(),"r");
	if (!sp)
		sp = fopen("/dbdata/databases/com.webkey/shared_prefs/com.webkey_preferences.xml","r");
	if (sp)
	{
		char buff[256];
		while (fgets(buff, sizeof(buff)-1, sp) != NULL)
		{
			if (startswith(buff,"<boolean name=\"allowremotereg\" value=\"false\""))
			{
				r = false;
				break;
			}
		}
		fclose(sp);
	}
	if (!r)
	{
		mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"3;url=index.html\"></head><body>%s",lang(ri,"New user's registration is disabled, click on \"allow user registration in browser\" to enable it.").c_str());
		mg_printf(conn,"%s</body></html>",lang(ri,"Reloading...").c_str());
		return;
	}
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	requested_username = "";

	if (strncmp(post_data,"username=",9))
		return;
	int i = 9;
	while (i < post_data_len && post_data[i] != '&')
	{
		requested_username += post_data[i++];
	}
	if (strncmp(post_data+i,"&password=",10))
	{
		requested_username = "";
		return;
	}
	i += 10;
	requested_password = "";
	while (i < post_data_len && post_data[i] != '&')
	{
		requested_password += post_data[i++];
	}
	if (requested_username.size() >= FILENAME_MAX-1 || requested_password.size() >= FILENAME_MAX-1)
	{
		requested_username = "";
		return;
	}
//	printf("%s\n%s\n%u",requested_username.c_str(),requested_password.c_str(),requested_ip);fflush(NULL);
	requested_ip = ri->remote_ip;
	char to[FILENAME_MAX];
	strcpy(to,requested_username.c_str());
	url_decode(to, strlen(to), to, FILENAME_MAX, true);
	requested_username = to;
	strcpy(to,requested_password.c_str());
	url_decode(to, strlen(to), to, FILENAME_MAX, true);
	requested_password = to;
	delete[] post_data;
	mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"10;url=index.html\"></head><body>%s ",lang(ri,"Registration request sent, please wait until it's authorized on the phone.").c_str());
	mg_printf(conn,"%s<br/><img alt=\"reganim\" src=\"reganim.gif\"/></body></html>",lang(ri,"Reloading...").c_str());
	syst("am broadcast -a \"webkey.intent.action.remote.registration\"");
}
static void
setpassword(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	printf("setpassword:\n");
	fflush(NULL);
	if (ri->remote_ip!=2130706433 || (ri->remote_user && strcmp(ri->remote_user,"JAVA_CLIENT") != 0)) //localhost
		return;
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	int i = 0;
	int pos[1];
	int j = 0;
	for (;i < post_data_len; i++)
	{
		if (post_data[i] == '\n' && j < 1)
		{
			post_data[i] = 0;
			pos[j++] = i+1;
		}
	}
//	printf("username = %s\n",post_data);
//	printf("password = %s\n",post_data+pos[0]);
//	fflush(NULL);
	mg_modify_passwords_file(ctx, (dir+passfile).c_str(), post_data, post_data+pos[0],-2);
	chmod((dir+passfile).c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	chown((dir+passfile).c_str(), info.st_uid, info.st_gid);
	chmod((dir+passfile).c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	send_ok(conn);
}
static void
setpermission(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->remote_ip!=2130706433 || (ri->remote_user && strcmp(ri->remote_user,"JAVA_CLIENT") != 0)) //localhost
		return;
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	int i = 0;
	int pos[2];
	int j = 0;
	for (;i < post_data_len; i++)
	{
		if (post_data[i] == '\n' && j < 2)
		{
			post_data[i] = 0;
			pos[j++] = i+1;
		}
	}
//	printf("setpermission:\n");
//	printf("username = %s\n",post_data);
//	printf("password = %s\n",post_data+pos[0]);
//	printf("permission = %s\n",post_data+pos[1]);
	mg_modify_passwords_file(ctx, (dir+passfile).c_str(), post_data, post_data+pos[0],getnum(post_data+pos[1]));
	chmod((dir+passfile).c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	chown((dir+passfile).c_str(), info.st_uid, info.st_gid);
	chmod((dir+passfile).c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	send_ok(conn);
}

static void
config(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
//	if (ri->permissions != PERM_ROOT)
//		return;
	lock_wakelock();
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
        int n = post_data_len;
	int i = 0;
	char name[256];
	char pass[256];
	name[0] = pass[0] = 0;
	bool changed = false;
	bool changed_perm = false;
	int permissions = 0;
	int j = 0;
	if (n>256)
		return;
	while(i < n)
	{
//		printf("%s\n",ri->post_data);
		if (!memcmp(post_data+i, "username",8))
		{
			i+=9;
			j = 0;
			while(i<n && post_data[i] != '&' && j<255)
				name[j++] = post_data[i++];
			name[j] = 0; i++;
		}
		else if (!memcmp(post_data+i, "permission",10))
		{
			changed_perm = true;
			changed = true;
			i+=10;
			if (post_data[i] < '8' && post_data[i] >= '0' && permissions != -1)
			{
				int p = post_data[i] - 48;
				if (p == 0)
					permissions = -1;
				else
					permissions = permissions | (1<<(p-1));
			}

			while(i<n && post_data[i] != '&')
				i++;
			i++;
		}
		else if (!memcmp(post_data+i, "password",8))
		{
			i+=9;
			int k = 0;
			while(i<n && post_data[i] != '&' && k<255)
				pass[k++] = post_data[i++];
			pass[k] = 0; i++;
			if (j && k)
			{
				if (ri->permissions == PERM_ROOT || strcmp(ri->remote_user,name)==0)
				{
					access_log(ri,"modify users");
					url_decode(name, strlen(name), name, FILENAME_MAX, true);
					url_decode(pass, strlen(pass), pass, FILENAME_MAX, true);
					for (int q = 0; q < strlen(name); q++)
						if (name[q] == ':')
							name[q] = ' ';
					mg_modify_passwords_file(ctx, (dir+passfile).c_str(), name, pass,-2);
				}
				changed = true;
			}
		}
		else if (!memcmp(post_data+i, "remove",6))
		{
			i=n;
			if (j)
			{
				if (ri->permissions == PERM_ROOT)
				{
					access_log(ri,"modify users");
					url_decode(name, strlen(name), name, FILENAME_MAX, true);
					for (int q = 0; q < strlen(name); q++)
						if (name[q] == ':')
							name[q] = ' ';
					mg_modify_passwords_file(ctx, (dir+passfile).c_str(), name, "",-2);
				}
				changed = true;
			}
		}
		else if (!memcmp(post_data+i, "dellog",6))
		{
			if (ri->permissions == PERM_ROOT)
			{

				pthread_mutex_lock(&logmutex);
				FILE* f = fopen(logfile.c_str(),"w");
				if (f)
				{
					fclose(f);
					access_times.clear();
					pthread_mutex_unlock(&logmutex);
					access_log(ri,"clear log");
				}
				else
					pthread_mutex_unlock(&logmutex);
			}
			changed = true;
			break;
		}
		else
			i++;
	}
	if (changed_perm && ri->permissions == PERM_ROOT)
	{
		if (permissions != PERM_ROOT && (permissions & PERM_PUBLIC) && (permissions & PERM_FILES))
			permissions = (permissions ^ PERM_PUBLIC);
		url_decode(name, strlen(name), name, FILENAME_MAX, true);
		url_decode(pass, strlen(pass), pass, FILENAME_MAX, true);
		for (int q = 0; q < strlen(name); q++)
			if (name[q] == ':')
				name[q] = ' ';
		mg_modify_passwords_file(ctx, (dir+passfile).c_str(), name, pass,permissions);
		access_log(ri,"modify permissions");
	}
	if (changed)
	{
		if (!data)
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"1;url=config\"></head><body>%s</body></html>",lang(ri,"Saved, reloading ...").c_str());
		else
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"1;url=usersconfig\"></head><body>%s</body></html>",lang(ri,"Saved, reloading ...").c_str());
		if (post_data)
			delete[] post_data;
		return;
	}
	send_ok(conn);
	mg_printf(conn,"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://www.w3.org/TR/html4/strict.dtd\">\n<html>\n<head>\n<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">\n<title>%s</title>",lang(ri,"Webkey for Android").c_str());
        mg_printf(conn,"<link href=\"css/jquery-ui.css\" rel=\"stylesheet\" type=\"text/css\"/><link href=\"css/webkey.css\" rel=\"stylesheet\" type=\"text/css\"/><link rel=\"shortcut icon\" href=\"favicon.ico\"><script src=\"js/jquery.js\"></script><script src=\"js/jquery-ui.min.js\"></script><script src=\"js/webkey.js\"></script></head><body>");
	if (!data)
		sendmenu(conn,ri,NULL);
//	mg_printf(conn,"<br/>Users. The user \'admin\' will have ALL permissions and a random password each time you start the service. Empty passwords are not allowed.<br/>");
	char line[256]; char domain[256];
	FILE* fp = fo(passfile.c_str(),"r");
	if (!fp)
		return;
	while (fgets(line, sizeof(line)-1, fp) != NULL)
	{
		permissions = -1;
		if (sscanf(line, "%[^:]:%[^:]:%[^:]:%d", name, domain, pass, &permissions) < 3)
			continue;
		if (ri->permissions != PERM_ROOT && strcmp(name,ri->remote_user)!=0)
			continue;

		mg_printf(conn,"<hr/>");
		mg_printf(conn,"<form name=\"%s_form\" method=\"post\">%s: <input type=\"text\" readonly=\"readonly\" value=\"%s\" name=\"username\">",name,lang(ri,"username").c_str(),name);
		mg_printf(conn,"%s: <input type=\"password\" name=\"password\"></input>",lang(ri,"password").c_str());
		mg_printf(conn,"<input type=\"submit\" value=\"%s\"></input>",lang(ri,"Change password").c_str());
		if (ri->permissions == PERM_ROOT)
			mg_printf(conn,"<input name=\"remove\" type=\"submit\" value=\"%s\"></input></form>",lang(ri,"Remove user").c_str());
		else
			mg_printf(conn,"<br/>");
		mg_printf(conn,"<form name=\"%s_form\" method=\"post\"><input type=\"hidden\" name=\"username\" value=\"%s\">",name,name);
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission0\" %s %s>%s</input>",permissions == -1? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"ALL").c_str());
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission1\" %s %s>%s</input>",(permissions != -1) && (permissions & PERM_SCREENSHOT)? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"Screenshot").c_str());
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission2\" %s %s>%s</input>",(permissions != -1) && (permissions & PERM_GPS)? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"Location").c_str());
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission3\" %s %s>%s</input>",(permissions != -1) && (permissions & PERM_CHAT)? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"Chat").c_str());
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission4\" %s %s>%s</input>",(permissions != -1) && (permissions & PERM_PUBLIC)? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"Read /sdcard/public/").c_str());
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission5\" %s %s>%s</input>",(permissions != -1) && (permissions & PERM_SMS_CONTACT)? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"Contacts, Sms, Calls").c_str());
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission6\" %s %s>%s</input>",(permissions != -1) && (permissions & PERM_FILES)? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"Read files").c_str());
		mg_printf(conn,"<input type=\"checkbox\" name=\"permission7\" %s %s>%s</input>",(permissions != -1) && (permissions & PERM_SDCARD)? "checked=\"yes\"":"",ri->permissions == -1?"":"disabled=false",lang(ri,"Sdcard").c_str());
		if (ri->permissions == PERM_ROOT)
			mg_printf(conn,"<input type=\"submit\" value=\"%s\"></input>",lang(ri,"Save permissions").c_str());
		mg_printf(conn,"<br/>");
//		if (!strcmp(name,"admin"))
//			mg_printf(conn,"This password will change on every restart\n");
		mg_printf(conn,"</form>\n");
	}
	mg_printf(conn,"<hr/>");
	if (ri->permissions == PERM_ROOT)
	{
		mg_printf(conn,"<form name=\"newuser\" method=\"post\">%s:<input type=\"text\" name=\"username\">",lang(ri,"New user").c_str());
		mg_printf(conn,"%s: <input type=\"password\" name=\"password\"></input>",lang(ri,"password").c_str());
		mg_printf(conn,"<input type=\"submit\" value=\"%s\"></input></form>\n",lang(ri,"Create").c_str());
	}
	fclose(fp);
	mg_printf(conn,"<h3>%s</h3>",lang(ri,"Details about permissions").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"ALL").c_str());
	mg_printf(conn,"%s",lang(ri,"All other permissions. Additionally, user can inject keys, inject touch events, run commands, run commands in terminal, view log. These functions are not accessible by any of the other permissions.").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"Screenshot").c_str());
	mg_printf(conn,"%s",lang(ri,"View screenshot of the phone.").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"Location").c_str());
	mg_printf(conn,"%s",lang(ri,"View GPS and network location of the phone. The GPS is only available if it is enabled in your phone's Settings. Network location depends on your carrier.").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"Chat").c_str());
	mg_printf(conn,"%s",lang(ri,"Read and write messages. Every message is in the same list, and everyone can empty that list.").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"Read /sdcard/public/").c_str());
	mg_printf(conn,"%s",lang(ri,"If this permission is set and the permission \"Read files\" is not, then under the menu \"Files\" only the content of /sdcard/public will be available for read. Please create that directory if you are about to use this function.").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"Contacts, Sms, Calls").c_str());
	mg_printf(conn,"%s",lang(ri,"Read personal data such as contacts, messages and call list.").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"Read files").c_str());
	mg_printf(conn,"%s",lang(ri,"Read all files on the phone. Be careful, the contacts, messages, call list and the passwords of Webkey are stored in files!").c_str());
	mg_printf(conn,"<h4 class=\"list\">%s</h4>",lang(ri,"Sdcard").c_str());
	mg_printf(conn,"%s",lang(ri,"Read and modify the content of the sdcard.").c_str());

	if (ri->permissions == PERM_ROOT)
	{
		mg_printf(conn,"<h3>%s</h3>",lang(ri,"Log (activities is logged once in every 30 minutes)").c_str());
		FILE *f = fopen(logfile.c_str(),"r");
		if(f)
		{
			char buff[256];
			while (fgets(buff, sizeof(buff)-1, f) != NULL)
			{
				mg_printf(conn,"%s<br/>",buff);
			}
			fclose(f);
		}
		mg_printf(conn,"<form name=\"dellog_form\" method=\"post\"><input type=\"hidden\" name=\"dellog\" value=\"dellog\"><input type=\"submit\" value=\"%s\"></input></form>",lang(ri,"Clear log").c_str());
	}
	mg_printf(conn,"</body></html>");
	if (post_data)
		delete[] post_data;
}
static void
screenshot(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	printf("HERE %s\n", ri->uri);
	lock_wakelock();
	access_log(ri,"view screenshot");
	int orient = 0;
	bool png = false;
	if (ri->uri[12] == 'p')
		png = true;
	if (ri->uri[16] == 'h') // horizontal
		orient = 1;
	int lowres = 0;
	if (ri->uri[17] == 'l') // low res
		lowres = 1;
	firstfb = false;
	if (ri->uri[18] == 'f') // first fb
		firstfb = true;
	bool flip = false;
	if (ri->uri[19] == 'f') // flip
		flip = true;
	bool wait = false;
	if (ri->uri[20] == 'w') // wait for diff
		wait = true;
	bool asfile = false;
	if (ri->uri[21] == 'f') // save as file
	{
		asfile = true;
	}
	lastorient = orient;
	lastflip = flip;
	printf("HERE\n");
//int r = rand();
//printf("in %d\n",r);

	if (wait && !picchanged)
	{
		pthread_mutex_lock(&diffmutex);
		diffcondWaitCounter++;
		int myCounter = diffcondWaitCounter;
		pthread_cond_broadcast(&diffstartcond);
		if (exit_flag)
		       return;
		do{
			pthread_cond_wait(&diffcond,&diffmutex);
			if (exit_flag)
				return;
		}
		while ((myCounter%3)!=(diffcondDiffCounter%3));
		pthread_mutex_unlock(&diffmutex);
	}
	if (!asfile)
	{
		struct timeval tv;
		gettimeofday(&tv,0);
		time_t now = tv.tv_sec;
		if (png == true)
			mg_printf(conn,"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: image/png\r\nCache-Control: max-age=300\r\nSet-Cookie: time=%lu%lu\r\n\r\n",tv.tv_sec,tv.tv_usec);
		else
			mg_printf(conn,"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: image/jpeg\r\nCache-Control: max-age=300\r\nSet-Cookie: time=%lu%lu\r\n\r\n",tv.tv_sec,tv.tv_usec);
	}

	if (!pict)
		init_fb();
	FILE* f;
	std::string path = dir+"tmp";
	pthread_mutex_lock(&pngmutex);
	update_image(orient,lowres,png,flip,!wait);
	pthread_mutex_unlock(&pngmutex);
	if (png)
	{
		path += ".png";
		f = fopen(path.c_str(),"rb");
	}
	else
	{
		path += ".jpg";
		f = fopen(path.c_str(),"rb");
	}
	if (!f)
	{
		return;
	}
	fseek (f , 0 , SEEK_END);
	int lSize = ftell (f);
	rewind (f);
	char* filebuffer = new char[lSize+1];
	if(!filebuffer)
	{
		error("not enough memory for loading tmp.png\n");
	}
	fread(filebuffer,1,lSize,f);
	filebuffer[lSize] = 0;
	//printf("sent bytes = %d\n",mg_write(conn,filebuffer,lSize));
	if (asfile)
	{
		if (png)
			send_ok(conn,"Content-Type: image/png; charset=UTF-8\r\nContent-Disposition: attachment;filename=screenshot.png",lSize);
		else
			send_ok(conn,"Content-Type: image/jpeg; charset=UTF-8\r\nContent-Disposition: attachment;filename=screenshot.jpg",lSize);
	}
	mg_write(conn,filebuffer,lSize);
	fclose(f);
	delete[] filebuffer;
//printf("out %d\n",r);
}
//#define mylog(x,fmt) {FILE* __f = fopen("/data/data/com.webkey/log.txt","a"); if (__f) {fprintf(__f,"%s:%u (%d,%d) %s="fmt"\n",__FILE__, __LINE__, pthread_self(), time(NULL), #x,x); fclose(__f); } printf("%s:%u (%d,%d) %s="fmt"\n",__FILE__, __LINE__, pthread_self(), time(NULL), #x,x);}
#define mylog(x,fmt)
static void
stop(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	mylog("stop","%s");
	send_ok(conn);
	if (ri->remote_ip==2130706433) //localhost
	{
//		printf("Stopping server...\n");
		mylog("stopping","%s");
		exit_flag = 2;
		mg_printf(conn,"Goodbye.");
		access_log(ri,"stop service");
	}
}
static void
run(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"run command");
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	send_ok(conn);
	int n = 4;
	std::string call = "";
	if (post_data_len)
	{
		call = post_data;
	}
	else
	if (ri->uri[n] == '_')
		while (ri->uri[++n])
		{
			int k = getnum(ri->uri+n);
			while(ri->uri[n] && ri->uri[n] != '_') n++;
			if (k<=0 || 255<k)
				continue;
			call += (char)k;
		}
	if (post_data)
		delete[] post_data;
	call += " 2>&1";
	FILE* in;
//	printf("%s\n",call.c_str());
	fflush(NULL);



	struct pid volatile cur;
	int pdes[2];
	pid_t pid;

	if (pipe(pdes) < 0) {
		return;
	}

	switch (pid = fork()) {
	case -1:			/* Error. */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		return;
		/* NOTREACHED */
	case 0:				/* Child. */
	    {
		struct pid *pcur;
		int tpdes1 = pdes[1];

		(void) close(pdes[0]);
		/*
		 * We must NOT modify pdes, due to the
		 * semantics of vfork.
		 */
		if (tpdes1 != STDOUT_FILENO) {
			(void)dup2(tpdes1, STDOUT_FILENO);
			(void)close(tpdes1);
			tpdes1 = STDOUT_FILENO;
		}
		execl("/system/bin/sh", "sh", "-c", call.c_str(), (char *)NULL);
		_exit(127);
		/* NOTREACHED */
	    }
	}

	(void)close(pdes[1]);


//	if ((in = mypopen(call.c_str(),"r")) == NULL)
//		return;
	char buff[256];
	bool empty = true;
//	struct timeval tv;
//	gettimeofday(&tv,0);
//	time_t lasttime = tv.tv_sec;
	while (1)
	{
		fd_set set;
		struct timeval timeout;
		FD_ZERO(&set);
		FD_SET(pdes[0], &set);
		timeout.tv_sec = 10;
		timeout.tv_usec = 0;
		int s = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
		if (s>0)
		{
			int i = read(pdes[0],buff,255);
			if (i == 0 || exit_flag)
			{
				kill(pid,SIGKILL);
				break;
			}
			buff[i] = 0;
			mg_printf(conn,"%s",buff);
			empty = false;
		}
//		kill(pid,SIGKILL);
/*		while (fgets(buff, sizeof(buff)-1, in) != NULL)
		{
			int i = mg_printf(conn, "%s",buff);
			empty = false;
			if (buff[0] && i == 0)
				break;
			gettimeofday(&tv,0);
			if (lasttime + 120 < tv.tv_sec)
				break;
		}
*/
//		mypclose(in);
	}
	if (empty)
		mg_printf(conn,"</pre>empty<pre>");
}
static void
sendbroadcast(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
	mg_printf(conn,"OK");
	if (ri->remote_ip==2130706433 && strcmp(ri->remote_user,"JAVA_CLIENT") == 0) //localhost
	{
		lock_wakelock();
		syst("/system/bin/am broadcast -a \"android.intent.action.BOOT_COMPLETED\" -n com.android.mms/.transaction.SmsReceiver&");
	}
}

static void
intent(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"start intent");
	send_ok(conn);
	int n = 7;
	std::string call = "/system/bin/am start ";
        while (ri->uri[++n])
	{
		int k = getnum(ri->uri+n);
		while(ri->uri[n] && ri->uri[n] != '_') n++;
		if (k<=0 || 255<k)
			continue;
		char ch = (char)k;
		if ((ch >= 'a' && ch <= 'z') ||
			(ch >= 'A' && ch <= 'Z') || ch == ' ')
			call += ch;
		else
		{
			call += '\\';
			call += ch;
		}
	}
//	printf("%s\n",ri->uri+8);
//	printf("%s\n",call.c_str());

	syst(call.c_str());
}

static void
password(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
//	if (ri->remote_ip==2130706433 && exit_flag == 0) //localhost
//	{
//		mg_printf(conn,admin_password.c_str());
//	}
}

static std::string update_dyndns(__u32 ip)
{
	if (!ip)
		return "no IP address found";
	int s;
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		//error in opening socket
		return "error opening socket";
	}
	struct sockaddr_in addr;
	struct hostent *hp;
	if ((hp = gethostbyname("members.dyndns.org")) == NULL)
	{
		close(s);
		return "unable to resolve members.dyndns.org";
	}
	bcopy ( hp->h_addr, &(addr.sin_addr.s_addr), hp->h_length);
	addr.sin_port = htons(80);
	addr.sin_family = AF_INET;
	if (connect(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
	{
		close(s);
		return "unable to connect members.dyndns.org";
	}
	std::string data = "GET /nic/update?hostname=";
	data += dyndns_host+"&myip=";
	in_addr r;
	r.s_addr = ip;
	data += inet_ntoa(r);
	data += "&wildcard=NOCHG&mx=NOCHG&backmx=NOCHG HTTP/1.0\r\nHost: members.dyndns.org\r\nAuthorization: Basic ";
	data += dyndns_base64;
	data += "\r\nUser-Agent: Webkey\r\n\r\n";
	if (send(s, data.c_str(), data.size(),MSG_NOSIGNAL) < 0)
	{
		close(s);
		return "unable to send data to members.dyndns.org";
	}
	char buf[256];
	bzero(buf, sizeof(buf));
	if (read(s, buf, sizeof(buf)) < 0)
	{
		close(s);
		return "unable to receive data from members.dyndns.org";
	}
	int n = contains(buf,"\r\n\r\n");
	std::string ans;
        if(n!=0)
		ans = (buf+n+3);
	shutdown(s,SHUT_RDWR);
	close(s);
	if (contains(ans.c_str(),"good") || contains(ans.c_str(),"nochg"))
	{
		dyndns_last_updated_ip = ip;
		return "update's ok, dyndns answered: "+ans;
	}
	if (contains(ans.c_str(),"badauth") || contains(ans.c_str(),"!donator") || contains(ans.c_str(),"notfqdn")
			|| contains(ans.c_str(),"nohost") || contains(ans.c_str(),"numhost")
			|| contains(ans.c_str(),"abuse") || contains(ans.c_str(),"badagent"))
	{
		dyndns = false;
		return "dyndns rejected, their answer: "+ans;
	}
	return "unknown answer: "+ans;
}

static __u32 ipaddress()
{
	struct ifreq *ifr;
	struct ifconf ifc;
	int numif;
	int s, j;

	memset(&ifc, 0, sizeof(ifc));
	ifc.ifc_ifcu.ifcu_req = NULL;
	ifc.ifc_len = 0;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		//error in opening socket
		return 0;
	}
	if (ioctl(s, SIOCGIFCONF, &ifc) < 0) {
		perror("ioctl");
		return 0;
	}

	if ((ifr = new ifreq[ifc.ifc_len+2]) == NULL) {
		//error in the number of sockets
		close(s);
		return 0;
	}
	ifc.ifc_ifcu.ifcu_req = ifr;

	if (ioctl(s, SIOCGIFCONF, &ifc) < 0) {
		//error in getting the list
		close(s);
		return 0;
	}

	numif = ifc.ifc_len / sizeof(struct ifreq);
	__u32 ip = 0;
	__u32 ip192 = 0;
	for (j = 0; j < numif; j++)
	{
		struct ifreq *r = &ifr[j];
		struct ifreq c = *r;
		if (ioctl(s, SIOCGIFFLAGS, &c) < 0)
		{
			//error in getting a property
			close(s);
			return 0;
		}
		if ((c.ifr_flags & IFF_UP) && ((c.ifr_flags & IFF_LOOPBACK) == 0))
		{
			__u32 t = ((struct sockaddr_in *)&r->ifr_addr)->sin_addr.s_addr;
			if ((t&255) == 192 && ((t>>8)&255) == 168)
				ip192 = t;
			else
				ip = t;
		}

	}
	if (ip == 0)
	{
		ip = ip192;
	}
	delete[] ifr;
	close(s);
	return ip;
}
static void
dyndnsset(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
	if (ri->remote_ip!=2130706433 || strcmp(ri->remote_user,"JAVA_CLIENT") != 0) //localhost
	{
		return;
	}
	int n = strlen(ri->uri);
       	if (n == 7)
	{
		dyndns = false;
		mg_printf(conn,"stopped using dyndns\n");
		return;
	}
	dyndns = true;
	dyndns_last_updated_ip = 0;
	dyndns_host = "";
	dyndns_base64 = "";
	int i = 7;
	while (i<n && ri->uri[i]!='&')
	{
		dyndns_host += ri->uri[i];
		i++;
	}
	i++;
	while (i<n)
	{
		dyndns_base64 += ri->uri[i];
		i++;
	}
	__u32 ip = ipaddress();
	mg_printf(conn,"%s",update_dyndns(ip).c_str());
}


static void
uptime(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	FILE* f;
	double a,b;
	if ((f = fopen("/proc/uptime","r")) && fscanf(f,"%lf %lf\n",&a,&b) == 2)
	{
		int A = (int)a/60;
		int B = (int)b/60;
		mg_printf(conn,"%s: ",lang(ri,"uptime").c_str());
		mg_printf(conn,"<abbr title=\"%s: %d\">",lang(ri,"uptime secs").c_str(),(int)a);
		if (A < 2*60)
			mg_printf(conn,"%d %s</abbr>, ",A,lang(ri,"mins").c_str());
		else if (A < 2*24*60)
			mg_printf(conn,"%d %s</abbr>, ",A/60,lang(ri,"hours").c_str());
		else if ((A/60)%24 == 1)
		{
			mg_printf(conn,"%d %s ",A/60/24,lang(ri,"days").c_str());
			mg_printf(conn,"%d %s</abbr>, ",(A/60)%24,lang(ri,"hour").c_str());
		}
		else
		{
			mg_printf(conn,"%d %s ",A/60/24,lang(ri,"days").c_str());
			mg_printf(conn,"%d %s</abbr>, ",(A/60)%24,lang(ri,"hours").c_str());
		}
		if ((int)b)
		{
			mg_printf(conn,"%s: ",lang(ri,"CPUup").c_str());
			mg_printf(conn,"<abbr title=\"%s: %d\">",lang(ri,"CPU uptime secs").c_str(),(int)b);
			if (B < 2*60)
				mg_printf(conn,"%d %s</abbr>, ",B,lang(ri,"mins").c_str());
			else if (B < 2*24*60)
				mg_printf(conn,"%d %s</abbr>, ",B/60,lang(ri,"hours").c_str());
			else if ((B/60)%24 == 1)
			{
				mg_printf(conn,"%d %s ",B/60/24,lang(ri,"days").c_str());
				mg_printf(conn,"%d %s</abbr>, ",(B/60)%24,lang(ri,"hour").c_str());
			}
			else
			{
				mg_printf(conn,"%d %s ",B/60/24,lang(ri,"days").c_str());
				mg_printf(conn,"%d %s</abbr>, ",(B/60)%24,lang(ri,"hours").c_str());
			}
			mg_printf(conn,"</abbr>");
		}
	}
	if (f)
		fclose(f);
}

static void
meminfo(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	FILE* f;
	if (f = fopen("/proc/meminfo","r"))
	{
		char name[100];
		int size;
		while(fscanf(f,"%s %d kB\n",name,&size) == 2)
		{
			if (!strcmp(name,"MemTotal:") || !strcmp(name,"MemFree:") || ((!strcmp(name,"SwapTotal:") || !strcmp(name,"SwapFree:")) && size))
			{
				if (size < 1000)
					mg_printf(conn,"%s <span class=\"gray\">%d</span> kB, ",name,size);
				else
					mg_printf(conn,"%s %d<span class=\"gray\"> %03d</span> kB, ",name,size/1000,size%1000);
			}
		}
		fclose(f);
	}
}

static void
netinfo(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	FILE* f;
	if (f = fopen("/proc/net/dev","r"))
	{
		char name[200];
		int bin,bout,pin,pout,t;
		fgets(name,200,f);
		fgets(name,200,f);
		while(fscanf(f,"%s %d %d",name,&bin,&pin) == 3)
		{
			//if (!strcmp(name,"MemTotal:") || !strcmp(name,"MemFree:") || ((!strcmp(name,"SwapTotal:") || !strcmp(name,"SwapFree:")) && size))
			int i;
			for (i=0; i< 6;i++)
			{
				if (fscanf(f,"%d",&t) != 1)
					break;
			}
			if (i<6)
				break;
			if(fscanf(f,"%d %d",&bout,&pout) != 2)
				break;
			if (bin+bout && strcmp(name,"lo:"))
			{
				if (bin+bout < 1024*1000)
					mg_printf(conn,"<abbr title=\"%s kbytes in: %d, out: %d; packages in: %d, out: %d\">%s</abbr>.<span class=\"gray\">%d</span> kB, ",name,bin>>10,bout>>10,pin,pout,name,(bin+bout)>>10);
				else
					mg_printf(conn,"<abbr title=\"%s kbytes in: %d, out: %d; packages in: %d, out: %d\">%s</abbr> %d<span class=\"gray\"> %03d</span> kB, ",name,bin>>10,bout>>10,pin,pout,name,((bin+bout)>>10)/1000,((bin+bout)>>10)%1000);
			}
			fgets(name,200,f);
		}
		fclose(f);
	}
}

static void
status(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
//test
//        if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0)
//		return;
//	mg_printf(conn,"%d\n",scrinfo.yoffset);

	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();

	if (modelname[0])
		mg_printf(conn,"%s, ",modelname);

	FILE* f;
	double a,b;
	if ((f = fopen("/proc/uptime","r")) && fscanf(f,"%lf %lf\n",&a,&b) == 2)
	{
		int A = (int)a/60;
		int B = (int)b/60;
		mg_printf(conn,"%s: ",lang(ri,"uptime").c_str());
		mg_printf(conn,"<abbr title=\"%s: %d\">",lang(ri,"uptime secs").c_str(),(int)a);
		if (A < 2*60)
			mg_printf(conn,"%d %s</abbr>, ",A,lang(ri,"mins").c_str());
		else if (A < 2*24*60)
			mg_printf(conn,"%d %s</abbr>, ",A/60,lang(ri,"hours").c_str());
		else if ((A/60)%24 == 1)
		{
			mg_printf(conn,"%d %s ",A/60/24,lang(ri,"days").c_str());
			mg_printf(conn,"%d %s</abbr>, ",(A/60)%24,lang(ri,"hour").c_str());
		}
		else
		{
			mg_printf(conn,"%d %s ",A/60/24,lang(ri,"days").c_str());
			mg_printf(conn,"%d %s</abbr>, ",(A/60)%24,lang(ri,"hours").c_str());
		}
		if ((int)b)
		{
			mg_printf(conn,"%s: ",lang(ri,"CPUup").c_str());
			mg_printf(conn,"<abbr title=\"%s: %d\">",lang(ri,"CPU uptime secs").c_str(),(int)b);
			if (B < 2*60)
				mg_printf(conn,"%d %s</abbr>, ",B,lang(ri,"mins").c_str());
			else if (B < 2*24*60)
				mg_printf(conn,"%d %s</abbr>, ",B/60,lang(ri,"hours").c_str());
			else if ((B/60)%24 == 1)
			{
				mg_printf(conn,"%d %s ",B/60/24,lang(ri,"days").c_str());
				mg_printf(conn,"%d %s</abbr>, ",(B/60)%24,lang(ri,"hour").c_str());
			}
			else
			{
				mg_printf(conn,"%d %s ",B/60/24,lang(ri,"days").c_str());
				mg_printf(conn,"%d %s</abbr>, ",(B/60)%24,lang(ri,"hours").c_str());
			}
			mg_printf(conn,"</abbr>");
		}
	}
	if (f)
		fclose(f);
	if (f = fopen("/proc/meminfo","r"))
	{
		char name[100];
		int size;
		while(fscanf(f,"%s %d kB\n",name,&size) == 2)
		{
			if (!strcmp(name,"MemTotal:") || !strcmp(name,"MemFree:") || ((!strcmp(name,"SwapTotal:") || !strcmp(name,"SwapFree:")) && size))
			{
				if (size < 1000)
					mg_printf(conn,"%s <span class=\"gray\">%d</span> kB, ",name,size);
				else
					mg_printf(conn,"%s %d<span class=\"gray\"> %03d</span> kB, ",name,size/1000,size%1000);
			}
		}
		fclose(f);
	}
	if (f = fopen("/proc/net/dev","r"))
	{
		char name[200];
		int bin,bout,pin,pout,t;
		fgets(name,200,f);
		fgets(name,200,f);
		while(fscanf(f,"%s %d %d",name,&bin,&pin) == 3)
		{
			//if (!strcmp(name,"MemTotal:") || !strcmp(name,"MemFree:") || ((!strcmp(name,"SwapTotal:") || !strcmp(name,"SwapFree:")) && size))
			int i;
			for (i=0; i< 6;i++)
			{
				if (fscanf(f,"%d",&t) != 1)
					break;
			}
			if (i<6)
				break;
			if(fscanf(f,"%d %d",&bout,&pout) != 2)
				break;
			if (bin+bout && strcmp(name,"lo:"))
			{
				if (bin+bout < 1024*1000)
					mg_printf(conn,"<abbr title=\"%s kbytes in: %d, out: %d; packages in: %d, out: %d\">%s</abbr>.<span class=\"gray\">%d</span> kB, ",name,bin>>10,bout>>10,pin,pout,name,(bin+bout)>>10);
				else
					mg_printf(conn,"<abbr title=\"%s kbytes in: %d, out: %d; packages in: %d, out: %d\">%s</abbr> %d<span class=\"gray\"> %03d</span> kB, ",name,bin>>10,bout>>10,pin,pout,name,((bin+bout)>>10)/1000,((bin+bout)>>10)%1000);
			}
			fgets(name,200,f);
		}
		fclose(f);
	}

	int fd = open("/sys/class/leds/lcd-backlight/brightness", O_RDONLY);
	if (fd < 0)
		fd = open("/sys/class/backlight/pwm-backlight/brightness", O_RDONLY);
	char value[20];
	int n;
	if (fd >= 0)
	{
		n = read(fd, value, 10);
		if (n)
			mg_printf(conn,"%s: %d%%, ",lang(ri,"Brightness").c_str(),100*getnum(value)/max_brightness);
		close(fd);
	}
}

int remove_directory(const char *path)
{
   DIR *d = opendir(path);
   size_t path_len = strlen(path);
   int r = -1;
   if (d)
   {
      struct dirent *p;
      r = 0;
      while (!r && (p=readdir(d)))
      {
          int r2 = -1;
          char *buf;
          size_t len;
          if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
             continue;
	  std::string next = std::string(path) + std::string("/") + std::string(p->d_name);
	  struct stat statbuf;
             if (!stat(next.c_str(), &statbuf))
             {
                if (S_ISDIR(statbuf.st_mode))
                {
                   r2 = remove_directory(next.c_str());
                }
                else
                {
                   r2 = unlink(next.c_str());
                }
             }
          r = r2;
      }
      closedir(d);
   }
   if (!r)
   {
 	  r = rmdir(path);
   }
   return r;
}

static void
upload(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	printf("upload\n");
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"upload");
	char* post_data;
	int post_data_len = contentlen(conn);
	int read = 0;

	printf("contentlen = %d\n",post_data_len);
	std::string file = getoption(ri->uri,"file=");
	printf("file = %s\n",file.c_str());
	char to[FILENAME_MAX];
	strcpy(to,file.c_str());
	url_decode(to, strlen(to), to, FILENAME_MAX, true);
	remove_double_dots_and_double_slashes(to);

	mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/plain;\r\nConnection: close\r\n\r\n");

	FILE* out;
	if ((out = fopen(to, "w")) == NULL)
	{
		mg_printf(conn,"Unable to open file.\n");
		return;
	}

	while (read < post_data_len)
	{
		int next = post_data_len - read;
		if (next > 1048576) // 1 MB
			next = 1048576;
		post_data = new char[next+1];
		if (post_data == NULL)
		{
			mg_printf(conn, "Not enough memory.\n");
			return;
		}

		read += mg_read(conn,post_data,next);
		if (fwrite(post_data, 1, next, out) < next)
		{
			mg_printf(conn, "Failed to save file.\n");
			delete[] post_data;
			return;
		}
		delete[] post_data;
	}
	fclose(out);
	std::string perm = getoption(post_data,"file=");
	if (perm.size())
	{
		int p = atoi(perm.c_str());
		chmod(to, p);
	}

	mg_printf(conn, "ok");
}

static void
content(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT && (ri->permissions&PERM_SDCARD)==0)
		return;
	lock_wakelock();
	access_log(ri,"browse sdcard");
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
//	printf("request: %s\n",ri->query_string);
	//save edited file
	if (ri->query_string == NULL && post_data_len && startswith(post_data,"get_action=put_content"))
	{
		std::string file = getoption(post_data,"file=");
		char to[FILENAME_MAX];
		strcpy(to,file.c_str());
		url_decode(to, strlen(to), to, FILENAME_MAX, true);
		remove_double_dots_and_double_slashes(to);
		std::string code = getoption(post_data,"content=");
		char* c = new char[code.length()+1];
		strcpy(c,code.c_str());
		url_decode(c,strlen(c),c,strlen(c),true);
		mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/plain;\r\nConnection: close\r\n\r\n");
		if (post_data_len == 65536)
		{
			mg_printf(conn,"The file is too large.");
			delete[] c;
			return;
		}
		FILE* out;
		if (out = fopen(to, "w"))
		{
			fwrite(c, 1, strlen(c), out);
			fclose(out);
			mg_printf(conn,"The file has been saved successfully");
		}
		else
			mg_printf(conn,"1000");

		delete[] c;
		return;
	}
	int i;
	for (i = 0; i < ri->num_headers; i++)
	{
		if (!strncasecmp("Content-Type", ri->http_headers[i].name,12) &&
			!strncasecmp("multipart/form-data", ri->http_headers[i].value,19))
			break;
	}
//	printf("%d\n",__LINE__);
	if (i < ri->num_headers) //non-html5 upload
	{
//		printf("%d\n",__LINE__);
		i = 0;
		while (i < post_data_len && post_data[i] != '\r') i++;
		if (i>=post_data_len) {if (post_data) delete[] post_data; return;}
		post_data[i] = 0;
		std::string boundary = std::string("\r\n") + std::string(post_data);
		i+=2;
//		printf("%d\n",__LINE__);
		if (i>=post_data_len) {if (post_data) delete[] post_data; return;}
//		printf("%s\n",post_data+i);
		if (strncasecmp("Content-Disposition: form-data; name=\"userfile_",post_data+i,47))
			{if (post_data) delete[] post_data; return;}
		i += 47;
//		printf("%d\n",__LINE__);
		while (i < post_data_len && post_data[i] != '_') i++;
		i++;
		if (i>=post_data_len) {if (post_data) delete[] post_data; return;}
		int start = i;
		while (i < post_data_len && post_data[i] != '"') i++;
		if (i>=post_data_len) {if (post_data) delete[] post_data; return;}
		post_data[i] = 0;
//		printf("%d\n",__LINE__);
		url_decode(post_data+start, strlen(post_data+start), post_data+start, FILENAME_MAX, true);
		std::string dir = post_data+start;
//		printf("dir = %s\n",dir.c_str());
		while (i < post_data_len-4 && strncmp(post_data+i,"filename=\"",10)) i++;
		i += 10;
		start = i;
		while (i < post_data_len-4 && post_data[i] != '"') i++;
		if (i>=post_data_len) {if (post_data) delete[] post_data; return;}
//		printf("%d\n",__LINE__);
		post_data[i] = 0;
		std::string filename = post_data+start;
//		printf("filename = %s\n",filename.c_str());
		while (i < post_data_len-4 && strncmp(post_data+i,"\r\n\r\n",4)) i++;
		i+=4;
		if (i>=post_data_len) {if (post_data) delete[] post_data; return;}
		start = i;
//		printf("%d\n",__LINE__);
		send_ok(conn);
		FILE * f = fopen((dir+"/"+filename).c_str(),"w");
		if (!f)
		{
//		printf("%d\n",__LINE__);
			mg_printf(conn,"<html><script language=\"javascript\">if(parent.ajaxplorer.actionBar.multi_selector) parent.ajaxplorer.actionBar.multi_selector.submitNext('Error opening file for writing');</script></html>\n");
			if (post_data)
				delete[] post_data;
			return;
		}

		int already_read = 65536;
		while(post_data_len == 65536)
		{
			int s = fwrite(post_data+start,1,65536-500-start,f);
//			printf("s = %d\n",s);
//			printf("start = %d\n",start);
			if(s < 65536-500-start)
				{
					mg_printf(conn,"<html><script language=\"javascript\">if(parent.ajaxplorer.actionBar.multi_selector) parent.ajaxplorer.actionBar.multi_selector.submitNext('Error writing. Is disk full?');</script></html>\n");
					fclose(f);
					if (post_data)
						delete[] post_data;
					return;
				}
			start = 0;
			int l = contentlen(conn) - already_read;

			if (l > 65536-500)
				l = 65536-500;
			memcpy(post_data,post_data+65536-500,500);
			post_data_len = mg_read(conn,post_data+500,l) + 500;
			post_data[post_data_len] = 0;
			already_read += post_data_len - 500;
//			printf("post_data_len = %d\n",post_data_len);
		}
		i=0;
		while (i < post_data_len && strncmp(post_data+i,boundary.c_str(),boundary.size())) i++;
		if (i>=post_data_len)
		{
			mg_printf(conn,"<html><script language=\"javascript\">if(parent.ajaxplorer.actionBar.multi_selector) parent.ajaxplorer.actionBar.multi_selector.submitNext('Error, no boundary found at the end of the stream. Strange...');</script></html>\n");
			if (post_data)
				delete[] post_data;
			fclose(f);
			return;
		}
		if(fwrite(post_data+start,1,i-start,f) < i-start)
			{
				mg_printf(conn,"<html><script language=\"javascript\">if(parent.ajaxplorer.actionBar.multi_selector) parent.ajaxplorer.actionBar.multi_selector.submitNext('Error writing. Is disk full?');</script></html>\n");
				fclose(f);
				if (post_data)
					delete[] post_data;
				return;
			}
		fclose(f);
		mg_printf(conn,"<html><script language=\"javascript\">if(parent.ajaxplorer.actionBar.multi_selector) parent.ajaxplorer.actionBar.multi_selector.submitNext();</script></html>\n");
		if (post_data)
			delete[] post_data;
		return;
	}
	if (ri->query_string == NULL)
		return;
	//send_ok(conn);
	FILE* f = NULL;
	if (startswith(ri->query_string,"get_action=save_user_pref")
	|| startswith(ri->query_string,"get_action=switch_repository"))
	{
		return;
	}
	std::string action = getoption(ri->query_string,"action=");

	if (action == "get_boot_conf")
	{
		send_ok(conn,"Content-Type: text/javascript; charset=UTF-8");
		f = fo("ae_get_boot_conf","rb");
	}
	if (strcmp(ri->query_string,"get_action=get_template&template_name=flash_tpl.html&pluginName=uploader.flex&encode=false")==0)
	{
		send_ok(conn);
		f = fo("ae_get_template","rb");
		if (f)
		{
			fseek (f , 0 , SEEK_END);
			int lSize = ftell (f);
			rewind (f);
			char* filebuffer = new char[lSize+1];
			if (!filebuffer)
				return;
			fread(filebuffer,1,lSize,f);
			filebuffer[lSize] = 0;
			mg_write(conn,filebuffer,lSize);
			fclose(f);
			delete[] filebuffer;
//			int q;
//			for (q = 0; q < 32; q++)
//			{
//				token[q] = (char)(rand()%10+48);
//			}
//			mg_write(conn,token,32);
			f = NULL;
//			f =  fo("ae_get_template2","rb");
		}
	}
	if (action == "get_xml_registry")
	{
		send_ok(conn,"Content-Type: text/xml; charset=UTF-8");
		f = fo("ae_get_xml_registry","rb");
	}
	if (f)
	{
		fseek (f , 0 , SEEK_END);
		int lSize = ftell (f);
		rewind (f);
		char* filebuffer = new char[lSize+1];
		if (!filebuffer)
			return;
		fread(filebuffer,1,lSize,f);
		filebuffer[lSize] = 0;
		mg_write(conn,filebuffer,lSize);
		fclose(f);
		delete[] filebuffer;
		return;
	}
	if (action == "ls")
	{
//		printf("%s\n",ri->query_string);
		send_ok(conn,"Content-Type: text/xml; charset=UTF-8");
		std::string dp = getoption(ri->query_string,"dir=");
		char dirpath[FILENAME_MAX];
//		if (getoption(ri->query_string,"playlist=") == "true")
//		{
//			dp = base64_decode(dp);
//			strcpy(dirpath, dp.c_str());
//		}
//		else
//		{
			strcpy(dirpath, dp.c_str());
			url_decode(dirpath, strlen(dirpath), dirpath, FILENAME_MAX, true);
//		}
//		printf("%s\n",dirpath);
		if (!startswith(dirpath,"/sdcard"))
		{
			(void) mg_printf(conn, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree filename=\"\" text=\"\" is_file=\"false\"><tree is_file=\"false\" filename=\"/sdcard\" text=\"sdcard\" icon=\"folder.png\" mimestring=\"Directory\" is_image=\"0\"/></tree>");
			return;
		}
//			strcpy(dirpath,"/sdcard");
		remove_double_dots_and_double_slashes(dirpath);
//		printf("%s\n",dirpath);

		my_send_directory(conn,dirpath);
	}
	if (action == "download"
	 || action == "preview_data_proxy"
	 || action == "audio_proxy"
	 || action == "get_content"
	 || action == "open_with")
	{
//		printf("%s\n",ri->query_string);
		std::string fp = getoption(ri->query_string,"file=");
		if (action == "audio_proxy")
			fp = base64_decode(fp);
		char filepath[FILENAME_MAX];
		strcpy(filepath, fp.c_str());
		url_decode(filepath, strlen(filepath), filepath, FILENAME_MAX, true);
		if (!startswith(filepath,"/sdcard"))
			return;
		remove_double_dots_and_double_slashes(filepath);
		int i;
		for (i = strlen(filepath)-1; i>=0; i--)
			if (filepath[i]=='/')
				break;
//		printf("%s\n",filepath);
//		printf("%s\n",filepath+i+1);
		if (action=="preview_data_proxy")
		{
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: image/jpg; name=\"%s\"\r\nConnection: close\r\n\r\n",filepath+i+1);
//			printf("OK\n");
		}
		if (action == "open_with" || action == "get_content")
		{
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/plain;\r\nConnection: close\r\n\r\n");
		}
		if (action == "download")
		{
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nname=\"%s\"\r\nContent-Disposition: attachment; filename=\"%s\"\r\nConnection: close\r\n\r\n",filepath+i+1,filepath+i+1);
		}
		if (action == "audio_proxy")
		{
			mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: audio/mp3: name=\"%s\"\r\nConnection: close\r\n\r\n",filepath+i+1);
		}
		sendfile(filepath,conn);

		return;
	}
	if (action == "rename")
	{
		std::string fp = getoption(ri->query_string,"file=");
		char filepath[FILENAME_MAX];
		strcpy(filepath, fp.c_str());
		url_decode(filepath, strlen(filepath), filepath, FILENAME_MAX, true);
		if (!startswith(filepath,"/sdcard"))
			return;
		remove_double_dots_and_double_slashes(filepath);
		int i;
		for (i = strlen(filepath)-1; i>=0; i--)
			if (filepath[i]=='/')
				break;

		fp = getoption(ri->query_string,"filename_new=");
		char newfile[FILENAME_MAX];
		strcpy(newfile, filepath);
		newfile[i+1] = 0;
		strcat(newfile, fp.c_str());
		url_decode(newfile, strlen(newfile), newfile, FILENAME_MAX, true);
		remove_double_dots_and_double_slashes(newfile);

		int st = rename(filepath, newfile);
		send_ok(conn);
		if (st == 0)
		{
			char buff[LINESIZE];
			convertxml(buff,filepath);
			char buff2[LINESIZE];
			convertxml(buff2,fp.c_str());
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">%s has been renamed to %s</message><reload_instruction object=\"data\" node=\"\" file=\"%s\"/></tree>",buff,buff2,buff2);
		}
		else
		{
			char buff[LINESIZE];
			convertxml(buff,filepath);
			char buff2[LINESIZE];
			convertxml(buff2,fp.c_str());
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error renaming %s to %s</message></tree>",buff,buff2);
		}
		return;
	}
	if (action == "mkdir")
	{
		std::string fp = getoption(ri->query_string,"dir=");
		if (!startswith(fp.c_str(),"%2Fsdcard"))
			fp = "/sdcard/"+fp;
		char filepath[FILENAME_MAX];
		strcpy(filepath, fp.c_str());
		url_decode(filepath, strlen(filepath), filepath, FILENAME_MAX, true);

		std::string nd = getoption(ri->query_string,"dirname=");
		std::string dirname = fp + '/' + nd;
		char newdir[FILENAME_MAX];
		strcpy(newdir, dirname.c_str());
		url_decode(newdir, strlen(newdir), newdir, FILENAME_MAX, true);
		remove_double_dots_and_double_slashes(newdir);

		int st = mkdir(newdir, 0777);
		send_ok(conn);
		if (st == 0)
		{
			char buff[LINESIZE];
			convertxml(buff,newdir);
			char buff2[LINESIZE];
			convertxml(buff2,nd.c_str());
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The directory %s has been created.</message><reload_instruction object=\"data\" node=\"\" file=\"%s\"/></tree>",buff,buff2);
		}
		else
		{
			char buff[LINESIZE];
			convertxml(buff,newdir);
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error creating directory %s</message></tree>",buff);
		}
		return;
	}
	if (action == "mkfile")
	{
		std::string file = getoption(ri->query_string,"dir=") + "/" + getoption(ri->query_string,"filename=");
		if (!startswith(file.c_str(),"%2Fsdcard"))
			file = "/sdcard/"+file;

		char filepath[FILENAME_MAX];
		strcpy(filepath, file.c_str());
		url_decode(filepath, strlen(filepath), filepath, FILENAME_MAX, true);
		remove_double_dots_and_double_slashes(filepath);

		FILE* f;
		char buff[LINESIZE];
		convertxml(buff,filepath);
		if (f=fopen(filepath,"a"))
		{
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The file %s has been created.</message><reload_instruction object=\"data\" node=\"\" file=\"%s\"/></tree>",buff,buff);
			fclose(f);
			return;
		}
		mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error creating file %s</message></tree>",buff);
	}
	if (action == "delete" || action == "copy" || action == "move")
	{
		bool copy = (action == "copy");
		bool del = (action == "delete");
		bool move = (action == "move");
		std::string dest = getoption(ri->query_string,"dest=");
		if (!startswith(dest.c_str(),"%2Fsdcard"))
			dest = "/sdcard/"+dest;
		int i = -1;
		std::string file;
		int ok = 0;
		int failed = 0;
		char filepath[FILENAME_MAX];
		char to[FILENAME_MAX];


		while(1)
		{
			if (i == -1)
				file = getoption(ri->query_string,"file=");
			else
			{
				char name[32];
				strcpy(name,"file_");
				char num[32];
				itoa(num,i);
				strcat(name,num);
				strcat(name,"=");
				file = getoption(ri->query_string,name);
			}
			if (file == "" && i != -1)
				break;
			if (file == "" && i == -1)
			{
				i++;
				continue;
			}
			if (!startswith(file.c_str(),"%2Fsdcard"))
				file = "/sdcard/"+file;

			strcpy(filepath, file.c_str());
			url_decode(filepath, strlen(filepath), filepath, FILENAME_MAX, true);
			remove_double_dots_and_double_slashes(filepath);

			if (del)
			{
				  struct stat statbuf;
				  if (!stat(filepath, &statbuf))
				  {
					  int r = -1;
					  if (S_ISDIR(statbuf.st_mode))
						  r = remove_directory(filepath);
					  else
						  r = unlink(filepath);
					  if (!r)
						  ok++;
				  }
				  else
					  ok++; //opera bug
				//if (remove(filepath) == 0)
				//	system((std::string("rm -rf \"")+std::string(filepath)+std::string("\"")).c_str());
			}
			if (move)
			{
				strcpy(to,dest.c_str());
				url_decode(to, strlen(to), to, FILENAME_MAX, true);
				remove_double_dots_and_double_slashes(to);
				int j = strlen(to);
				if (j && to[j-1] != '/')
				{
					to[j++] = '/';
					to[j] = 0;
				}
				int n = strlen(filepath)+1;
				int i = n;
				for (;i>=0 && filepath[i] != '/';i--);
				for(i++; i < n; i++)
					to[j++] = filepath[i];
				to[j++] = 0;
				if (!rename(filepath, to))
					ok++;
//				else
//					printf("AAAAAAAA %s - %s\n",filepath,to);
			}
			if (copy)
			{
//				printf("COPY\n");
				strcpy(to,dest.c_str());
				url_decode(to, strlen(to), to, FILENAME_MAX, true);
				remove_double_dots_and_double_slashes(to);
				int j = strlen(to);
				if (j && to[j-1] != '/')
				{
					to[j++] = '/';
					to[j] = 0;
				}
				int n = strlen(filepath)+1;
				int i = n;
				for (;i>=0 && filepath[i] != '/';i--);
				for(i++; i < n; i++)
					to[j++] = filepath[i];
				to[j++] = 0;
				FILE* in, *out;
				char buff[65526];
				struct stat s;
				if (!stat(filepath,&s) && S_ISREG(s.st_mode) && (in = fopen(filepath, "r")))
				{
					if ((out = fopen(to, "w")))
					{
						while(j = fread(buff, 1, 65536, in))
						{
//							printf("%s - %s %d\n",filepath,to,j);
							fwrite(buff, 1, j, out);
						}
						fclose(out);
						ok++;
					}
					fclose(in);
				}
			}
//			printf("%d\n",i);
			i++;
		}
		char d[FILENAME_MAX];
		strcpy(d,dest.c_str());
		url_decode(d, strlen(d), d, FILENAME_MAX, true);
		remove_double_dots_and_double_slashes(d);
		if (i == 0 && del)
		{
			char buff[LINESIZE];
			convertxml(buff,filepath);
			if (ok == 0)
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error deleting file or directory %s</message></tree>",buff);
			else
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The file or directory %s has been deleted.</message><reload_instruction object=\"data\" node=\"\"/></tree>",buff);
		}
		if (i == 0 && move)
		{
			char buff[LINESIZE];
			convertxml(buff,filepath);
			char buff2[LINESIZE];
			convertxml(buff2,to);
			char buff3[LINESIZE];
			convertxml(buff3,d);
			if (ok == 0)
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error moving file or directory %s to %s.</message></tree>",buff,buff2);
			else
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The file or directory %s has been moved to %s.</message><reload_instruction object=\"data\" node=\"\"/><reload_instruction object=\"data\" node=\"%s\"/></tree>",buff,buff2,buff3);
		}
		if (i == 0 && copy)
		{
			char buff[LINESIZE];
			convertxml(buff,filepath);
			char buff2[LINESIZE];
			convertxml(buff2,to);
			char buff3[LINESIZE];
			convertxml(buff3,d);
			if (ok == 0)
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error copying file or directory %s to %s.</message></tree>",buff,buff2);
			else
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The file or directory %s has been copied to %s.</message><reload_instruction object=\"data\" node=\"\"/><reload_instruction object=\"data\" node=\"%s\"/></tree>",buff,buff2,buff3);
		}
		if (i>0 && del)
		{
			if (ok != i)
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error deleting some files or directories</message><reload_instruction object=\"data\" node=\"\"/></tree>");
			else
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The selected files and directories have been deleted.</message><reload_instruction object=\"data\" node=\"\"/></tree>");
		}
		if (i>0 && move)
		{
			char buff3[LINESIZE];
			convertxml(buff3,d);
			if (ok != i)
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error moving some files or directories</message><reload_instruction object=\"data\" node=\"\"/><reload_instruction object=\"data\" node=\"%s\"/></tree>",buff3);
			else
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The selected files and directories have been moved.</message><reload_instruction object=\"data\" node=\"\"/><reload_instruction object=\"data\" node=\"%s\"/></tree>",buff3);
		}
		if (i>0 && copy)
		{
			char buff3[LINESIZE];
			convertxml(buff3,d);
			if (ok != i)
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Error copying some files or directories</message><reload_instruction object=\"data\" node=\"\"/><reload_instruction object=\"data\" node=\"%s\"/></tree>",buff3);
			else
				mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The selected files and directories have been copied.</message><reload_instruction object=\"data\" node=\"\"/><reload_instruction object=\"data\" node=\"%s\"/></tree>",buff3);
		}
	}
	if (action == "upload")
	{
		//<html><script language="javascript">
		//
		// if(parent.ajaxplorer.actionBar.multi_selector) parent.ajaxplorer.actionBar.multi_selector.submitNext();</script></html>


		//send_ok(conn);
/*		mg_printf(conn,"HTTP/1.1 100 Continue\r\n\r\n");
//		printf("%s\n",ri->post_data);
		int i = 0;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		int start = i;
		while(i < post_data_len && post_data[i]!='\r') i++;
		if (i >= post_data_len)
		{
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Upload failed.</message><reload_instruction object=\"data\" node=\"\"/></tree>");
			return;
		}
		post_data[i] = 0;
		std::string file = (post_data + start);
		i+=2;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		start = i;
		while(i < post_data_len && post_data[i]!='\r') i++;
		if (i >= post_data_len)
		{
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Upload failed.</message><reload_instruction object=\"data\" node=\"\"/></tree>");
			return;
		}
		post_data[i] = 0;
		std::string dir = base64_decode(post_data + start);
		i+=2;
		if (!startswith(dir.c_str(),"/sdcard"))
			dir = "/sdcard/"+dir;
		file = dir+'/'+file;
		char filepath[FILENAME_MAX];
		if (file.size() >= FILENAME_MAX-1)
		{
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Upload failed.</message><reload_instruction object=\"data\" node=\"\"/></tree>");
			return;
		}
		strcpy(filepath,file.c_str());
		remove_double_dots_and_double_slashes(filepath);
//		printf("path = %s\n",filepath);
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		while(i < post_data_len && post_data[i]!='\n') i++; i++;
		if (i >= post_data_len)
		{
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Upload failed.</message><reload_instruction object=\"data\" node=\"\"/></tree>");
			return;
		}
		start = i;
		FILE* f = fopen(filepath,"w");
		if (!f)
		{
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Upload failed.</message><reload_instruction object=\"data\" node=\"\"/></tree>");
			return;
		}


		int already_read = 65536;
		while(post_data_len == 65536)
		{
			fwrite(post_data+start,1,65536-500-start,f);
			start = 0;
			int l = contentlen(conn) - already_read;

			if (l > 65536-500)
				l = 65536-500;
			memcpy(post_data,post_data+65536-500,500);
			post_data_len = mg_read(conn,post_data+500,l) + 500;
			post_data[post_data_len] = 0;
			already_read += post_data_len - 500;
		}

		i = post_data_len-1;
		while(i >= 0 && post_data[i]!='\n') i--; i--;
		while(i >= 0 && post_data[i]!='\n') i--; i--;
		while(i >= 0 && post_data[i]!='\n') i--; i--;
		while(i >= 0 && post_data[i]!='\n') i--; i--;
		while(i >= 0 && post_data[i]!='\n') i--; i--;
		while(i >= 0 && post_data[i]!='\n') i--; i--;
		if (i < start)
		{
			mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"ERROR\">Upload failed.</message><reload_instruction object=\"data\" node=\"\"/></tree>");
			return;
		}
		fwrite(post_data+start,1,i-start,f);
		fclose(f);
		mg_printf(conn,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><tree><message type=\"SUCCESS\">The file uploaded to %s.</message><reload_instruction object=\"data\" node=\"\"/></tree>",filepath);
//		printf("upload OK\n");
*/
		send_ok(conn);
		std::string dir = getoption(ri->query_string,"dir=");
		char to[FILENAME_MAX];
		strcpy(to,dir.c_str());
		url_decode(to, strlen(to), to, FILENAME_MAX, true);
		int i;
		std::string file;
		for (i = 0; i < ri->num_headers; i++)
			if (!strcasecmp("X-File-Name", ri->http_headers[i].name))
			{
				file = ri->http_headers[i].value;
				break;
			}
		if (i == ri->num_headers)
		{
			mg_printf(conn,"There should be an X-File-Name in the HTTP header list.");
			return;
		}
		if (!startswith(to,"/sdcard"))
			file = std::string("/sdcard/")+std::string(to)+'/'+file;
		else
			file = std::string(to)+'/'+file;
		char filepath[FILENAME_MAX];
		if (file.size() >= FILENAME_MAX-1)
		{
			mg_printf(conn,"Too long filename");
			return;
		}
		strcpy(filepath,file.c_str());
		remove_double_dots_and_double_slashes(filepath);
		//printf("%s\n",filepath);
		FILE* f = fopen(filepath,"w");
		if (!f)
		{
			mg_printf(conn,"Bad Request3");
			return;
		}


		int already_read = 0;
		while(post_data_len)
		{
			already_read += post_data_len;
			fwrite(post_data,1,post_data_len,f);
			int l = contentlen(conn) - already_read;
			if (l > 65536)
				l = 65536;
			post_data_len = mg_read(conn,post_data,l);
			post_data[post_data_len] = 0;
		}
		fwrite(post_data,1,post_data_len,f);
		fclose(f);
		mg_printf(conn,"OK");
	}
	if (post_data)
		delete[] post_data;
}

static void
testfb(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"run testfb");
	send_ok(conn);

        size_t pixels;

//        pixels = scrinfo.xres_virtual * scrinfo.yres_virtual;
        pixels = scrinfo.xres * scrinfo.yres;
        bytespp = scrinfo.bits_per_pixel / 8;
	if (bytespp == 3)
		bytespp = 4;

        mg_printf(conn,"We might read from unused memory, it might happen that Webkey gets stopped.\n");
        mg_printf(conn,"xres=%d, yres=%d, xresv=%d, yresv=%d, xoffs=%d, yoffs=%d, bpp=%d\n",
          (int)scrinfo.xres, (int)scrinfo.yres,
          (int)scrinfo.xres_virtual, (int)scrinfo.yres_virtual,
          (int)scrinfo.xoffset, (int)scrinfo.yoffset,
          (int)scrinfo.bits_per_pixel);



	int rr = scrinfo.red.offset;
	int rl = 8-scrinfo.red.length;
	int gr = scrinfo.green.offset;
	int gl = 8-scrinfo.green.length;
	int br = scrinfo.blue.offset;
	int bl = 8-scrinfo.blue.length;
	int j;
	mg_printf(conn,"rr=%d, rl=%d, gr=%d, gl=%d, br=%d, bl=%d\n",rr,rl,gr,gl,br,bl);

	mg_printf(conn,"nonstd=%d\n",scrinfo.nonstd);
	mg_printf(conn,"activate=%d\n",scrinfo.activate);
	mg_printf(conn,"height=%d\n",scrinfo.height);
	mg_printf(conn,"width=%d\n",scrinfo.width);
	mg_printf(conn,"accel_flags=%d\n",scrinfo.accel_flags);
	mg_printf(conn,"pixclock=%d\n",scrinfo.pixclock);
	mg_printf(conn,"left_margin=%d\n",scrinfo.left_margin);
	mg_printf(conn,"right_margin=%d\n",scrinfo.right_margin);
	mg_printf(conn,"upper_margin=%d\n",scrinfo.upper_margin);
	mg_printf(conn,"lower_margin=%d\n",scrinfo.lower_margin);
	mg_printf(conn,"hsync_len=%d\n",scrinfo.hsync_len);
	mg_printf(conn,"vsync_len=%d\n",scrinfo.vsync_len);
	mg_printf(conn,"sync=%d\n",scrinfo.sync);
	mg_printf(conn,"vmode=%d\n",scrinfo.vmode);
	mg_printf(conn,"rotate=%d\n",scrinfo.rotate);
	mg_printf(conn,"reserved[0]=%d\n",scrinfo.reserved[0]);
	mg_printf(conn,"reserved[1]=%d\n",scrinfo.reserved[1]);
	mg_printf(conn,"reserved[2]=%d\n",scrinfo.reserved[2]);
	mg_printf(conn,"reserved[3]=%d\n",scrinfo.reserved[3]);
	mg_printf(conn,"reserved[4]=%d\n",scrinfo.reserved[4]);

	int m = scrinfo.yres*scrinfo.xres_virtual;
	int offset = scrinfo.yoffset*scrinfo.xres_virtual+scrinfo.xoffset;

	if (!pict)
		init_fb();
	memcpy(copyfb,(char*)fbmmap+offset*bytespp,m*bytespp);

	mg_printf(conn,"offset*bytespp=%d\n",offset*bytespp);
	mg_printf(conn,"m*bytespp=%d\n",m*bytespp);

	if (!graph)
		init_fb();
	if (!graph)
	{
		mg_printf(conn,"graph is null\n");
		return;
	}

	int i;
	unsigned short int* map = (unsigned short int*)fbmmap;
	unsigned short int* map2 = (unsigned short int*)copyfb;
	for (i = 0; i < 500; i++)
	{
		mg_printf(conn,"map[%d] is %d\n",100*i+i,map[100*i+i]);
		mg_printf(conn,"map2[%d] is %d\n",100*i+i,map2[100*i+i]);
	}

}

static void
testtouch(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	lock_wakelock();
	access_log(ri,"run testtouch");
	send_ok(conn);
	if (is_icecreamsandwich)
		mg_printf(conn,"this is icrecreamsandwich\n");
	if (use_uinput_mouse)
		mg_printf(conn,"using uinput for mouse, type = %d\n", use_uinput_mouse);
	int i;
#ifdef MYDEB
	char touch_device[27] = "/android/dev/input/event0";
#else
	char touch_device[19] = "/dev/input/event0";
#endif
	int tfd = -1;
	for (i=0; i<50; i++)
	{
		char name[256]="Unknown";
		if (i < 10)
		{
			touch_device[sizeof(touch_device)-3] = '0'+(char)(i);
			touch_device[sizeof(touch_device)-2] = 0;
		}
		else
		{
			touch_device[sizeof(touch_device)-3] = '0'+(char)(i/10);
			touch_device[sizeof(touch_device)-2] = '0'+(char)(i%10);
			touch_device[sizeof(touch_device)-1] = 0;
		}
		struct input_absinfo info;
		if((tfd = open(touch_device, O_RDWR)) == -1)
		{
			continue;
		}
		mg_printf(conn,"searching for touch device, opening %s ... ",touch_device);
		if (ioctl(tfd, EVIOCGNAME(sizeof(name)),name) < 0)
		{
			mg_printf(conn,"failed, no name\n");
			close(tfd);
			tfd = -1;
			continue;
		}
		mg_printf(conn,"device name is %s\n",name);
		//
		// Get the Range of X and Y
		if(ioctl(tfd, EVIOCGABS(ABS_X), &info))
		{
			printf("failed, no ABS_X\n");
			close(tfd);
			tfd = -1;
			continue;
		}
		xmin = info.minimum;
		xmax = info.maximum;
		if (xmin == 0 && xmax == 0)
		{
			if(ioctl(tfd, EVIOCGABS(53), &info))
			{
				printf("failed, no ABS_X\n");
				close(tfd);
				tfd = -1;
				continue;
			}
			xmin = info.minimum;
			xmax = info.maximum;
		}

		if(ioctl(tfd, EVIOCGABS(ABS_Y), &info)) {
			printf("failed, no ABS_Y\n");
			close(tfd);
			tfd = -1;
			continue;
		}
		ymin = info.minimum;
		ymax = info.maximum;
		if (ymin == 0 && ymax == 0)
		{
			if(ioctl(tfd, EVIOCGABS(54), &info))
			{
				printf("failed, no ABS_Y\n");
				close(tfd);
				tfd = -1;
				continue;
			}
			ymin = info.minimum;
			ymax = info.maximum;
		}
		bool t = contains(name,"touch");
		bool tk = contains(name,"touchkey");
		if (t && !tk)
			printf("there is \"touch\", but not \"touchkey\" in the name\n");
		if (!(t && !tk) && (xmin < 0 || xmin == xmax))	// xmin < 0 for the compass
		{
			printf("failed, xmin<0 || xmin==xmax\n");
			close(tfd);
			tfd = -1;
			continue;
		}
		mg_printf(conn,"we choose this one\n");
#ifdef ANDROID
		__android_log_print(ANDROID_LOG_INFO,"Webkey C++","using touch device: %s",touch_device);
#endif
		ymin = info.minimum;
		ymax = info.maximum;
		mg_printf(conn,"xmin = %d, xmax = %d, ymin = %d, ymax = %d\n",xmin,xmax,ymin,ymax);
	}
	mg_printf(conn,"Please use the touchscreen on the phone for a while.\n");
	if (is_icecreamsandwich)
		mg_printf(conn, "it's ice cream sandwich\n");
	else
		mg_printf(conn, "it's not ice cream sandwich\n");
	for (i = 0; i < 500; i++)
	{
		struct input_event ev;
		if(read(touchfd, &ev, sizeof(ev)) < 0)
			mg_printf(conn,"touchfd read failed.\n");
		else
		{
			char buff[32];
			std::string m = "ev.type = ";
			if (ev.type == 3)
				m = m+"ABS("+itoa(buff, ev.type)+")";
			else if (ev.type == 0)
				m = m+"SYN("+itoa(buff, ev.type)+")";
			else
				m = m+itoa(buff, ev.type);
			m += ", ev.code = ";
			if (ev.code == 53)
				m = m+"X("+itoa(buff, ev.code)+")";
			else if (ev.code == 54)
				m = m+"Y("+itoa(buff, ev.code)+")";
			else if (ev.code == 58)
				m = m+"TOUCH("+itoa(buff, ev.code)+")";
			else if (ev.code == 48)
				m = m+"TOUCH("+itoa(buff, ev.code)+")";
			else if (ev.code == 330)
				m = m+"TOUCH("+itoa(buff, ev.code)+")";
			else
				m = m+itoa(buff, ev.code);

			m = m+ ", ev.value = "+itoa(buff, ev.value);

			//mg_printf(conn,"ev.type = %d, ev.code = %d, ev.value = %d\n",ev.type,ev.code,ev.value);
			mg_printf(conn,"%s\n",m.c_str());
		}
	}
	mg_printf(conn,"Finished.\n");
}

static void read_prefs()
{
	mylog("read_prefs()","%s");
	std::string sharedpref = dir + "../shared_prefs/com.webkey_preferences.xml";
	server = false;
	dyndns = false;
	FILE* sp = fopen(sharedpref.c_str(),"r");
	if (!sp)
		sp = fopen("/dbdata/databases/com.webkey/shared_prefs/com.webkey_preferences.xml","r");
	mylog(sp,"%d");
	if (sp)
	{
		char buff[256];
		while (fgets(buff, sizeof(buff)-1, sp) != NULL)
		{
			mylog(buff,"%s");
			int n = strlen(buff);
			mylog(n,"%d");
			if (startswith(buff,"<string name=\"dddomain\">"))
			{
				int i = 0;
				for(i=24;i<n;i++)
					if (buff[i]=='<')
					{
						buff[i]=0;
						break;
					}
				dyndns_base64 = (buff+24);
				mylog(dyndns_base64.c_str(),"%s");
			}
			if (startswith(buff,"<string name=\"hash\">"))
			{
				int i = 0;
				for(i=20;i<n;i++)
					if (buff[i]=='<')
					{
						buff[i]=0;
						break;
					}
				dyndns_base64 = (buff+20);
				mylog(dyndns_base64.c_str(),"%s");
			}
			if (startswith(buff,"<boolean name=\"ddusing\" value=\"true\""))
			{
				dyndns = true;
				mylog(dyndns,"%d");
			}
			if (startswith(buff,"<string name=\"port\">"))
			{
				int i = 0;
				for(i=20;i<n;i++)
					if (buff[i]=='<')
					{
						buff[i]=0;
						break;
					}
				port = strtol(buff+20, 0, 10);
				mylog(port,"%d");
			}
			if (startswith(buff,"<string name=\"sslport\">"))
			{
				int i = 0;
				for(i=23;i<n;i++)
					if (buff[i]=='<')
					{
						buff[i]=0;
						break;
					}
				sslport = strtol(buff+23, 0, 10);
				mylog(sslport,"%d");
			}
			if (startswith(buff,"<string name=\"random\">"))
			{
				int i = 0;
				for(i=22;i<n;i++)
					if (buff[i]=='<')
					{
						buff[i]=0;
						break;
					}
				mylog(buff,"%s");
				mylog(server_random,"%s");
				if (!server_random || strcmp(server_random,buff+22))
				{
					char * t = server_random;
					server_random = new char[n];
					strcpy(server_random,buff+22);
					mylog(server_random,"%s");
					if (t)
						delete[] t;
				}
			}
			if (startswith(buff,"<string name=\"username\">"))
			{
				int i = 0;
				for(i=24;i<n;i++)
					if (buff[i]=='<')
					{
						buff[i]=0;
						break;
					}
				mylog(buff,"%s");
				mylog(server_username,"%s");
				if (!server_username || strcmp(server_username,buff+24))
				{
					char * t = server_username;
					server_username = new char[n];
					strcpy(server_username,buff+24);
					mylog(server_username,"%s");
					if (t)
						delete[] t;
				}


#ifdef FORCE_USERNAME
					char * t = server_username;
					server_username = new char[strlen(FORCE_USERNAME)+1];
					strcpy(server_username,FORCE_USERNAME);
					mylog(server_username,"%s");
					if (t)
						delete[] t;
#endif


			}
			if (startswith(buff,"<boolean name=\"server\" value=\"true\""))
			{
				server = true;
				mylog(server,"%d");
			}
		}
		fclose(sp);
	}
}

static void
reread(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	send_ok(conn);
	if (ri->remote_ip!=2130706433 || strcmp(ri->remote_user,"JAVA_CLIENT") != 0) //localhost
	{
//		printf("reread request failed, permission's denied.\n");
		return;
	}
	read_prefs();
	server_changes++;
}
static void
test(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
    mg_printf(conn,"Webkey");
}
static void
javatest(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
    send_ok(conn);
    mg_printf(conn,"Webkey");
}

//from ShellInABox
static char *jsonEscape(const char *buf, int len) {
	static const char *hexDigit = "0123456789ABCDEF";
  // Determine the space that is needed to encode the buffer
  int count                   = 0;
  const char *ptr             = buf;
  for (int i = 0; i < len; i++) {
    unsigned char ch          = *(unsigned char *)ptr++;
    if (ch < ' ') {
      switch (ch) {
      case '\b': case '\f': case '\n': case '\r': case '\t':
        count                += 2;
        break;
      default:
        count                += 6;
        break;
      }
    } else if (ch == '"' || ch == '\\' || ch == '/') {
      count                  += 2;
    } else if (ch > '\x7F') {
      count                  += 6;
    } else {
      count++;
    }
  }

  // Encode the buffer using JSON string escaping
  char *result;
  result                = new char[count + 1];
  char *dst                   = result;
  ptr                         = buf;
  for (int i = 0; i < len; i++) {
    unsigned char ch          = *(unsigned char *)ptr++;
    if (ch < ' ') {
      *dst++                  = '\\';
      switch (ch) {
      case '\b': *dst++       = 'b'; break;
      case '\f': *dst++       = 'f'; break;
      case '\n': *dst++       = 'n'; break;
      case '\r': *dst++       = 'r'; break;
      case '\t': *dst++       = 't'; break;
      default:
      unicode:
        *dst++                = 'u';
        *dst++                = '0';
        *dst++                = '0';
        *dst++                = hexDigit[ch >> 4];
        *dst++                = hexDigit[ch & 0xF];
        break;
      }
    } else if (ch == '"' || ch == '\\' || ch == '/') {
      *dst++                  = '\\';
      *dst++                  = ch;
    } else if (ch > '\x7F') {
      *dst++                  = '\\';
      goto unicode;
    } else {
      *dst++                  = ch;
    }
  }
  *dst++                      = '\000';
  return result;
}

// from Android-Terminal-Emulator, check
// http://github.com/jackpal/Android-Terminal-Emulator/blob/master/jni/termExec.cpp
static int create_subprocess(pid_t* pProcessId)
{
    char *devname;
   // int ptm;
    pid_t pid;

    int ptm = open("/dev/ptmx", O_RDWR); // | O_NOCTTY);
    if(ptm < 0){
        printf("[ cannot open /dev/ptmx - %s ]\n",strerror(errno));
        return -1;
    }
    fcntl(ptm, F_SETFD, FD_CLOEXEC);

    if(grantpt(ptm) || unlockpt(ptm) ||
       ((devname = (char*) ptsname(ptm)) == 0)){
        printf("[ trouble with /dev/ptmx - %s ]\n", strerror(errno));
        return -1;
    }


    pid = fork();
    if(pid < 0) {
        printf("- fork failed: %s -\n", strerror(errno));
        return -1;
    }

    if(pid == 0){
	    close(ptm);
	    int pts;
	    setsid();
	    pts = open(devname, O_RDWR);
        if(pts < 0) exit(-1);

	fflush(NULL);
        dup2(pts, 0);
        dup2(pts, 1);
        dup2(pts, 2);

	char** env = new char*[6];
	env[0] = "TERM=xterm";
	env[1] = "LINES=25";
	env[2] = "COLUMNS=80";
	env[3] = "HOME=/sdcard";
	env[4] = "PATH=/data/local/bin:/usr/bin:/usr/sbin:/bin:/sbin:/system/bin:/system/xbin:/system/xbin/bb:/system/sbin";
	env[5] = 0;
        execle("/system/bin/bash", "/system/bin/bash", NULL, env);
        execle("/system/xbin/bash", "/system/xbin/bash", NULL, env);
        execle("/system/xbin/bb/bash", "/system/xbin/bb/bash", NULL, env);
        execle("/system/bin/sh", "/system/bin/sh", NULL, env);
//        execle("/bin/bash", "/bin/bash", NULL, env);
        exit(-1);
    } else {
	usleep(100000);
        *pProcessId = pid;
	//char buf[200];
//	write(ptm,"# Closed after 60 minutes of inactivity.\n",8);
        return ptm;
    }
}

static void
shellinabox(struct mg_connection *conn,
                const struct mg_request_info *ri, void *data)
{
	if (ri->permissions != PERM_ROOT)
		return;
	access_log(ri,"run terminal");
	char* post_data;
	int post_data_len;
	read_post_data(conn,ri,&post_data,&post_data_len);
	int i = 0;
	std::string param;
	std::string value;
	int p = 0;
	int width = 0;
	int height = 0;
	std::string sessionstr;
	std::string keys;
	bool keys_received = false;
	bool root_url = false;
	while (true)
	{
		if (i== post_data_len || post_data[i] == '&')
		{
			p = 0;
			if (param == "width")
				width = atoi(value.c_str());
			else if (param == "height")
				height = atoi(value.c_str());
			else if (param == "session")
				sessionstr = value;
			else if (param == "keys")
			{
				keys = value;
				keys_received = true;
			}
			else if (param == "rooturl")
				root_url = true;

			param = "";
			value = "";
		}
		else
		if (post_data[i] == '=')
		{
			p = 1;
		}
		else
		{
			if (p)
				value += post_data[i];
			else
				param += post_data[i];
		}
		if (i == post_data_len)
			break;
		i++;
	}
	send_ok(conn,"Content-type: application/json; charset=utf-8");
	SESSION* session = NULL;
	int sessionid = -1;
	int ptm = 0;
	if (sessionstr.length())
		sessionid = atoi(sessionstr.c_str());
	if ( root_url )
	{
		int pid;
		while (1)
		{
			ptm = create_subprocess(&pid);
			char buf[6];
			fd_set set;
			struct timeval timeout;
			FD_ZERO(&set);
			FD_SET(ptm, &set);
			timeout.tv_sec = 0;
			timeout.tv_usec = 100;
			int s = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
			if (s>0)
			{
				read(ptm,buf,6);
				break;
			}
			kill(pid,SIGKILL);
		}
		session = new SESSION;
		session->pid = pid;
		session->ptm = ptm;
		session->oldwidth = 180;
		session->oldheight = 25;
		session->alive = true;
		struct timeval tv;
		gettimeofday(&tv,0);
		session->lastused = tv.tv_sec;
		pthread_mutex_init(&(session->mutex), NULL);
		sessions.push_back(session);
		if (ptm >= 0)
			mg_printf(conn,"{\"session\":\"%d\",\"data\":\"\"}\n",sessions.size()-1);
		//	mg_printf(conn,"{\"session\":\"%d\",\"data\":\"%s\\r\\n\"}\n",sessions.size()-1,lang(ri,"This terminal will be closed after 60 minutes of inactivity."));
		printf("terminal started\n");
		if (post_data)
			delete[] post_data;
		return;
	}
	if (sessionid == -1 || sessionid >= sessions.size())
	{
//		printf("Wrong sessionid\n");
		if (post_data)
			delete[] post_data;
		return;
	}
	session = sessions[sessionid];
	if (session->alive == false)
	{
		if (post_data)
			delete[] post_data;
		return;
	}
	struct timeval tv;
	gettimeofday(&tv,0);
	session->lastused = tv.tv_sec;

	if (width && height && (width != session->oldwidth || height != session->oldheight))
	{
	      struct winsize win;
	      ioctl(session->ptm, TIOCGWINSZ, &win);
	      win.ws_row   = height;
	      win.ws_col   = width;
	      ioctl(session->ptm, TIOCSWINSZ, &win);
	      session->oldwidth = width;
	      session->oldheight = height;
	      //printf("size updated\n");
	}

//	printf("POST %s\n",ri->post_data);
  if (keys_received) {
//	  printf("%s\n",keys.c_str());
    lock_wakelock();
    char *keyCodes;
    keyCodes = new char[keys.length()/2];
    int len               = 0;
    for (const unsigned char *ptr = (const unsigned char *)keys.c_str(); ;) {
      unsigned c0         = *ptr++;
      if (c0 < '0' || (c0 > '9' && c0 < 'A') ||
          (c0 > 'F' && c0 < 'a') || c0 > 'f') {
        break;
      }
      unsigned c1         = *ptr++;
      if (c1 < '0' || (c1 > '9' && c1 < 'A') ||
          (c1 > 'F' && c1 < 'a') || c1 > 'f') {
        break;
      }
      keyCodes[len++]     = 16*((c0 & 0xF) + 9*(c0 > '9')) +
                                (c1 & 0xF) + 9*(c1 > '9');
    }
    write(session->ptm, keyCodes, len);
    delete[] keyCodes;
    if (post_data)
	delete[] post_data;
    return;
  }
  if (post_data)
	  delete[] post_data;
  pthread_mutex_lock(&(session->mutex));
  char buf[2048];
  fd_set set;
  struct timeval timeout;
  FD_ZERO(&set);
  FD_SET(session->ptm, &set);
  timeout.tv_sec = 30;
  timeout.tv_usec = 0;
  int s = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
  int n = 1;
  if (s == 1)
	  n = read(session->ptm,buf,2047);
  else if (s == 0)
  {
	  mg_printf(conn,"{\"session\":\"%d\",\"data\":\"\"}\n",sessionid);
	  pthread_mutex_unlock(&(session->mutex));
	  return;
  }
//  if (n<=0 || (n==4 && buf[0] == '\x1B' && buf[1] == '[' && buf[2] == '3' && buf[3] == 'n'))
  if (s<0 || n <= 0)
  {
	  session->alive = false;
	  close(session->ptm);
//	  printf("CLOSED TERMINAL\n");
	  pthread_mutex_unlock(&(session->mutex));
	  return;
  }
  char* t = jsonEscape(buf,n);
//  printf("%d: ",n);
//  for (i=0; i < n; i++)
//	  printf("%d, ",buf[i]);
//  printf("\n");
//  if (n==4 && buf[0] == '\x1B' && buf[1] == '[' && buf[2] == '0' && buf[3] == 'n')
//	  mg_printf(conn,"{\"session\":\"%d\",\"data\":\"\"}\n",sessionid);
//  else
	mg_printf(conn,"{\"session\":\"%d\",\"data\":\"%s\"}\n",sessionid,t);
//  printf("%s\n",t);
  delete[] t;
  pthread_mutex_unlock(&(session->mutex));
}

static void *event_handler(enum mg_event event,
                           struct mg_connection *conn,
                           const struct mg_request_info *request_info) {
  if (event != MG_NEW_REQUEST)
	  return 0;
  printf("u: ~%s~\n",request_info->uri);
  fflush(NULL);
  //access_log(request_info,"log in");
  void *processed = (void*)1;
  if (urlcompare(request_info->uri, "/screenshot.*"))
	screenshot(conn, request_info, NULL);
#if 0
  else if (urlcompare(request_info->uri, "/") || urlcompare(request_info->uri,"/index.html"))
		getfile(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/phone.html"))
  {
      index(conn, request_info, NULL);
  }
  else if (urlcompare(request_info->uri, "/main.html")||
		  urlcompare(request_info->uri, "/pure_menu.html")||
		  urlcompare(request_info->uri, "/pure_menu_nochat.html")||
		  urlcompare(request_info->uri, "/chat.html")||
		  urlcompare(request_info->uri, "/sms.html")||
		  urlcompare(request_info->uri, "/terminal.html")||
		  urlcompare(request_info->uri, "/js/webkey.js") ||
		  urlcompare(request_info->uri, "/js/screenshot.js")
		  )
	getfile(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/setpassword"))
	setpassword(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/setpermission"))
	setpermission(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/register"))
	reg(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/getreg"))
	getreg(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/injkey*"))
	key(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/oldkey*"))
	key(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/touch*"))
	touch(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/savebuttons"))
	savebuttons(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/savekeys"))
	savekeys(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/button*"))
	button(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/config_buttons.html"))
	config_buttons(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/config_keys.html"))
	config_keys(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/intent_*"))
	intent(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/run*"))
	run(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/stop"))
	stop(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/javatest"))
	javatest(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/config"))
	config(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/usersconfig"))
	config(conn, request_info, (void*)"nomenu");
  else if (urlcompare(request_info->uri, "/status"))
	status(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/dyndns*"))
	dyndnsset(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/new*"))
	emptyresponse(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/passwords.txt*"))
	emptyresponse(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/content.php*"))
	content(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/client/flash/content.php*"))
	content(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/TOUCHTEST"))
	testtouch(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/FBTEST"))
	testfb(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/reread"))
	reread(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/waitdiff"))
	waitdiff(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/sendbroadcast"))
	sendbroadcast(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/shellinabox*"))
	shellinabox(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/adjust_light_*"))
	adjust_light(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/netinfo"))
	netinfo(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/meminfo"))
	meminfo(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/upload*"))
	upload(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/uptime"))
	uptime(conn, request_info, NULL);
  else if (urlcompare(request_info->uri, "/login"))
  {
	mg_printf(conn,"HTTP/1.1 200 OK\r\nCache-Control: no-store, no-cache, must-revalidate\r\nCache-Control: post-check=0, pre-check=0\r\nPragma: no-cache\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n<html><head><meta http-equiv=\"refresh\" content=\"0;url=phone.html\"></head><body>Redirecting...</body></html>");
  }
  else if (urlcompare(request_info->uri, "/test"))
	test(conn, request_info, NULL);
#endif
  else
	processed = NULL;

  return processed;
}


int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("webkey <TOKEN>\n");
		exit(1);
	}

	FILE* ___f = fopen("/data/data/com.webkey/files/log.txt","w");
	if (___f)
		fclose(___f);

	umask(S_IWGRP | S_IWOTH);


#ifdef ANDROID
	__android_log_print(ANDROID_LOG_INFO,"Webkey C++","service's started");
#endif
	modelname[0] = 0;
	deflanguage[0] = 0;
	manufacturer[0] = 0;
#ifdef ANDROID
	__system_property_get("ro.product.model",modelname);
	__system_property_get("ro.product.manufacturer",manufacturer);
	__system_property_get("persist.sys.language",deflanguage);
#endif
	char manu[PROP_VALUE_MAX];
	manu[0] = 0;
#ifdef ANDROID
	__system_property_get("ro.product.manufacturer",manu);
#endif

	char buildversion[PROP_VALUE_MAX];
	buildversion[0] = 0;
#ifdef ANDROID
	__system_property_get("ro.build.version.release",buildversion);
#endif
	printf("buildversion = %s\n",buildversion);
	if (buildversion[0] >= '4')
	{
		is_icecreamsandwich = true;
		use_uinput_mouse = true;
		if( access( "/dev/uinput", F_OK ) == -1  &&
			access( "/dev/input/uinput", F_OK ) == -1  &&
			access( "/dev/misc/uinput", F_OK ) == -1)
			use_uinput_mouse = false;
	}
	if (is_icecreamsandwich)
	{
		printf("this is icrecreamsandwich\n");
		fflush(NULL);
	}
	if( access( "/system/usr/keylayout/geniatech.kl", F_OK ) == 0)
		geniatech = true;
	if (geniatech && access( "/system/usr/idc/Webkey.idc", F_OK ) == -1)
	{
		syst("mount -o remount,rw /system");
		FILE* f = fopen("/system/usr/idc/Webkey.idc","w");
		if (f)
		{
			fprintf(f,"touch.deviceType = touchScreen\n"
				"touch.orientationAware = 0\n"
				"keyboard.layout = geniatech\n"
				"keyboard.characterMap = geniatech\n"
				"keyboard.orientationAware = 1\n"
				"keyboard.builtIn = 1\n"
				"cursor.mode = navigation\n"
				"cursor.orientationAware = 0\n");
			fclose(f);
		}
		syst("mount -o remount,ro /system");
	}

	if (check_type(manu,"samsung"))
		samsung = true;
	int i;
	for (i=0;deflanguage[i] && i < PROP_VALUE_MAX;i++)
		if (deflanguage[i] >= 'A' && deflanguage[i] <= 'Z')
			deflanguage[i] += 'a'-'A';
	printf("deflanguage = %s\n",deflanguage);
	printf("phone model: %s\n",modelname);
	if (check_type(modelname,"GT-I5800"))
		force_240 = true;
	if (check_type(modelname,"Geeksphone ONE"))
		force_240 = true;
	if (check_type(modelname,"HTC Wildfire"))
		force_240 = true;
	if (check_type(modelname,"Dell Streak"))
	{
		is_handle_m = true;
		is_reverse_colors = true;
	}
	if (check_type(modelname,"HTC Desire HD"))
		is_handle_m = true;
	if (check_type(modelname,"C771"))
		is_reverse_colors = true;
	if (check_type(modelname,"U8800"))
                is_reverse_colors = true;
	if (check_type(modelname,"IS11CA"))
		is_reverse_colors = true;
	if (check_type(modelname,"GT-I9001"))
		is_reverse_colors = true;
	if (check_type(modelname,"GT-I5500"))
		is_reverse_colors = true;
	if (check_type(modelname,"GT-S5830"))
		is_reverse_colors = true;
	if (check_type(modelname,"GT-S5660"))
		is_reverse_colors = true;
	if (check_type(modelname,"GT-S5670"))
		is_reverse_colors = true;
	if (check_type(modelname,"SAMSUNG-SGH-I717"))
		is_reverse_colors = true;
	if (check_type(modelname,"Ideos S7 Slim"))
		is_reverse_colors = true;
	if (check_type(modelname,"ZTE BLADE"))
		flip_touch = true;
	if (check_type(modelname,"ZTE-SKATE"))
		flip_touch = true;
	if (check_type(modelname,"NookColor"))
		rotate_touch = true;
	if (check_type(modelname,"XT910"))
		force_544 = true;
	if (check_type(modelname,"BLADE3"))
		force_544 = true;
	if (check_type(modelname,"DROID RAZR"))
		force_544 = true;
	if (check_type(modelname,"DROID3"))
		force_544 = true;
	if (check_type(modelname,"DROID BIONIC"))
		force_544 = true;
	if (check_type(modelname,"Bionic"))
		force_544 = true;
	if (check_type(modelname,"MB865"))
		force_544 = true;
	if (check_type(modelname,"XT860"))
		force_544 = true;
	if (check_type(modelname,"DROID4"))
		force_544 = true;
	if (strcmp(modelname,"Nexus S")==0)
		samsung = true;
	if (strcmp(modelname,"LG-P999")==0)
		ignore_framebuffer = true;
	if (strcmp(modelname,"LG-P990")==0)
		ignore_framebuffer = true;
	if (check_type(modelname,"LT26i"))
		force_544 = true;

	pid_t pid;
	if (pipe(pipeforward))
		error("Unable to set up pipes.");
	if (pipe(pipeback))
		error("Unable to set up pipes.");

	pid = fork();
	if (pid < (pid_t)0)
	{
		error("Unable to fork().");
	}
	if (pid == 0)
	{
		close(pipeback[0]);
		close(pipeforward[1]);
		char line[LINESIZE];
		char buff[LINESIZE];
		FILE* in = fdopen(pipeforward[0],"r");
		if (!in)
			error("Unable to open pipeforward.");
		FILE* out = fdopen(pipeback[1],"w");
		if (!out)
			error("Unable to open pipeback.");
		while (fgets(line, sizeof(line)-1, in) != NULL)
		{
			if (strlen(line))
				line[strlen(line)-1] = 0;
			if (line[0] == 'S')
			{
				if (fork()==0)
				{
					system(line+1);
					return EXIT_FAILURE;
				}
				fflush(out);
				continue;
			}
//			printf("%s\n",line);
//			fflush(NULL);
/*			if (line[0] == 'B')
			{
				FILE* p = mypopen(line+1,"r");
				if (p)
				{
					int a;
					while ((a=fread(buff, 1, sizeof(buff)-1, p)) == sizeof(buff)-1)
					{
						fwrite(buff,1,a,out);
					}
					if (a)
						fwrite(buff,1,a,out);
					mypclose(p);
					fflush(NULL);
				}
				else
				{
					printf("error calling popen\n");
					fflush(NULL);
				}
			}
			else
*/ //			{
				FILE* p = mypopen(line,"r");
				if (p)
				{
					while (fgets(buff, sizeof(buff)-1, p) != NULL)
					{
						fprintf(out,"%s",buff);
					}
					mypclose(p);
				}
				else
					printf("error calling popen\n");
//			}
			fprintf(out,"!!!END_OF_POPEN!!!\n");
			fflush(out);
		}
		fclose(in);
		fclose(out);
		close(pipeback[1]);
		close(pipeforward[0]);
		printf("popen fork closed\n");
		return EXIT_FAILURE;
	}
	close(pipeback[1]);
	close(pipeforward[0]);
	pipeout = fdopen(pipeforward[1],"w");
	if (!pipeout)
		error("Unable to open pipeout.");
	pipein = fdopen(pipeback[0],"r");
	if (!pipein)
		error("Unable to open pipein.");


//	int terminal_pid;
//	create_subprocess(&terminal_pid);

	for (i = strlen(argv[0])-1; i>=0; i--)
		if (argv[0][i] == '/')
		{
			argv[0][i+1]=0;
			break;
		}
	dir = argv[0];
	stat(argv[0], &info);
	chmod("/data/data/com.webkey/files/log.txt", S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	chown("/data/data/com.webkey/files/log.txt", info.st_uid, info.st_gid);
	chmod("/data/data/com.webkey/files/log.txt", S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	printf("dir: %s\n",dir.c_str());
	logfile = dir+"log.txt";
	pthread_mutex_init(&logmutex, NULL);
	pthread_mutex_init(&initfbmutex, NULL);
	access_log(NULL,"service's started");
	dirdepth = -1;
	for (i = 0; i < strlen(argv[0]); i++)
		if (argv[0][i] == '/')
			dirdepth++;
	//printf("%d\n",dirdepth);


	port = 81;
	sslport = 443;
//	read_prefs();
	token = argv[1];
	printf("Token is [%s]\n", token);
	if (port <= 0 || sslport <= 0)
		error("Invalid port\n");
	fbfd = -1;
        if (is_icecreamsandwich == false && ignore_framebuffer == false && (fbfd = open(FB_DEVICE, O_RDONLY)) == -1)
        {
                //error("open framebuffer\n");
#ifdef ANDROID
		__android_log_print(ANDROID_LOG_ERROR,"Webkey C++","error opening framebuffer, let's try other tricks");
#endif
		//printf("error opening framebuffer, using adb\n");
		//syst("setprop service.adb.root 1");
		//syst("stop adbd");
		//syst("start adbd");
        }
	if (fbfd >= 0)
	{
		if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0)
		{
//			error("error reading screeninfo\n");
			printf("error reading screeninfo, fall back\n");
			fbfd = -1;
		}
		if (scrinfo.xres_virtual == 240) //for x10 mini pro
		       scrinfo.xres_virtual = 256;
		if (scrinfo.xres == 256)
		{
			int nall = 0;
			int n;
			int fd = open(FB_DEVICE, O_RDONLY);
			char tmp[1024];
			if (fd>=0)
			{
				while (n = read(fd, tmp, 1024))
				{
					nall += n;
				}
				printf("%d\n",nall);
				close(fd);
				bytespp = scrinfo.bits_per_pixel / 8;
				if (bytespp == 3)
					bytespp = 4;
				if (scrinfo.xres*scrinfo.yres*bytespp*2 < nall)
					force_240 = true;
			}
		}
		if (force_240)
			scrinfo.xres_virtual = scrinfo.xres = 240;
	}
	if (fbfd == -1)
	{
		FILE* sc;
		int i;
		for(i = 0; screencapbinaries[i].name!=NULL; i++)
		{
			printf("s %d\n",i);
			fflush(NULL);
//				pthread_mutex_lock(&popenmutex);
//				fprintf(pipeout,"B%s\n",screencapbinaries[i].name);
				fflush(NULL);
				FILE* sc = mypopen(screencapbinaries[i].name,"r");
				//fprintf(pipeout,"Bls\n");
//				printf("going on\n");
//				fflush(NULL);
				//char buff[12];
			int a;
			uint32_t size[3];
			int x,y,format;
			if (i >= 5) // this is for fbread
			{
				char temp[1024];
				if (feof(sc) || fread(temp,1,128,sc) != 128)
				{
					printf("fread failed\n");
					fflush(NULL);
					mypclose(sc);
					continue;
				}
				int i = 0;
				while(i < 110 && strncmp(temp+i,"size",4)!=0) i++;
				i+=4;
				while (i < 110 && temp[i]== ' ') i++;
				x = getnum(temp+i);
				while (i < 110 && temp[i]!= 'x' && temp[i] != 'X') i++;
				i++;
				y = getnum(temp+i);
				while(i < 110 && strncmp(temp+i,"format",6)!=0) i++;
				i+=6;
				while (i < 110 && temp[i]== ' ') i++;
				format = getnum(temp+i);
				int s = 128;
				while ((i = fread(temp,1,1024,sc)) == 1024)
					s += i;
				s+=i;
				screencap_skipbytes = max(0,s-x*y*4);
				printf("xres = %d, yres = %d, format = %d, skip first %d bytes\n",x,y,format,screencap_skipbytes);
				fflush(NULL);
			}
			else // this is for screencap
			{
				if (feof(sc) || (a = fread(size,4,3,sc)) != 3)
				{
					printf("fread failed a = %d\n",a);
					fflush(NULL);
					mypclose(sc);
					continue;
				}
				x = size[0];
				y = size[1];
				format = size[2];
				screencap_skipbytes = 12;
			}
			scrinfo.xres_virtual = scrinfo.xres = x;
			scrinfo.yres_virtual = scrinfo.yres = y;
			if (format == 4)
			{
				scrinfo.bits_per_pixel = 16;
				scrinfo.red.offset = 11;
				scrinfo.red.length = 5;
				scrinfo.green.offset = 5;
				scrinfo.green.length = 6;
				scrinfo.blue.offset = 0;
				scrinfo.blue.length = 5;
			}
			else
			{
				scrinfo.bits_per_pixel = 32;
				scrinfo.red.offset = 0;
				scrinfo.red.length = 8;
				scrinfo.green.offset = 8;
				scrinfo.green.length = 8;
				scrinfo.blue.offset = 16;
				scrinfo.blue.length = 8;
			}
			bytespp = scrinfo.bits_per_pixel / 8;
			uint32_t temp[1000];
			while (fread(temp,4,1000,sc));
			mypclose(sc);
			break;
/*				while (i = fread(buff, 1, 12, p))
				{
					printf("%d\n",i);
					fflush(NULL);
					break;
//					if (strcmp(buff,"!!!END_OF_POPEN!!!\n")==0)
//						break;
				}
			//        fflush(stdout);
					printf("a2\n");
					fflush(NULL);
				mypclose(p);
				fflush(NULL);
				if (i)
					break;
//				pthread_mutex_unlock(&popenmutex);
//				break;
*/
			/*			fflush(NULL);
			printf("%s\n",screencapbinaries[i].name);
			if ((sc = popen(screencapbinaries[i].name,"r"))==NULL)
			{
				printf("skipped %s\n",screencapbinaries[i].name);
				fflush(NULL);
				continue;
			}
			printf("main i = %d\n",i);
			fflush(NULL);
			uint32_t size[12];
			int a;
			if (feof(sc) || (a = fread(size,1,3,sc)) != 3)
			{
				printf("fread failed a = %d\n",a);
				fflush(NULL);
				mypclose(sc);
				continue;
			}
			printf("main2\n");
			fflush(NULL);
			scrinfo.xres_virtual = scrinfo.xres = size[0];
			scrinfo.yres_virtual = scrinfo.yres = size[0];
			scrinfo.bits_per_pixel = 16;
			bytespp = scrinfo.bits_per_pixel / 8;
			scrinfo.red.offset = 11;
			scrinfo.red.length = 5;
			scrinfo.green.offset = 5;
			scrinfo.green.length = 6;
			scrinfo.blue.offset = 0;
			scrinfo.blue.length = 5;
			mypclose(sc);
			break;
*/		}
		if (screencapbinaries[i].name == NULL)
			screencap = -1;
		else
			screencap = i;
	}

	max_brightness = 255;
	int fd = open("/sys/class/leds/lcd-backlight/max_brightness", O_RDONLY);
	if (fd < 0)
		fd = open("/sys/class/backlight/pwm-backlight/max_brightness", O_RDONLY);
	char value[20];
	int n;
	if (fd >= 0)
	{
		n = read(fd, value, 10);
		if (n)
			max_brightness = getnum(value);
		close(fd);
	}
	char buffer[8192];
        (void) signal(SIGCHLD, signal_handler);
        (void) signal(SIGTERM, signal_handler);
        (void) signal(SIGINT, signal_handler);
	itoa(buffer,port);
//        if (mg_set_option(ctx, "ports", buffer) != 1)
//                error("Error in configurate port.\n");
	passfile = "passwords.txt";
	FILE* pf = fopen((dir+passfile).c_str(),"r");
	if (pf)
		fclose(pf);
	else
	{
		pf = fopen((dir+passfile).c_str(),"w");
		if (pf)
			fclose(pf);
	}
	chmod((dir+passfile).c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	chown((dir+passfile).c_str(), info.st_uid, info.st_gid);
	chmod((dir+passfile).c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	char prot[512];
	sprintf(prot,(std::string("/favicon.ico=,/flags_=,/javatest=,/stop=,/dyndns=,/reread=,/test=,/sendbroadcast=,/index.html=,/register=,/reganim.gif=,/js/jquery=,/=")+dir+passfile).c_str());
//        mg_set_option(ctx, "auth_realm", "Webkey");
#ifdef __linux__
//        mg_set_option(ctx, "error_log", "log.txt");
#endif
#ifndef __linux__
//	mg_set_option(ctx, "protect", (std::string("/password=,/gpsset=,/stop=,/dyndns=,/reread=,/test=,/sendbroadcast=,/=")+dir+passfile).c_str());
//	mg_set_option(ctx, "root",dir.c_str());
#else
//	mg_set_option(ctx, "root",dir.c_str());
#endif
	char strport[16];
	char strport_ssl[16];
	char docroot[256];
	strcpy(docroot,dir.c_str());
	itoa(strport,port);
	FILE* sf = fopen((dir+"ssl_cert.pem").c_str(),"r");
	if (sf)
	{
		fclose(sf);
		has_ssl = true;
	}
	else
	{
		//temporary
		//syst("chmod \"a+x\" /data/data/com.webkey/files/openssl");
		syst((dir+"openssl req -x509 -nodes -days 5650 -newkey rsa:1024 -keyout "+dir+"ssl_cert.pem -out "+dir+"ssl_cert.pem -config "+dir+"ssleay.cnf").c_str());
		sf = fopen((dir+"ssl_cert.pem").c_str(),"r");
		if (sf)
		{
			fclose(sf);
			has_ssl = true;
		}
	}
	if (has_ssl)
		unlink((dir+"openssl").c_str());
	static const char *options[] = {
		"document_root", docroot,
		"listening_ports", strport,
		"num_threads", "10",
#ifdef ANDROID
		"protect_uri", prot,
		"authentication_domain", "Webkey",
#endif
		NULL
	};
	strcpy(strport_ssl,strport);
	char t[10];
	itoa(t,sslport);
	strcat(strport_ssl,",");
	strcat(strport_ssl,t);
	strcat(strport_ssl,"s");
	char sslc[100];
	strcpy(sslc,(dir+"ssl_cert.pem").c_str());
	static const char *options_ssl[] = {
		"document_root", docroot,
		"listening_ports", strport_ssl,
		"ssl_certificate", sslc,
		"num_threads", "10",
#ifdef ANDROID
		"protect_uri", prot,
		"authentication_domain", "Webkey",
#endif
		NULL
	};

	// check root
	if (getuid() != 0)
		error("Not running as root.\n");

	if (has_ssl)
	{
		printf("SSL is ON\n");
		if ((ctx = mg_start(token,&event_handler,dir.c_str(),options_ssl)) == NULL) {
			error("Cannot initialize Mongoose context");
		}
	}
	else
	{
		printf("SSL is OFF, error generating the key.\n");
		if ((ctx = mg_start(token,&event_handler,dir.c_str(),options)) == NULL) {
			error("Cannot initialize Mongoose context");
		}
	}

	if (ctx == NULL)
		return -1;


	FILE* auth;
	if ((auth = fopen((dir+"authkey.txt").c_str(),"w")))
	{
		fprintf(auth,"%s",token);
		fclose(auth);
	}
	chmod((dir+"authkey.txt").c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	chown((dir+"authkey.txt").c_str(), info.st_uid, info.st_gid);
	chmod((dir+"authkey.txt").c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);

	FILE* pidFile;
	if ((pidFile = fopen((dir+"pid.txt").c_str(),"w")))
	{
		fprintf(pidFile,"%d",getpid());
		fclose(pidFile);
	}
	chmod((dir+"pid.txt").c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	chown((dir+"pid.txt").c_str(), info.st_uid, info.st_gid);
	chmod((dir+"pid.txt").c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);

	pthread_mutex_init(&pngmutex, NULL);
	pthread_mutex_init(&fbmutex, NULL);
	pthread_mutex_init(&diffmutex, NULL);
	pthread_mutex_init(&popenmutex, NULL);
	pthread_mutex_init(&uinputmutex, NULL);
	pthread_mutex_init(&wakelockmutex, NULL);
	pthread_cond_init(&diffcond,0);
	pthread_cond_init(&diffstartcond,0);
//	pthread_mutex_init(&smsmutex,0);
//	pthread_mutex_init(&contactsmutex,0);
//	pthread_cond_init(&smscond,0);
//	pthread_cond_init(&contactscond,0);
//	admin_password = "";
	wakelock = true;	//just to be sure
	unlock_wakelock(true);
//	for (i = 0; i < 8; i++)
//	{
//		char c[2]; c[1] = 0; c[0] = (char)(rand()%26+97);
//		admin_password += c;
//	}
//	mg_modify_passwords_file(ctx, (dir+passfile).c_str(), "admin", admin_password.c_str(),-1);
//	mg_modify_passwords_file(ctx, (dir+passfile).c_str(), "admin", admin_password.c_str(),-2);
	//load_mimetypes();




//        printf("Webkey %s started on port(s) [%s], serving directory [%s]\n",
//            mg_version(),
//            mg_get_option(ctx, "ports"),
//            mg_get_option(ctx, "root"));


	printf("starting touch...\n");
	init_touch();
//	printf("starting uinput...\n");
//	init_uinput();
//	load_keys();
        fflush(stdout);

	pthread_t backthread;
	backserver_parameter par;
	par.server_username = &server_username;
	par.server_random = &server_random;
	par.server = &server;
	par.server_changes = &server_changes;
	par.server_port = &(strport[0]);
	par.ctx = ctx;
	pthread_create(&backthread,NULL,backserver,(void*)&par);
	pthread_t watchthread;
	pthread_create(&watchthread,NULL,watchscreen,(void*)NULL);

	i = 0;
	int d = 0;
	int u = 0;
	__u32 tried = 0;
	__u32 lastip = 0;
	up = 0;
	//TEMP
	//TEMP
//		init_uinput();
	while (exit_flag == 0)
	{
		i++;
		d++;
		up++;
                sleep(1);
	//usleep(100000);
	//
	//
//		struct timeval tv;
//		tv.tv_sec = 0;
//		tv.tv_usec = 100000;
//		n = select(NULL,NULL, NULL, NULL, &tv);
		if (is_icecreamsandwich && up > shutdownkey_up && uinput_fd != -1)
		{
			pthread_mutex_lock(&uinputmutex);
			suinput_close(uinput_fd);
			uinput_fd = -1;
			pthread_mutex_unlock(&uinputmutex);
		}
		if (i==10)
		{
			unlock_wakelock(false);
//			set_blink(1,500,200);
//			vibrator_on(1500);

			i = 0;
		}
		struct timeval tv;
		//__android_log_print(ANDROID_LOG_INFO,"Webkey C++","debug: d = %d, dyndns = %d, host = %s, dyndns_base64 = %s",d,dyndns,dyndns_host.c_str(),dyndns_base64.c_str());
		if (d==60)
		{
			d=0;
			gettimeofday(&tv,0);
			int q;
			for (q = 0; q < sessions.size(); q++)
			{
				if (sessions[q]->alive)
				{
					if (tv.tv_sec > sessions[q]->lastused + 3600)
					{
						sessions[q]->alive = false;
						kill(sessions[q]->pid,SIGKILL);
						close(sessions[q]->ptm);
						printf("CLOSED\n");
					}
				}
			}
			backdecrease(); //decrease the number of connection to the server
			__u32 ip = ipaddress();
//			printf("IP ADDRESS: %d\n",ip);
//			printf("LAST IP ADDRESS: %d\n",lastip);
//			printf("SERVER_CHANGES before check: %d\n",server_changes);
			if (ip!=lastip)
				server_changes++;
//			printf("SERVER_CHANGES before after: %d\n",server_changes);
			lastip = ip;
			if(dyndns)
			{
				//__android_log_print(ANDROID_LOG_INFO,"Webkey C++","debug: dyndns is true");
				//in_addr r;
				//r.s_addr = ip;
				//__android_log_print(ANDROID_LOG_INFO,"Webkey C++","debug: ip address = %s",inet_ntoa(r));
				//r.s_addr = dyndns_last_updated_ip;
				//__android_log_print(ANDROID_LOG_INFO,"Webkey C++","debug: dyndns_last_updated_ip = %s",inet_ntoa(r));
				if (ip && ip != dyndns_last_updated_ip)
				{
					//__android_log_print(ANDROID_LOG_INFO,"Webkey C++","debug: updateing, u = %d",u);
					//r.s_addr = tried;
					//__android_log_print(ANDROID_LOG_INFO,"Webkey C++","debug: tried = %s",inet_ntoa(r));
					u++;
					if (tried != ip || u==10) //tries to update every 10 mins
					{
						tried = ip;
						u = 0;
						std::string ret = update_dyndns(ip);
//						printf("%s\n",ret.c_str());
#ifdef ANDROID
						__android_log_print(ANDROID_LOG_INFO,"Webkey C++","debug: ret = %s",ret.c_str());
#endif
					}
				}
			}
		}
	}
	pthread_mutex_lock(&diffmutex);
	pthread_cond_broadcast(&diffstartcond);
	pthread_mutex_unlock(&diffmutex);
        (void) printf("Exiting on signal %d, "
            "waiting for all threads to finish...", exit_flag);
        fflush(stdout);
        mg_stop(ctx);
        (void) printf("%s", " done.\n");
	unlock_wakelock(true);

	clear();
	pthread_mutex_destroy(&pngmutex);
	pthread_mutex_destroy(&fbmutex);
	fclose(pipeout);
	fclose(pipein);
	close(pipeback[0]);
	close(pipeforward[1]);
        return (EXIT_SUCCESS);
}
