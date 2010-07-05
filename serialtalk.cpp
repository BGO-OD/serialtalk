#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <termios.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/time.h>

void usage(char *const argv[]) {
	fprintf(stderr,"usage: %s <options> device\n",argv[0]);
	fprintf(stderr,"\tExit by ctrl-c (kill)!\n");
	fprintf(stderr,"\t-b number\tset baudrate\n");
	fprintf(stderr,"\t-o number\tset output baudrate\n"
					"\t\tavailable: 0, 50, 75, 110, 134, 150, 200, 300, 600,\n"
					"\t\t1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600,\n"
					"\t\t115200, 230400\n");
	fprintf(stderr,"\t-t number\tset poll timeout in milliseconds\n"
					"\t\t0 means no loop, negative is for ever\n");
	fprintf(stderr,"\t-B number\tset number of bits, must be 5,6,7, or 8\n");
	fprintf(stderr,"\t-s\tshow state after timeout, needs timeout value!\n");
	fprintf(stderr,"\t-v\tbe verbose, show parameters\n");
	fprintf(stderr,"\t-d\tclear DTR line\n");
	fprintf(stderr,"\t-D\tset   DTR line\n");
	fprintf(stderr,"\t-r\tclear RTS line\n");
	fprintf(stderr,"\t-R\tset   RTS line\n");
	fprintf(stderr,"\t-c\tclear CTS line\n");
	fprintf(stderr,"\t-C\tset   CTS line\n");
	fprintf(stderr,"\t-p\tenable odd parity\n");
	fprintf(stderr,"\t-P\tenable even parity\n");



	fprintf(stderr,"\t-h\tthis help\n");
	exit(EXIT_FAILURE);
}

void printstate(int state) {
	fprintf(stderr,"%s%s%s%s%s%s%s%s%s\n",
					(state & TIOCM_LE) ? "LE " : "",
					(state & TIOCM_DTR) ? "DTR " : "",
					(state & TIOCM_RTS) ? "RTS " : "",
					(state & TIOCM_ST) ? "ST " : "",
					(state & TIOCM_SR) ? "SR " : "",
					(state & TIOCM_CTS) ? "CTS " : "",
					(state & TIOCM_CAR) ? "CAR " : "",
					(state & TIOCM_RNG) ? "RI " : "",
					(state & TIOCM_DSR) ? "DSR " : "");
}

void getstate(int fd) {
	int state;
	struct timeval now;
	ioctl(fd,TIOCMGET,&state);
	gettimeofday(&now,NULL);
	fprintf(stderr,"State: %d.%06d ",
					(int)(now.tv_sec), (int)(now.tv_usec));
	printstate(state);
}


int main(int argc, char *const argv[]) {
	struct termios fd_attr;
	int fd;
	int opt;
	int verbose=0;
	int baudin=9600;
	int baudout=0;
	int showstate=0;
	int timeout=-1;
	int setlines=0;
	int clearlines=0;
	int bits=CS8;
	int parity=0;
	while ((opt=getopt(argc,argv,"b:o:t:B:svdDrRcCpPh")) != -1) {
		switch (opt) {
		case 'b': baudin=strtol(optarg,NULL,10); break;
		case 'o': baudout=strtol(optarg,NULL,10); break;
		case 't': timeout=strtol(optarg,NULL,10); break;
		case 'B': switch (optarg[0]) {
			case '5': bits=CS5; break;
			case '6': bits=CS6; break;
			case '7': bits=CS7; break;
			case '8': bits=CS8; break;
			default: usage(argv);
			} break;
		case 'v': verbose=1; break;
		case 's': showstate=1; break;
		case 'd': clearlines |= TIOCM_DTR; break;
		case 'D': setlines   |= TIOCM_DTR; break;
		case 'r': clearlines |= TIOCM_RTS; break;
		case 'R': setlines   |= TIOCM_RTS; break;
		case 'c': clearlines |= TIOCM_CTS; break;
		case 'C': setlines   |= TIOCM_CTS; break;
		case 'p': parity = 1; break;
		case 'P': parity = 2; break;
		case 'h':
		default: usage(argv);
		}
	}
	if (optind>=argc) {
		usage(argv);
	}

	if (baudout==0) {
		baudout = baudin;
	}
	const char *devname=argv[optind];
	if (verbose) {
		fprintf(stderr,"will now open \"%s\"...\n",devname);
	}
	fd = open(devname,O_RDWR | O_NOCTTY|O_NONBLOCK);
	if (fd == -1) {
		perror("can't open device");
		exit(1);
	}
	if (verbose) {
		fprintf(stderr,"done.\n");
		fprintf(stderr,"Parameters:\n");
		fprintf(stderr,"\tinput baudrate\t%d\n",baudin);
		fprintf(stderr,"\toutput baudrate\t%d\n",baudout);
		fprintf(stderr,"\twill set "); printstate(setlines);
		fprintf(stderr,"\twill clear "); printstate(clearlines);
		fprintf(stderr,"\t%d bits\n",(bits==CS8) ? 8 : ((bits==CS7) ? 7 : ((bits==CS6) ? 6 : ((bits==CS5) ? 5 : (-1)))));
		fprintf(stderr,"\t%s parity\n",parity ? ((parity==1)?"odd":"even") : "no"); 
	}
	


	fd_attr.c_iflag = IGNBRK;
	if (parity==0) {
		fd_attr.c_iflag |= IGNPAR;
	}
	fd_attr.c_iflag &= ~(INLCR | IGNCR | ICRNL);
	fd_attr.c_oflag = 0;
	fd_attr.c_cflag = bits	/* number of bits per character */
		| CREAD		/* enable receiver */
		| CLOCAL;	/* ignore modem control lines */
	if (parity) {
		fd_attr.c_cflag |= PARENB;
	}
	if (parity==1) {
		fd_attr.c_cflag |= PARODD;
	}
	fd_attr.c_lflag = 0;
	speed_t baud;
	switch (baudout) {
	case 0: baud=B0;break;
	case 50: baud=B50;break;
	case 75: baud=B75;break;
	case 110: baud=B110;break;
	case 134: baud=B134;break;
	case 150: baud=B150;break;
	case 200: baud=B200;break;
	case 300: baud=B300;break;
	case 600: baud=B600;break;
	case 1200: baud=B1200;break;
	case 1800: baud=B1800;break;
	case 2400: baud=B2400;break;
	case 4800: baud=B4800;break;
	case 9600: baud=B9600;break;
	case 19200: baud=B19200;break;
	case 38400: baud=B38400;break;
	case 57600: baud=B57600;break;
	case 115200: baud=B115200;break;
	case 230400: baud=B230400;break;
	default: fprintf(stderr,"illegal baud rate %d",baudout);
		exit(EXIT_FAILURE);
	}
	cfsetospeed(&fd_attr,baud); /* baud rate is 9600 Baud */
	switch (baudin) {
	case 0: baud=B0;break;
	case 50: baud=B50;break;
	case 75: baud=B75;break;
	case 110: baud=B110;break;
	case 134: baud=B134;break;
	case 150: baud=B150;break;
	case 200: baud=B200;break;
	case 300: baud=B300;break;
	case 600: baud=B600;break;
	case 1200: baud=B1200;break;
	case 1800: baud=B1800;break;
	case 2400: baud=B2400;break;
	case 4800: baud=B4800;break;
	case 9600: baud=B9600;break;
	case 19200: baud=B19200;break;
	case 38400: baud=B38400;break;
	case 57600: baud=B57600;break;
	case 115200: baud=B115200;break;
	case 230400: baud=B230400;break;
	default: fprintf(stderr,"illegal baud rate %d",baudin);
		exit(EXIT_FAILURE);
	}
	cfsetispeed(&fd_attr,baud);
	if (tcsetattr(fd,TCSANOW,&fd_attr) == -1) {/* commit changes NOW */
		perror("can't setattr");
		close(fd);
		exit(1);
	}
	int flags;
	flags=fcntl(fd,F_GETFL);
	flags &= ~O_NONBLOCK;
	if (fcntl(fd,F_SETFL,flags) != 0) {
		perror("can't go to blocking mode");
	}
	
	struct pollfd pollfds[2];
	pollfds[0].fd = fd;
	pollfds[0].events = POLLIN | POLLERR;
	pollfds[1].fd = 0;
	pollfds[1].events = POLLIN;


	if (showstate) getstate(fd);
	ioctl(fd,TIOCMBIC,&clearlines);
	ioctl(fd,TIOCMBIS,&setlines);
	if ((clearlines || setlines) && showstate) {
		getstate(fd);
	}
	for (;timeout;) {
		int result;
		result=poll(pollfds,
									sizeof(pollfds)/sizeof(pollfds[0]),
									timeout);
		if (result==0 && showstate) {
			getstate(fd);
		}
		if (pollfds[0].revents & POLLERR) { /* some problem occurred */
			struct timeval now;
			gettimeofday(&now,NULL);
			fprintf(stderr,"Error: %d.%06d\n",
					(int)(now.tv_sec), (int)(now.tv_usec));
		}
		if (pollfds[0].revents & POLLIN) { /* data from tty to stdout */
			char c;
			read(fd,&c,1);
			write(1,&c,1);
		}
		if (pollfds[1].revents & POLLIN) { /* data from stdin to tty */
			char c;
			read(1,&c,1);
			write(fd,&c,1);
		}
	}
	close(fd);
}
