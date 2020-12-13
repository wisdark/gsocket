
#include "common.h"
#include "utils.h"
#include "console.h"

struct _gopt gopt;

extern char **environ;

/*
 * Add list of argv's from GSOCKET_ARGS to argv[]
 * result: argv[0] + GSOCKET_ARGS + argv[1..n]
 */
static void
add_env_argv(int *argcptr, char **argvptr[])
{
	char *str_orig = getenv("GSOCKET_ARGS");
	char *str = NULL;
	char *next;
	char **newargv = NULL;
	int newargc;

	if (str_orig == NULL)
		return;

	str = strdup(str_orig);
	next = str;

	newargv = malloc(1 * sizeof *argvptr);
	memcpy(&newargv[0], argvptr[0], 1 * sizeof *argvptr);
	newargc = 1; 

	while (next != NULL)
	{
		while (*str == ' ')
			str++;

		next = strchr(str, ' ');
		if (next != NULL)
		{
			*next = 0;
			next++;
		}
		// catch if last character is ' '
		if (strlen(str) > 0)
		{
			/* *next == '\0'; str points to argument (0-terminated) */
			newargc++;
			DEBUGF("%d. arg = '%s'\n", newargc, str);
			newargv = realloc(newargv, newargc * sizeof newargv);
			newargv[newargc - 1] = str;
		}

		str = next;
		if (str == NULL)
			break;
	}

	// Copy original argv[1..n]
	newargv = realloc(newargv, (newargc + *argcptr) * sizeof newargv);
	memcpy(newargv + newargc, *argvptr + 1, (*argcptr - 1) * sizeof *argvptr);

	newargc += (*argcptr - 1);
	newargv[newargc] = NULL;

	*argcptr = newargc;
	*argvptr = newargv;
	DEBUGF("Total argv[] == %d\n", newargc);
	int i;
	for (i = 0; i < newargc + 1; i++)
		DEBUGF("argv[%d] = %s\n", i, newargv[i]);
}

void
init_defaults(int *argcptr, char **argvptr[])
{
	gopt.log_fp = stderr;
	gopt.err_fp = stderr;
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);	// no defunct childs please

	/* MacOS process limit is 256 which makes Socks-Proxie yield...*/
	struct rlimit rlim;
	memset(&rlim, 0, sizeof rlim);
	int ret;
	ret = getrlimit(RLIMIT_NOFILE, &rlim);
	if (ret == 0)
	{
		rlim.rlim_cur = MIN(rlim.rlim_max, FD_SETSIZE);
		ret = setrlimit(RLIMIT_NOFILE, &rlim);
		getrlimit(RLIMIT_NOFILE, &rlim);
		// DEBUGF_C("Max File Des: %llu (max = %llu)\n", rlim.rlim_cur, rlim.rlim_max);
	}

	add_env_argv(argcptr, argvptr);
}

GS *
gs_create(void)
{
	GS *gs = GS_new(&gopt.gs_ctx, &gopt.gs_addr);
	XASSERT(gs != NULL, "%s\n", GS_CTX_strerror(&gopt.gs_ctx));
	
	if (gopt.token_str != NULL)
		GS_set_token(gs, gopt.token_str, strlen(gopt.token_str));

	return gs;
}

static void
cb_sigterm(int sig)
{
	exit(255);	// will call cb_atexit()
}

void
get_winsize(void)
{
	int ret;

	memcpy(&gopt.winsize_prev, &gopt.winsize, sizeof gopt.winsize_prev);
	
	ret = ioctl(STDOUT_FILENO, TIOCGWINSZ, &gopt.winsize);
	if ((ret == 0) && (gopt.winsize.ws_col != 0))
	{
		/* SUCCESS */
		DEBUGF_M("Columns: %d, Rows: %d\n", gopt.winsize.ws_col, gopt.winsize.ws_row);
	} else {
		gopt.winsize.ws_col = 80;
		gopt.winsize.ws_row = 24;
	}
}

void
init_vars(void)
{
	GS_library_init(gopt.err_fp, /* Debug Output */ gopt.err_fp);
	GS_LIST_init(&gopt.ids_peers, 0);
	GS_CTX_init(&gopt.gs_ctx, &gopt.rfd, &gopt.wfd, &gopt.r, &gopt.w, &gopt.tv_now);

	if (gopt.is_use_tor == 1)
		GS_CTX_setsockopt(&gopt.gs_ctx, GS_OPT_USE_SOCKS, NULL, 0);
	/* If Server is not available yet then wait for Server. */
	if (gopt.is_sock_wait != 0)
		GS_CTX_setsockopt(&gopt.gs_ctx, GS_OPT_SOCKWAIT, NULL, 0);

	/* We can turn client _OR_ server */
	if (gopt.is_client_or_server != 0)
		GS_CTX_setsockopt(&gopt.gs_ctx, GS_OPT_CLIENT_OR_SERVER, NULL, 0);

	/* Disable encryption (-C) */
	if (gopt.is_no_encryption == 1)
		GS_CTX_setsockopt(&gopt.gs_ctx, GS_OPT_NO_ENCRYPTION, NULL, 0);

	/* This example uses blocking sockets. Set blocking. */
	if (gopt.is_blocking == 1)
		GS_CTX_setsockopt(&gopt.gs_ctx, GS_OPT_BLOCK, NULL, 0);

	if (gopt.is_multi_peer == 0)
		GS_CTX_setsockopt(&gopt.gs_ctx, GS_OPT_SINGLESHOT, NULL, 0);

	char *str = getenv("GSOCKET_ARGS");
	if ((str != NULL) && (strlen(str) > 0))
		VLOG("=Extra arguments: '%s'\n", str);

	DEBUGF("sec_str %s\n", gopt.sec_str);
	gopt.sec_str = GS_user_secret(&gopt.gs_ctx, gopt.sec_file, gopt.sec_str);
	if (gopt.sec_str == NULL)
		ERREXIT("%s\n", GS_CTX_strerror(&gopt.gs_ctx));

	VLOG("=Secret         : \"%s\"\n", gopt.sec_str);

	/* Convert a secret string to an address */
	GS_ADDR_str2addr(&gopt.gs_addr, gopt.sec_str);
	gopt.gsocket = gs_create();

	DEBUGF("PID = %d\n", getpid());

	signal(SIGTERM, cb_sigterm);
}

void
usage(const char *params)
{
	fprintf(stderr, "%s [0x%lxL] (GS v%s)\n", OPENSSL_VERSION_TEXT, OPENSSL_VERSION_NUMBER, PACKAGE_VERSION);

	while (*params)
	{
		switch (params[0])
		{
			case 'L':
				fprintf(stderr, "  -L <file>    Logfile\n");
				break;
			case 'q':
				fprintf(stderr, "  -q           Quite. No log output\n");
				break;
			case 'r':
				fprintf(stderr, "  -r           Receive-only. Terminate when no more data.\n");
				break;
			case 's':
				fprintf(stderr, "  -s <secret>  Secret (e.g. password).\n");
				break;
			case 'k':
				fprintf(stderr, "  -k <file>    Read Secret from file.\n");
				break;
			case 'l':
				fprintf(stderr, "  -l           Listening server [default: client]\n");
				break;
			case 'g':
				fprintf(stderr, "  -g           Generate a Secret (random)\n");
				break;
			case 'a':
				fprintf(stderr, "  -a <token>   Set listen password\n");
				break;
			case 'w':
				fprintf(stderr, "  -w           Wait for server to become available [client only]\n");
				break;
			case 'A':
				fprintf(stderr, "  -A           Be server if no server is listening\n");
				break;
			case 'C':
				fprintf(stderr, "  -C           Disable encryption\n");
				break;
			case 'T':
				fprintf(stderr, "  -T           Use TOR.\n");
				break;
		}

		params++;
	}
}

static void
zap_arg(char *str)
{
	int len;

	len = strlen(str);
	memset(str, '*', len);
}

void
do_getopt(int argc, char *argv[])
{
	int c;

	opterr = 0;
	while ((c = getopt(argc, argv, UTILS_GETOPT_STR)) != -1)
	{
		switch (c)
		{
			case 'L':
				gopt.is_logfile = 1;
				gopt.log_fp = fopen(optarg, "a");
				if (gopt.log_fp == NULL)
					ERREXIT("fopen(%s): %s\n", optarg, strerror(errno));
				gopt.err_fp = gopt.log_fp;
				break;
			case 'T':
				gopt.is_use_tor = 1;
				break;
			case 'q':
				gopt.is_quite = 1;
				break;
			case 'r':
				gopt.is_receive_only = 1;
				break;
			case 'i':
				gopt.is_interactive = 1;
				break;
			case 'C':
				gopt.is_no_encryption = 1;
				break;
			case 'A':
				gopt.is_client_or_server = 1;
				break;
			case 'w':
				gopt.is_sock_wait = 1;
				break;
			case 'a':
				/* This only becomes secure when the initial GS-network connection is done by TLS
				 * (at least for the listening server to submit the token securely to the server so that
				 * the server rejects any listening attempt that uses a bad token)
				 */
				VLOG("*** WARNING *** -a not fully supported yet. Trying our best...\n");
				gopt.token_str = optarg;
				break;
			case 'l':
				gopt.flags |= GSC_FL_IS_SERVER;
				break;
			case 's':
				gopt.sec_str = strdup(optarg);
				zap_arg(optarg);
				break;
			case 'k':
				gopt.sec_file = strdup(optarg);
				zap_arg(optarg);
				break;
			case 'g':		/* Generate a secret */
				printf("%s\n", GS_gen_secret());
				fflush(stdout);
				exit(0);
			case '3':
				if (strcmp(optarg, "1337") == 0)
					VLOG("!!Greets to 0xD1G, xaitax and the rest of https://t.me/thcorg!!\n");
		}
	}
}

static struct termios tios_saved;
static int is_stty_raw;
/*
 * Client only: Save TTY state and set raw mode.
 */
void
stty_set_raw(void)
{
	int ret;

	if (is_stty_raw != 0)
		return;

	if (!isatty(STDIN_FILENO))
		return;

    struct termios tios;
    ret = tcgetattr(STDIN_FILENO, &tios);
    if (ret != 0)
    	return;
    memcpy(&tios_saved, &tios, sizeof tios_saved);
    // -----BEGIN ORIG-----
 //    tios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	// tios.c_oflag &= ~(OPOST);
	// tios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	// tios.c_cflag |= (CS8);
	// -----BEGIN NEW-----
    // tios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	// tios.c_oflag &= ~(OPOST);
	// tios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	// tios.c_cflag &= ~(CSIZE | PARENB);	// stty -a shows rows/columns correctly
	// tios.c_cflag |= (CS8);
	// -----BEGIN SSH-----
    tios.c_iflag |= IGNPAR;
    tios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
#ifdef IUCLC
    tios.c_iflag &= ~IUCLC;
#endif
    tios.c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
#ifdef IEXTEN
    tios.c_lflag &= ~IEXTEN;
#endif
    tios.c_oflag &= ~OPOST;
    tios.c_cc[VMIN] = 1;
    tios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSADRAIN, &tios);
    // tcsetattr(STDIN_FILENO, TCSAFLUSH, &tios);
    
    /* Set NON blocking */
    // fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK | fcntl(STDIN_FILENO, F_GETFL, 0));

    is_stty_raw = 1;
}

/*
 * Restore TTY state
 */
void
stty_reset(void)
{
	if (is_stty_raw == 0)
		return;

	is_stty_raw = 0;
	DEBUGF_G("resetting TTY\n");
    tcsetattr(STDIN_FILENO, TCSADRAIN, &tios_saved);
}

static const char esc_seq[] = "\r~.\r";
static int esc_pos;
/*
 * In nteractive mode/Client mode check if User typed '\n~.\n' escape
 * sequence.
 */
void
stty_check_esc(GS *gs, char c)
{
	// DEBUGF_R("checking %d on esc_pos %d == %d\n", c, esc_pos, esc_seq[esc_pos]);
	if (c == esc_seq[esc_pos])
	{
		esc_pos++;
		if (esc_pos < sizeof esc_seq - 1)
			return;

		DEBUGF_M("ESC detected. EXITING...\n");
		/* Hard exit */
		stty_reset();
		exit(0);
		return;
	}
	esc_pos = 0;
	if (c == esc_seq[0])
		esc_pos = 1;
}

/*
 * Return SHELL and shell name (/bin/bash , -bash)
 */
static const char *
mk_shellname(char *shell_name, ssize_t len)
{
	const char *shell = getenv("SHELL");
	if ((shell == NULL) || (strlen(shell) == 0))
	{
		shell = "/bin/sh";	// default
		/* Try /bin/bash if available */
		struct stat sb;
		if (stat("/bin/bash", &sb) == 0)
			shell = "/bin/bash";
	}
	char *ptr = strrchr(shell, '/');
	if (ptr == NULL)
	{
		shell = "/bin/sh";
		ptr = "/sh";
	}
	snprintf(shell_name, len, "-%s", ptr + 1);

	return shell;
}

/*
 * Create an envp list from existing env. This is a hack for cmd-execution.
 * 'blacklist' contains env-vars which should not be part of the new
 * envp for the shell (such as STY, a screen variable, which we must remove).
 * 'addlist' contains env-vars that should be added _if_ they do not yet
 * exist.
 *
 * If blacklist and addlist contain the same variable then that variable
 * will be replaced with the one from addlist.
 */
char **
mk_env(char **blacklist, char **addlist)
{
	char **env;
	int total = 0;
	int add_total = 0;
	int i;
	char *end;
	int n;

	for (i = 0; environ[i] != NULL; i++)
		total++;

	for (i = 0; addlist[i] != NULL; i++)
		add_total++;

	// DEBUGF("Number of environment variables: %d (calloc(%d, %zu)\n", total, total + 1, sizeof *env);
	env = calloc(total + add_total + 1, sizeof *env);

	/* Copy to env unless variable is in blacklist */
	int ii = 0;
	for (i = 0; i < total; i++)
	{
		char *s = environ[i];

		/* Check if we want this env variable and continue if not */
		end = strchr(s, '=');
		if (end == NULL)
			continue;			// Illegal enviornment variable
		/* Check if the env is in the BLACK list */
		char **b = blacklist;
		for (; *b != NULL; b++)
		{
			if (end - s > strlen(*b))
				continue;
			if (memcmp(s, *b, end - s) == 0)
				break;			// In the blacklist
		}
		if (*b != NULL)
			continue;			// Skip if in blacklist

		env[ii] = strdup(s);
		ii++;
	}

	/* Append to env unless variable is already in env */
	int env_len = ii;
	int should_add;
	for (n = 0; addlist[n] != NULL; n++)
	{
		char *al_end = strchr(addlist[n], '=');
		if (al_end == NULL)
			continue;

		should_add = 1;
		for (i = 0; i < env_len; i++)
		{
			char *s = env[i];
			end = strchr(s, '=');
			if (end == NULL)
				continue;
			if (al_end - addlist[n] != end - s)
				continue;
			if (memcmp(s, addlist[n], end - s) == 0)
			{
				should_add = 0;
				break;	// Already in this list
			}
		}
		if (should_add != 0)
		{
			// DEBUGF_C("Adding %s\n", addlist[n]);
			env[ii] = strdup(addlist[n]);
			ii++;
		}

	}

	return env;
}

static void
setup_cmd_child(void)
{
	/* Close all (but 1 end of socketpair) fd's */
	int i;
	for (i = 3; i < FD_SETSIZE; i++)
		close(i);

	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
}

#if 0
/* 'resize' as per xterm() and using ANSI codes */
#define ESCAPE(string) "\033" string
#define PTY_RESIZE_STR	ESCAPE("7") ESCAPE("[r") ESCAPE("[9999;9999H") ESCAPE("[6n")
#define PTY_RESTORE		ESCAPE("8")
#define PTY_SIZE_STR	ESCAPE("[%d;%dR")
#define UIntClr(dst,bits) dst = dst & (unsigned) ~(bits)
static int tty;
static struct termios tioorig;
static int onintr_called;

static void
onintr(int sig)
{
	if (onintr_called)
		return;

	onintr_called = 1;
    (void) tcsetattr(tty, TCSADRAIN, &tioorig);
}

static void
resize_timeout(int sig)
{
	onintr(sig);
}

static int
readstring(int fd, char *buf, size_t sz, const char *str)
{
    unsigned char last;
    unsigned char c;
    int n;
    int rv = -1;
    char *end = buf + sz;

    signal(SIGALRM, resize_timeout);
    alarm(10);
    n = read(fd, &c, 1);
    if (n <= 0)
		goto err;

    if (c == 0233)
    {	/* meta-escape, CSI */
		*buf++ = ESCAPE("")[0];
		*buf++ = '[';
    } else {
		*buf++ = (char) c;
    }
    if (c != *str)
		goto err;

    last = str[strlen(str) - 1];	// R
    while (1)
    {
		n = read(fd, &c, 1);
		if (n <= 0)
			goto err;
		*buf++ = c;
		if (c == last)
			break;
		if (buf >= end)
			goto err;
    }

    alarm(0);
    *buf = 0;
    rv = 0;
err:
	if (rv != 0)
	{
		signal(SIGALRM, SIG_DFL);
		alarm(0);	// CANCEL alarm
	}
    return rv;
}


static void
pty_resize(int fd)
{
	int rv = -1;
    struct termios tio;
    int rows;
    int cols;
    char buf[64];
    struct winsize ws;


    tty = fd;
	rv = tcgetattr(tty, &tioorig);
    if (rv != 0)
		return;
    tio = tioorig;

	/* FIXME: set signal here */

    UIntClr(tio.c_iflag, ICRNL);
    UIntClr(tio.c_lflag, (ICANON | ECHO));
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 6;
    tio.c_cc[VTIME] = 1;
	rv = tcsetattr(tty, TCSADRAIN, &tio);
    if (rv != 0)
    {
    	goto err;
    }

    rv = write(fd, PTY_RESIZE_STR PTY_RESTORE, strlen(PTY_RESIZE_STR) + strlen(PTY_RESTORE));
    rv = readstring(tty, buf, sizeof buf, PTY_SIZE_STR);
    if (rv != 0)
		goto err;
    if (sscanf(buf, PTY_SIZE_STR, &rows, &cols) != 2)
		goto err;

    memset(&ws, 0, sizeof ws);
    ws.ws_col = cols;
    ws.ws_row = rows;

    ioctl(fd, TIOCSWINSZ, &ws);
    onintr(0);
	// fprintf(stderr, "rows %d, cols %d\n", rows, cols);
    rv = 0;
err:
	if (rv != 0)
	{
	}
	/* reset terminal values. cleanup */
	onintr(0);
}

#endif

#ifndef HAVE_OPENPTY
static int
openpty(int *amaster, int *aslave, void *a, void *b, void *c)
{
	int master;
	int slave;

	master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master == -1)
		return -1;

	if (grantpt(master) != 0)
		return -1;
	if (unlockpt(master) != 0)
		return -1;

	slave = open(ptsname(master), O_RDWR | O_NOCTTY);
	if (slave < 0)
		return -1;

# if defined __sun || defined __hpux /* Solaris, HP-UX */
  if (ioctl (slave, I_PUSH, "ptem") < 0
      || ioctl (slave, I_PUSH, "ldterm") < 0
#  if defined __sun
      || ioctl (slave, I_PUSH, "ttcompat") < 0
#  endif
     )
    {
      close (slave);
      return -1;
    }
# endif	

    *amaster = master;
    *aslave = slave;

    return 0;
}
#endif	/* HAVE_OPENPTY */

#ifndef HAVE_FORKPTY
static int
forkpty(int *fd, void *a, void *b, void *c)
{
	pid_t pid;
	int slave;
	int master;

	if (openpty(&master, &slave, NULL, NULL, NULL) == -1)
		return -1;

	pid = fork();
	switch (pid)
	{
		case -1:
			return -1;
		case 0:
			/* CHILD */
		#ifdef TIOCNOTTY
			ioctl(slave, TIOCNOTTY, NULL);
		#endif
			setsid();
			close(master);
			dup2(slave, 0);
			dup2(slave, 1);
			dup2(slave, 2);
			*fd = slave;
			return 0;	// CHILD
		default:
			/* PARENT */
			close(slave);
			*fd = master;
			return pid;
	}

	return -1; // NOT REACHED
}
#endif /* HAVE_FORKPTY */

int
pty_cmd(const char *cmd)
{
	pid_t pid;
	int fd;
	
	pid = forkpty(&fd, NULL, NULL, NULL);
	XASSERT(pid >= 0, "Error: forkpty(): %s\n", strerror(errno));

	if (pid == 0)
	{
		/* Our own forkpty() (solaris 10) returns the actual slave TTY.
		 * We can not open /dev/tty on solaris10 and use the fd that
		 * our own forkpty() returns. Any other OS needs to open
		 * /dev/tty to get correct fd for child's tty.
		 */
		#ifdef HAVE_FORKPTY
		fd = open("/dev/tty", O_NOCTTY | O_RDWR);
		#endif
		if (fd >= 0)
		{
			//pty_resize(fd);
			close(fd);
		}

		/* HERE: Child */
		setup_cmd_child();

		/* Find out default ENV (just in case they do not exist in current
		 * env-variable such as when started during bootup
		 */
		const char *shell;		// e.g. /bin/bash
		char shell_name[64];	// e.g. -bash
		if (cmd != NULL)
		{
			shell = "/bin/sh";
		} else {
			shell = mk_shellname(shell_name, sizeof shell_name);
		}
		char shell_env[64];		// e.g. SHELL=/bin/bash
		snprintf(shell_env, sizeof shell_env, "SHELL=%s", shell);

		char home_env[128];
		snprintf(home_env, sizeof home_env, "HOME=/root");	// default
		struct passwd *pwd;
		pwd = getpwuid(getuid());
		if (pwd != NULL)
			snprintf(home_env, sizeof home_env, "HOME=%s", pwd->pw_dir);

		/* Remove some environment variables:
		 * STY = Confuses screen if gs-netcat is started from within screen (OSX)
		 * GSOCKET_ARGS = Otherwise any further gs-netcat command would
		 *    execute with same (hidden) commands as the current shell.
		 * HISTFILE= does not work on oh-my-zsh (it sets it again)
		 */
		char *env_blacklist[] = {"STY", "GSOCKET_ARGS", "HISTFILE", NULL};
		char *env_addlist[] = {shell_env, "TERM=xterm-256color", "HISTFILE=\"\"", home_env, NULL};
		char **envp = mk_env(env_blacklist, env_addlist);

		if (cmd != NULL)
		{
			execle("/bin/sh", cmd, "-c", cmd, NULL, envp);
			ERREXIT("exec(%s) failed: %s\n", cmd, strerror(errno));
		} 

		const char *args = "-il";	// bash, fish, zsh
		if (strcmp(shell_name, "-sh") == 0)
			args = "-i";	// solaris 10 /bin/sh does not like -l
		if (strcmp(shell_name, "-csh") == 0)
			execle(shell, shell_name, NULL, envp); // csh (fbsd) without any arguments

		execle(shell, shell_name, args, NULL, envp);
		ERREXIT("execlp(%s) failed: %s\n", shell, strerror(errno));
	}
	/* HERE: Parent */

	return fd;
}

/*
 * Spawn a cmd and return fd.
 */
int
fd_cmd(const char *cmd)
{
	pid_t pid;
	int fds[2];
	int ret;

	DEBUGF("cmd == %s\n", cmd);

	if (gopt.is_interactive)
	{
		return pty_cmd(cmd);
	}

	ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
	if (ret != 0)
		ERREXIT("pipe(): %s\n", strerror(errno));	/* FATAL */

	pid = fork();
	if (pid < 0)
		ERREXIT("fork(): %s\n", strerror(errno));	/* FATAL */

	if (pid == 0)
	{
		/* HERE: Child process */
		dup2(fds[0], STDOUT_FILENO);
		dup2(fds[0], STDERR_FILENO);
		dup2(fds[0], STDIN_FILENO);
		setup_cmd_child();

		execl("/bin/sh", cmd, "-c", cmd, NULL);
		ERREXIT("exec(%s) failed: %s\n", cmd, strerror(errno));
	}

	/* HERE: Parent process */
	close(fds[0]);

	return fds[1];
}

/*
 * Complete the connect() call
 * Return -1 on waiting.
 * Return -2 on fatal.
 */
int
fd_net_connect(GS_SELECT_CTX *ctx, int fd, uint32_t ip, uint16_t port)
{
	struct sockaddr_in addr;
	int ret;

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ip;
	addr.sin_port = port;
	ret = connect(fd, (struct sockaddr *)&addr, sizeof addr);
	DEBUGF("connect(%s:%d, fd = %d): %d (errno = %d)\n", int_ntoa(ip), ntohs(port), fd, ret, errno);
	if (ret != 0)
	{
		if ((errno == EINPROGRESS) || (errno == EAGAIN) || (errno == EINTR))
		{
			XFD_SET(fd, ctx->wfd);
			return -1;
		}
		if (errno != EISCONN)
		{
			DEBUGF_R("ERROR %s\n", strerror(errno));
			// gs_set_error(gsocket->ctx, "connect(%s:%d)", int_ntoa(ip), ntohs(port));
			return -2;
		}
	}
	/* HERRE: ret == 0 or errno == EISCONN (Socket is already connected) */
	DEBUGF_G("connect(fd = %d) SUCCESS (errno = %d)\n", fd, errno);

	return 0;
}

int
fd_net_accept(int listen_fd)
{
	int sox;
	int ret;

	sox = accept(listen_fd, NULL, NULL);
	DEBUGF_B("accept(%d) == %d\n", listen_fd, sox);
	if (sox < 0)
		return -2;

	ret = fcntl(sox, F_SETFL, O_NONBLOCK | fcntl(sox, F_GETFL, 0));
	if (ret != 0)
		return -2;

	return sox;
}

/*
 * Create a listening fd on port.
 */
int
fd_net_listen(int fd, uint16_t port)
{
	struct sockaddr_in addr;
	int ret;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof (int));

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = port;

	ret = bind(fd, (struct sockaddr *)&addr, sizeof addr);
	if (ret < 0)
		return ret;

	ret = listen(fd, 1);
	if (ret != 0)
		return -1;

	return 0;
}

/*
 * Return fd on success.
 * Return < 0 on fata error.
 */
int
fd_new_socket(void)
{
	int fd;
	int ret;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -2;
	DEBUGF_W("socket() == %d\n", fd);

	ret = fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL, 0));
	if (ret != 0)
		return -2;

	return fd;
}

void
cmd_ping(struct _peer *p)
{
	if (gopt.is_want_ping != 0)
		return;

	gopt.is_want_ping = 1;
	GS_SELECT_FD_SET_W(p->gs);
}


const char fname_valid_char[] = ""
"................"
"................"
" !.#$%&.()*+,-.."	/* Dont allow " or / or ' */
"0123456789:;.=.."	/* Dont allow < or > or ? */
"@ABCDEFGHIJKLMNO"
"PQRSTUVWXYZ[.]^_"	/* Dont allow \ */
".abcdefghijklmno"	/* Dont allow ` */
"pqrstuvwxyz{.}.." 	/* Dont allow | or ~ */
"";

void
sanitize_fname_to_str(uint8_t *str, size_t len)
{
	int i;
	uint8_t c;

	for (i = 0; i + 1 < len; i++)
	{
		c = str[i];
		if (c < sizeof fname_valid_char)
		{
			if (c == fname_valid_char[c])
				continue;
		}
		if (c == 0)
			break;
		DEBUGF("san 0x%02x\n", c);
		str[i] = '#'; // Change to # if invalid character
	}

	str[i] = 0x00; // always 0 terminate
}


static const char unit[] = "BKMGT";
void
format_bps(char *buf, size_t size, int64_t bytes)
{
	int i;

	if (bytes < 1000)
	{
		snprintf(buf, size, "%3d.0 B", (int)bytes);
		return;
	}
	bytes *= 100;

	for (i = 0; bytes >= 100*1000 && unit[i] != 'T'; i++)
		bytes = (bytes + 512) / 1024;
	snprintf(buf, size, "%3lld.%1lld%c%s",
            (long long) (bytes + 5) / 100,
            (long long) (bytes + 5) / 10 % 10,
            unit[i],
            i ? "B" : " ");
}













