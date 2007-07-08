
/*
 * cronload.real.c
 *
 * CRONTAB
 *
 * usually setuid root, -c option only works if getuid() == geteuid()
 *
 * Copyright 1994 Matthew Dillon (dillon@apollo.backplane.com)
 * May be distributed under the GNU General Public License
 */

#include "defs.h"

#define VERSION	"$Revision$"

const char *CDir = SCRIPTS_CRONTABS;
int   UserId;
short LogLevel = 9;

int GetReplaceStream(const char *user, const char *file);
extern int ChangeUser(const char *user, short dochdir);

int
main(int ac, char **av)
{
    enum { NONE, LIST, REPLACE, DELETE } option = NONE;
    struct passwd *pas;
    char *repFile = NULL;
    int repFd = 0;
    int i;
    char caller[256];		/* user that ran program */

    UserId = getuid();
    if ((pas = getpwuid(UserId)) == NULL) {
        perror("getpwuid");
        exit(1);
    }
    snprintf(caller, sizeof(caller), "%s", pas->pw_name);

    i = 1;
    if (ac > 1) {
        if (av[1][0] == '-' && av[1][1] == 0) {
            option = REPLACE;
            ++i;
	} else if (av[1][0] != '-') {
            option = REPLACE;
            ++i;
            repFile = av[1];
	}
    }

    for (; i < ac; ++i) {
        char *ptr = av[i];

        if (*ptr != '-')
            break;
	ptr += 2;

	switch(ptr[-1]) {
	case 'l':
	    if (ptr[-1] == 'l')
		option = LIST;
	    /* fall through */
	case 'd':
	    if (ptr[-1] == 'd')
		option = DELETE;
	    /* fall through */
	case 'u':
	    if (i + 1 < ac && av[i+1][0] != '-') {
	        ++i;
	        if (getuid() == geteuid()) {
		    pas = getpwnam(av[i]);
		    if (pas) {
			UserId = pas->pw_uid;
		    } else {
			errx(1, "user %s unknown\n", av[i]);
		    }
		} else {
		    errx(1, "only the superuser may specify a user\n");
		}
	    }
	    break;
	case 'c':
	    if ((getuid() == geteuid()) && (0 == getuid())) {
		CDir = (*ptr) ? ptr : av[++i];
	    } else {
	        errx(1, "-c option: superuser only\n");
	    }
	    break;
	default:
	    i = ac;
	    break;
	}
    }
    if (i != ac || option == NONE) {
	printf("cronload.real " VERSION "\n");
	printf("cronload.real file <opts>  replace crontab from file\n");
	printf("cronload.real -    <opts>  replace crontab from stdin\n");
	printf("cronload.real -u user      specify user\n");
	printf("cronload.real -l [user]    list crontab for user\n");
	printf("cronload.real -d [user]    delete crontab for user\n");
	printf("cronload.real -c dir       specify crontab directory\n");
	exit(0);
    }

    /*
     * Get password entry
     */

    if ((pas = getpwuid(UserId)) == NULL) {
        perror("getpwuid");
        exit(1);
    }

    /*
     * If there is a replacement file, obtain a secure descriptor to it.
     */

    if (repFile) {
        repFd = GetReplaceStream(caller, repFile);
        if (repFd < 0) {
            errx(1, "unable to read replacement file\n");
        }
    }

    /*
     * Change directory to our crontab directory
     */

    if (chdir(CDir) < 0) {
        errx(1, "cannot change dir to %s: %s\n", CDir, strerror(errno));
    }

    /*
     * Handle options as appropriate
     */

    switch(option) {
    case LIST:
	{
	    FILE *fi;
	    char buf[1024];

	    if ((fi = fopen(pas->pw_name, "r"))) {
		while (fgets(buf, sizeof(buf), fi) != NULL)
		    fputs(buf, stdout);
		fclose(fi);
	    } else {
		fprintf(stderr, "no crontab for %s\n", pas->pw_name);
	    }
	}
	break;
    case REPLACE:
	{
	    char buf[1024];
	    char path[1024];
	    int fd;
	    int n;

	    snprintf(path, sizeof(path), "%s.new", pas->pw_name);
	    if ((fd = open(path, O_CREAT|O_TRUNC|O_EXCL|O_APPEND|O_WRONLY, 0600)) >= 0) {
		while ((n = read(repFd, buf, sizeof(buf))) > 0) {
		    write(fd, buf, n);
		}
		close(fd);
		rename(path, pas->pw_name);
	    } else {
		fprintf(stderr, "unable to create %s/%s: %s\n", 
		    CDir,
		    path,
		    strerror(errno)
		);
	    }
	    close(repFd);
	}
	break;
    case DELETE:
        remove(pas->pw_name);
        break;
    case NONE:
    default: 
        break;
    }

    /*
     *  Bump notification file.  Handle window where crond picks file up
     *  before we can write our entry out.
     */
	/* // only applicable to dcron
    if (option == REPLACE || option == DELETE) {
        FILE *fo;
        struct stat st;

        while ((fo = fopen(CRONUPDATE, "a"))) {
			fprintf(fo, "%s\n", pas->pw_name);
			fflush(fo);
			if (fstat(fileno(fo), &st) != 0 || st.st_nlink != 0) {
			fclose(fo);
			break;
			}
			fclose(fo);
			// * loop * /
		}
		if (fo == NULL) {
			fprintf(stderr, "unable to append to %s/%s\n", CDir, CRONUPDATE);
		}
    }
    */
    (volatile void)exit(0);
    /* not reached */
}

int
GetReplaceStream(const char *user, const char *file)
{
    int filedes[2];
    int pid;
    int fd;
    int n;
    char buf[1024];

    if (pipe(filedes) < 0) {
        perror("pipe");
        return(-1);
    }
    if ((pid = fork()) < 0) {
        perror("fork");
        return(-1);
    }
    if (pid > 0) {
        /*
         * PARENT
         */

	close(filedes[1]);
	if (read(filedes[0], buf, 1) != 1) {
	    close(filedes[0]);
	    filedes[0] = -1;
	}
	return(filedes[0]);
    }

    /*
     * CHILD
     */

    close(filedes[0]);

    if (ChangeUser(user, 0) < 0)
        exit(0);

    fd = open(file, O_RDONLY);
    if (fd < 0)
        errx(0, "unable to open %s\n", file);
    buf[0] = 0;
    write(filedes[1], buf, 1);
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(filedes[1], buf, n);
    }
    exit(0);
}
