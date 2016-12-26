/*
 * AMBER. Copyright (c) 2004 Peter da Silva. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the program and author may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <stdarg.h>

/* Special time values */
#define DEFTIME 300	/* 5 mins */
#define NOTIME -1

/* Status - even values accept, odd values defer */
#define UNKNOWN 2
#define DEFER 1
#define ACCEPT 0

/* operation mode for error messages */
#define PRINTING 0
#define LOGGING 1

/* Database */
#define RECSIZE 32	/* 32 byte records leave room for growth */

typedef struct {
	time_t first_seen;
	time_t last_seen;
} ip_info;

static u_long read_addr = INADDR_ANY;

int conn_delay = DEFTIME;
int deferral = DEFTIME;
int long_deferral = NOTIME;
int idle = NOTIME;
int long_idle = NOTIME;
int error_mode = PRINTING;

char *red_file = NULL;
char *green_file = NULL;

typedef struct _vl {
	struct _vl *next;
	char *name;
	char *val;
} env_t;

env_t *pass_env_list = NULL;
char default_var[] = "AMBERCHECK=NO";
char *smtp_code = "430 Message Deferred";

char *usage_string =
  "[-lnNeE] [-d dir] [-c secs] [-t secs] [-T secs] [-i secs] [-I secs] [-r file] [-g file] [-s string] [-p NAME[=VAL]] [command [args...]]";

char *version_string =
  "AMBER version " VER " Copyright (c) 2004-2006 Peter da Silva.";

char *prog;
char *remote_ip = NULL;
char *remote_host = NULL;

void syntax_exit(char *msg, char flag);
void insert_text(int id, char *text);
void log_perror(char *object);
void log_warning(char *message);
void log_pass(char *message);
void log_banner(char *banner);
char *con_name(void);
void normal_exit(char **av, int mode);
int read_time(u_long addr, ip_info *recp);
int write_time(u_long addr, ip_info *recp);
char *file_name(u_long addr);
int make_dir(u_long addr);
long parse_time(char *s);
void add_pass_env(char *name);
int input_check(int fd);

void usage(void)
{
	fprintf(stderr, "Usage: %s %s\n", prog, usage_string);
}

void version(void)
{
	puts(version_string);
}

int main(int ac, char **av)
{
	char *workdir = NULL;
	u_long his_addr;
	time_t now;
	ip_info rec;
	int log_errors = 0;
	int throttle_noip = 0;
	int throttle_dynamic = 0;
	int eager_throttle = 0;
	int eager_blocking = 0;
	env_t *var;

	if(prog = strrchr(*av, '/'))
		prog++;
	else
		prog = *av;

	add_pass_env(default_var);

	while(*++av) {
		char opt;

		if(**av != '-')
			break;

		while(opt = *++*av) {
			char *arg;

			/* options without args */
			switch(opt) {
				case 'V': version(); exit(0);
				case 'l': log_errors = 1; opt = 0; break;
				case 'n': throttle_noip = 1; opt = 0; break;
				case 'N': throttle_dynamic = 1; opt = 0; break;
				case 'e': eager_throttle = 1; opt = 0; break;
				case 'E': eager_blocking = 1; opt = 0; break;
			}
			if(!opt)
				continue;

			arg = ++*av;
			if(!*arg) {
				arg = *++av;
				if(!arg)
					syntax_exit("No value for option", opt);
			}

			/* options with args */
			switch(opt) {
				case 'c': conn_delay = parse_time(arg); break;
				case 't': deferral = parse_time(arg); break;
				case 'T': long_deferral = parse_time(arg); break;
				case 'i': idle = parse_time(arg); break;
				case 'I': long_idle = parse_time(arg); break;
				case 'd': workdir = arg; break;
				case 'r': /* red list */
				case 'b': red_file = arg; break;
				case 'g': green_file = arg; break;
				case 'p': add_pass_env(arg); break;
				case 's': smtp_code = arg; break;
				default: syntax_exit("Unknown option", opt);
			}
			break;
		}
	}

	if(!*av)
		av = NULL;

	/* Finished parsing arguments, time to switch logging to syslog */
	if(log_errors) {
		openlog(prog, LOG_PID, LOG_MAIL);
		error_mode = LOGGING;
	}

	/* Check for bypasses from lower levels */
	for(var = pass_env_list; var; var=var->next) {
		char *val = getenv(var->name);
		if(val && (var->val==0 || strcmp(val, var->val) == 0)) {
			log_pass("BYPASS");
			normal_exit(av, ACCEPT);
		}
	}

	/* If connection delay set, take a nap */
	if(conn_delay != NOTIME) {
		sleep(conn_delay);
		if(input_check(0)) {
			log_warning("DATA before HELO");
			if(eager_blocking) normal_exit(av, DEFER);
			if(long_deferral == NOTIME)
				long_deferral = deferral * 6;
			deferral = long_deferral;
			if(long_idle != NOTIME)
				idle = long_idle;
		} else
			eager_throttle = 0;
	} else
		eager_throttle = 0;

	/* If something wrong with tcpserver or run from inetd, fail open */
	remote_ip = getenv("TCPREMOTEIP");
	if(!remote_ip) {
		log_warning("No TCPREMOTEIP");
		normal_exit(av, UNKNOWN);
	}

	/* If there's a green list, use it */
	if(green_file) {
		if(check_file(remote_ip, green_file)) {
			log_pass("GREENLIST OK");
			normal_exit(av, ACCEPT);
		}
	}

	/* If the host lookup failed, use a longer delay */
	remote_host = getenv("TCPREMOTEHOST");
	if(!remote_host || remote_host[0] == '[') {
		log_warning("Unresolved remote host");
		if(long_deferral == NOTIME)
			long_deferral = deferral * 6;
		deferral = long_deferral;
		if(long_idle != NOTIME)
			idle = long_idle;
	} else if(throttle_dynamic &&
		  (strcasestr(remote_host, "dsl") ||
		   strcasestr(remote_host, "cable") ||
		   strcasestr(remote_host, "dyn") ||
		   strcasestr(remote_host, "ppp") ||
		   strcasestr(remote_host, "dial"))) {
		log_warning("Possible dynamic domain");
		if(long_deferral == NOTIME)
			long_deferral = deferral * 6;
		deferral = long_deferral;
		if(long_idle != NOTIME)
			idle = long_idle;
	} else {
		throttle_dynamic = throttle_noip = 0;
	}

	his_addr = inet_addr(remote_ip);

	/* this shouldn't be possible, but it's a simple check */
	if(his_addr == INADDR_NONE || his_addr == INADDR_ANY) {
		log_warning("Impossible TCPREMOTEIP");
		normal_exit(av, UNKNOWN);
	}

	if(workdir && chdir(workdir) == -1) {
		log_perror(workdir);
		normal_exit(av, UNKNOWN);
	}

	now = time(NULL);

	if(!read_time(his_addr, &rec))
		rec.first_seen = now;

	if(idle != NOTIME && rec.last_seen + idle < now) {
		if(rec.last_seen) {
			log_warning("Resetting idle address");
		} else {
			log_warning("New address");
		}
		rec.first_seen = now;
	}

	rec.last_seen = now;

	if(!write_time(his_addr, &rec))
		normal_exit(av, UNKNOWN);

	if(rec.first_seen + deferral > now)
		normal_exit(av, DEFER);
	else {
		if(red_file) {
			if(check_file(remote_ip, red_file)) {
				log_warning("In REDLIST file.");
				normal_exit(av, DEFER);
			}
		}
		if(throttle_noip || throttle_dynamic || eager_throttle) {
			rec.first_seen = now;
			write_time(his_addr, &rec);
			log_pass("THROTTLE OK");
		} else
			log_pass("NORMAL OK");
		normal_exit(av, ACCEPT);
	}
}

void normal_exit(char **av, int mode)
{
	int accept = (mode&0x1) == 0;

	if(av) {
		char *newprog = *av;

		if(!accept) {
			char banner[83];

			log_banner(smtp_code);

			sprintf(banner, "%.80s\r\n", smtp_code);

			write(1, banner, strlen(banner));
			sleep(1);
			exit(mode);
		}

		*av = strrchr(newprog, '/');
		if(*av)
			++*av;
		else
			*av = newprog;

		execvp(newprog, av);
		log_perror(newprog);
		exit(-1);
	} else {
		if(!accept)
			exit(mode);

		exit(0);
	}
}

void syntax_exit(char *msg, char flag)
{
	if(flag)
		fprintf(stderr, "%s -%c\n", msg, flag);
	else
		fprintf(stderr, "%s\n", msg);
	usage();
	exit(2);
}

void log_perror(char *object)
{
	if(error_mode == PRINTING)
		perror(object);
	else
		syslog(LOG_ERR, "%s: %s", object, strerror(errno));
}

void log_warning(char *message)
{
	if(error_mode == PRINTING)
		fprintf(stderr, "%s: %s: %s\n", prog, con_name(), message);
	else
		syslog(LOG_WARNING, "%s: %s", con_name(), message);
}

void log_pass(char *message)
{
	if(error_mode == PRINTING)
		fprintf(stderr, "%s: %s: %s\n", prog, con_name(), message);
	else
		syslog(LOG_INFO, "%s: %s", con_name(), message);
}

void log_banner(char *banner)
{
	char buffer[512], *s;

	snprintf(buffer, 512, "%s: %s %s", prog, con_name(), banner);
	if(s = strchr(buffer, '\r'))
		*s = 0;

	if(error_mode == PRINTING)
		fprintf(stderr, "%s\n", buffer);
	else
		syslog(LOG_NOTICE, "%s", buffer);
}

char *con_name()
{
	static char name[80*2+3] = "";

	if(!name[0]) {
		char *addr = remote_ip ? remote_ip : getenv("TCPREMOTEIP");
		char *host = remote_host ? remote_host : getenv("TCPREMOTEHOST");

		if(addr) {
			if(host) {
				sprintf(name, "%.80s[%.80s]", host, addr);
			} else {
				sprintf(name, "[%.80s]", addr);
			}
		} else {
			if(host) {
				sprintf(name, "%.80s", host);
			} else {
				sprintf(name, "unknown");
			}
		}
	}
	return name;
}

int read_time(u_long addr, ip_info *recp)
{
	char *name = file_name(addr);
	int status = 0;
	int fd = -1;

	fd = open(name, O_RDONLY);

	if(fd != -1) {
		off_t offset, seeked;

		offset = (ntohl(addr)&0xFF) * RECSIZE;
		seeked = lseek(fd, offset, SEEK_SET);

		if(offset == seeked)
			if(read(fd, recp, sizeof *recp) == sizeof *recp)
				status = 1;
		close(fd);

	}

	return status;
}

int write_time(u_long addr, ip_info *recp)
{
	char *name = file_name(addr);
	int fd = -1;
	off_t new, byte;

	if(!make_dir(addr))
		return 0;

	fd = open(name, O_WRONLY|O_CREAT, 0666);
	if(fd == -1)
		goto error;

	byte = (ntohl(addr)&0xFF) * RECSIZE;
	new = lseek(fd, byte, SEEK_SET);
	if(new != byte)
		goto error;

	if(write(fd, recp, sizeof *recp) != sizeof *recp)
		goto error;

	close(fd);
	return 1;

error:
	log_perror(name);
	if(fd != -1)
		close(fd);
	return 0;
}

char *file_name(u_long addr)
{
	union {
		unsigned char byte[4];
		u_long word;
	} u;
	static char buffer[256];

	u.word = addr;

	sprintf(buffer, "%02x/%02x/%02x.t", u.byte[0], u.byte[1], u.byte[2]);

	return buffer;
}

int make_dir(u_long addr)
{
	union {
		unsigned char byte[4];
		u_long word;
	} u;
	static char buffer[256];
	char *ptr;

	u.word = addr;
	ptr = buffer;

	sprintf(buffer, "%02x", u.byte[0]);
	if(mkdir(buffer, 0777) == -1 && errno != EEXIST) {
		log_perror(buffer);
		return 0;
	}

	sprintf(buffer, "%02x/%02x", u.byte[0], u.byte[1]);
	if(mkdir(buffer, 0777) == -1 && errno != EEXIST) {
		log_perror(buffer);
		return 0;
	}

	return 1;
}

long parse_time(char *s)
{
	long t = 0, d = 0, h = 0, m = 0;

	while(*s) {
		if(isdigit(*s)) {
			t = t * 10 + *s - '0';
		} else if(*s == ':') {
			h = m;
			m = t;
			t = 0;
		} else if(*s == 'h') {
			h = t;
			t = 0;
		} else if(*s == 'm') {
			m = t;
			t = 0;
		} else if(*s == 'd') {
			d = t;
			t = 0;
		} else
			break;
		s++;
	}
	return ((d * 24 + h) * 60 + m) * 60 + t;
}

void add_pass_env(char *name)
{
	env_t *new = malloc(sizeof *new);
	char *val;

	if(!new) {
		log_perror(name);
		exit(2);
	}

	if(val = strchr(name, '='))
		*val++ = 0;

	new->next = pass_env_list;
	new->name = name;
	new->val = val;
	pass_env_list = new;
}

int check_file(char *name, char *file)
{
	FILE *fp;
	char line[256];
	int namelen = strlen(name);

	if(!file || !(fp = fopen(file, "r"))) {
		return 0;
	}

	while(fgets(line, sizeof line - 1, fp)) {
		if(strncmp(name, line, namelen) == 0) {
			char *s = &line[namelen];
			if(*s=='\0' || *s=='\n')
				return 1;
			if(*s==' ' || *s=='\t') {
				while(*s == ' ' || *s == '\t')
					s++;
				if(*s == '4' || *s == '5')
					smtp_code = s;
				return 1;
			}
		}
	}
	fclose(fp);
	return 0;
}

int input_check(fd)
{
	fd_set stdin_set;
	struct timeval null_timer;

	memset(&null_timer, 0, sizeof null_timer);
	FD_ZERO(&stdin_set);
	if(select(1, &stdin_set, NULL, NULL, &null_timer) > 0)
		return 1;
	return 0;
}

