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
#include <sys/socket.h>
#include <netdb.h>

/* This is for ASYNC_*, serial_struct on linux => custom baud rates. */
#if defined(__linux__)
#define CUSTOM_BAUD_POSSIBLE
#include <linux/serial.h>
#endif

static int timeout=-1;
static int fd;
static bool timing=false;
static int translate_to_crnl=0;
static int translate_from_crnl=0;
static int wait=0;
static bool showstate=false;
static int verbose=0;
static int baudin=9600;
static int baudout=0;
static int setlines=0;
static int clearlines=0;
static bool canonical=true;
static int bits=CS8;
static int parity=0;
static bool noecho=false;
static bool do_hupcl=false;
static bool send_ctrl_c=false;
static int listenPort=0;
static bool doFork=false;


static void usage(char *const argv[]) __attribute__ ((noreturn));
static void usage(char *const argv[]) {
	fprintf(stderr,"usage: %s <options> device\n",argv[0]);
	fprintf(stderr,"\tExit by ctrl-c (kill)!\n");
	fprintf(stderr,"\t-b number\tset baudrate (%d)\n",baudin);
	fprintf(stderr,"\t-o number\tset output baudrate (%d) 0: the same as input\n"
					"\t\tavailable: 0, 50, 75, 110, 134, 150, 200, 300, 600,\n"
					"\t\t1200, 1800, 2400, 4800, 9600 (default), 19200, 38400,\n"
					"\t\t57600, 115200, 230400, 500000, 576000, 921600, 1000000,\n" 
	        "\t\t1152000, 1500000, 2000000, 2500000, 3000000, 3500000, 4000000\n",baudout);
#ifdef CUSTOM_BAUD_POSSIBLE	
	fprintf(stderr,"\t\tFor input=output baudrate, non-POSIX baudrates are supported.\n");
#endif
	fprintf(stderr,"\t-t number\tset poll timeout in milliseconds (%d)\n"
	        "\t\t0 means no loop, negative is for ever\n",timeout);
	fprintf(stderr,"\t-B number\tset number of bits, must be 5,6,7, or 8   (%d)\n",
	        bits==CS5?5:(bits==CS6?6:(bits==CS7?7:8)));
	fprintf(stderr,"\t-s\tshow state after timeout, needs timeout value! (%s)\n",showstate?"yes":"no");
	fprintf(stderr,"\t-v\tbe verbose, show parameters (%d)\n",verbose);
	fprintf(stderr,"\t-d\tclear DTR line (%s)\n", clearlines & TIOCM_DTR ? "yes":"no");
	fprintf(stderr,"\t-D\tset   DTR line (%s)\n", setlines & TIOCM_DTR ? "yes":"no");
	fprintf(stderr,"\t-r\tclear RTS line (%s)\n", clearlines & TIOCM_RTS ? "yes":"no");
	fprintf(stderr,"\t-R\tset   RTS line (%s)\n", setlines & TIOCM_RTS ? "yes":"no");
	fprintf(stderr,"\t-c\tclear CTS line (%s)\n", clearlines & TIOCM_CTS ? "yes":"no");
	fprintf(stderr,"\t-C\tset   CTS line (%s)\n", setlines & TIOCM_CTS ? "yes":"no");
	fprintf(stderr,"\t-p\tenable odd parity (default: %s)\n",parity==0?"none":(parity==1?"odd":"even"));
	fprintf(stderr,"\t-P\tenable even parity\n");
	fprintf(stderr,"\t-n\tnon-canonical stdin, read immediately (not at newline) (%scanonical)\n",
	        (!canonical)?"yes, non-":"no, ");
	fprintf(stderr,"\t-N\tno local echo for typed characters (%s)\n",noecho?"yes, no echo":"no, do echo");
	fprintf(stderr,"\t-k\tsend ctrl-c and exit (%s)\n",send_ctrl_c?"yes":"no");
	fprintf(stderr,"\t-T\tprint time stamps (%s)\n",timing?"yes":"no");
	fprintf(stderr,"\t-x\ttranslate nl to cr nl (from terminal to device) (%s)\n",translate_to_crnl==1?"yes":"no");
	fprintf(stderr,"\t-X\ttranslate nl to cr (from terminal to device) (%s)\n",translate_to_crnl==2?"yes":"no");
	fprintf(stderr,"\t-y\ttranslate cr to cr nl (from device to terminal) (%s)\n",translate_from_crnl==1?"yes":"no");
	fprintf(stderr,"\t-Y\ttranslate cr to nl (from device to terminal) (%s)\n",translate_from_crnl==2?"yes":"no");
	fprintf(stderr,"\t-H\tdo HUPCL (lower control lines after close) (%s)\n",do_hupcl?"yes":"no");
	fprintf(stderr,"\t-w number\twait number miliseconds after end of stdin before close (%d)\n",wait);
	fprintf(stderr,"\t-l port\tlisten on port port for tcp connections and use them instead of stdin/stdout (%d)\n",listenPort);
	fprintf(stderr,"\t-f\tfork into a daemonic life (%s)\n",doFork?"yes":"no");
	
	fprintf(stderr,"\t-h\tthis help\n");
	exit(EXIT_FAILURE);
}

static void printstate(int state) {
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

static void getstate(int ffd) {
	int state;
	struct timeval now;
	ioctl(ffd,TIOCMGET,&state);
	gettimeofday(&now,NULL);
	fprintf(stderr,"State: %d.%06d ",
					(int)(now.tv_sec), (int)(now.tv_usec));
	printstate(state);
}



static void talkLoop(struct pollfd *pollfds, int nfds, int outFd) {
	for (;timeout;) {
		int result;
		struct timeval now;
		result=poll(pollfds,
									nfds,
									timeout);
		gettimeofday(&now,NULL);
		if (nfds==1 && result==0) {
			wait -= timeout;
		}
		if (nfds==1 && result==0 && timeout>0 && wait<=0) {
			break;
		}
		if (result==0 && showstate) {
			getstate(fd);
		}
		if (pollfds[0].revents & POLLERR) { /* some problem occurred */
			fprintf(stderr,"Error: %d.%06d %s\n",
							(int)(now.tv_sec), (int)(now.tv_usec),strerror(errno));
		}
		if (pollfds[0].revents & POLLIN) { /* data from tty to stdout */
			char c;
			read(fd,&c,1);
			if (timing) {
				fprintf(stderr,"Received %d.%06d '%c' (0x%02x)\n",
								(int)(now.tv_sec), (int)(now.tv_usec),c<' '?' ':c,c);
			}
			if (translate_from_crnl && (c=='\r')) {
				if (translate_from_crnl==1) {
					write(outFd,&c,1);
				}
				c='\n';
			}
			write(outFd,&c,1);
		}
		if (pollfds[1].revents & POLLIN) { /* data from stdin to tty */
			char c;
			read(pollfds[1].fd,&c,1);
			if (translate_to_crnl && (c=='\n')) {
				char cr='\r';
				gettimeofday(&now,NULL);
				write(fd,&cr,1);
				if (timing) {
					fprintf(stderr,"Sent %d.%06d <cr> (0x%02x)\n",
									(int)(now.tv_sec), (int)(now.tv_usec),cr);
				}
			} 
			if (translate_to_crnl < 2 || (c!='\n')) {
				gettimeofday(&now,NULL);
				write(fd,&c,1);
				if (timing) {
					fprintf(stderr,"Sent %d.%06d '%c' (0x%02x)\n",
									(int)(now.tv_sec), (int)(now.tv_usec),c<' '?' ':c,c);
				}
			}
		}
		if (((pollfds[1].revents & POLLHUP) && !(pollfds[1].revents & POLLIN))
		    || (pollfds[1].revents & POLLRDHUP)) { 
			if (timeout > 0) {
				nfds=1;
			} else {
				break;
			}
		}
	}
}



int main(int argc, char *const argv[]) {
	struct termios fd_attr;
	int opt;

	while ((opt=getopt(argc,argv,"b:o:t:B:svdDrRcCpPnNkxXyYTHw:hl:f")) != -1) {
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
		case 'v': verbose++; break;
		case 's': showstate=true; break;
		case 'd': clearlines |= TIOCM_DTR; break;
		case 'D': setlines   |= TIOCM_DTR; break;
		case 'r': clearlines |= TIOCM_RTS; break;
		case 'R': setlines   |= TIOCM_RTS; break;
		case 'c': clearlines |= TIOCM_CTS; break;
		case 'C': setlines   |= TIOCM_CTS; break;
		case 'p': parity = 1; break;
		case 'P': parity = 2; break;
		case 'n': canonical=false; break;
		case 'N': noecho=true; break;
		case 'x': translate_to_crnl=1; break;
		case 'X': translate_to_crnl=2; break;
		case 'y': translate_from_crnl=1; break;
		case 'Y': translate_from_crnl=2; break;
		case 'T': timing=true; break;
		case 'H': do_hupcl=true; break;
		case 'k': send_ctrl_c=true; break;
		case 'w': wait=strtol(optarg,NULL,10); break;
		case 'l': listenPort=strtol(optarg,NULL,10); break;
		case 'f': doFork=true; break;
		case 'h':
		default: usage(argv);
		}
	}
	if (optind>=argc) {
		usage(argv);
	}

	if (wait && timeout==-1) {
		timeout = wait;
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
	

	if (!canonical) {
		struct termios attr;
		tcgetattr(0,&attr);
		attr.c_lflag &= ~ICANON;
		tcsetattr(0,TCSANOW,&attr);
	}
	if (noecho) {
		struct termios attr;
		tcgetattr(0,&attr);
		attr.c_lflag &= ~ECHO;
		tcsetattr(0,TCSANOW,&attr);
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
	if (do_hupcl) {
		fd_attr.c_cflag |= HUPCL;
	}
	fd_attr.c_lflag = 0;
	bool customBaudPossible=false;
#ifdef CUSTOM_BAUD_POSSIBLE
	customBaudPossible=true;
#endif
	bool needCustomBaud=false;
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
	case 500000: baud=B500000;break;
	case 576000: baud=B576000;break;  
	case 921600: baud=B921600;break;  
	case 1000000: baud=B1000000;break; 
	case 1152000: baud=B1152000;break; 
	case 1500000: baud=B1500000;break; 
	case 2000000: baud=B2000000;break; 
	case 2500000: baud=B2500000;break; 
	case 3000000: baud=B3000000;break; 
	case 3500000: baud=B3500000;break; 
	case 4000000: baud=B4000000;break; 
	default: 
		{
			if (customBaudPossible) {
				needCustomBaud = true;
				if (verbose) {
					fprintf(stderr,"non-POSIX baud rate %d\r\n",baudout);
				}
				if (baudout != baudin) {
					fprintf(stderr,"non-POSIX baud rates only supported if input = output baud rate!\r\n");
					fprintf(stderr,"You set input baud rate to %d, output to %d\r\n",baudin,baudout);
					exit(EXIT_FAILURE);
				}
				// Setting serial-divisor only works fine for 38400 baud. 
				baud=B38400;
			} else {
				fprintf(stderr,"illegal baud rate %d\r\n",baudout);
				exit(EXIT_FAILURE);
			}
		}
	}
	cfsetospeed(&fd_attr,baud);
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
	case 500000: baud=B500000;break;
	case 576000: baud=B576000;break;  
	case 921600: baud=B921600;break;  
	case 1000000: baud=B1000000;break; 
	case 1152000: baud=B1152000;break; 
	case 1500000: baud=B1500000;break; 
	case 2000000: baud=B2000000;break; 
	case 2500000: baud=B2500000;break; 
	case 3000000: baud=B3000000;break; 
	case 3500000: baud=B3500000;break; 
	case 4000000: baud=B4000000;break;
	default: 
		{
			if (customBaudPossible) {
				if (baudout != baudin) {
					fprintf(stderr,"non-POSIX baud rates only supported if input = output baud rate!\r\n");
					fprintf(stderr,"You set input baud rate to %d, output to %d\r\n",baudin,baudout);
					exit(EXIT_FAILURE);
				}
				// Setting serial-divisor only works fine for 38400 baud. 
				baud=B38400;
			} else {
				fprintf(stderr,"illegal baud rate %d\r\n",baudout);
				exit(EXIT_FAILURE);
			}
		}
	}
	cfsetispeed(&fd_attr,baud);
	if (tcsetattr(fd,TCSANOW,&fd_attr) == -1) {/* commit changes NOW */
		perror("Can't setattr.");
		close(fd);
		exit(1);
	}
	if (needCustomBaud == true) {
#if defined(__linux__)
		struct serial_struct ser;

		if (ioctl(fd, TIOCGSERIAL, &ser) < 0) {
			perror("Could not get serial struct.");
		}
		ser.custom_divisor = ser.baud_base / baudin;
		ser.flags &= ~ASYNC_SPD_MASK;
		ser.flags |= ASYNC_SPD_CUST;

		if (ioctl(fd, TIOCSSERIAL, &ser) < 0) {
			perror("Could not set serial struct.");
		}
#endif
	}
	int flags;
	flags=fcntl(fd,F_GETFL);
	flags &= ~O_NONBLOCK;
	if (fcntl(fd,F_SETFL,flags) != 0) {
		perror("can't go to blocking mode");
	}
	
	if (showstate) getstate(fd);
	ioctl(fd,TIOCMBIC,&clearlines);
	ioctl(fd,TIOCMBIS,&setlines);
	if ((clearlines || setlines) && showstate) {
		getstate(fd);
	}

	if (send_ctrl_c) { // just send a ctr-c and exit
		char ctrlc=3;
		write(fd,&ctrlc,1);
		close(fd);
		exit(0);
	}

	if (listenPort != 0) {
		if (doFork) {
			pid_t daemon_pid;
			daemon_pid = fork();
			if (daemon_pid == 0) { /* now we are the forked process */
				int ffd;
				ffd=open("/dev/null",O_RDONLY);
				dup2(ffd,0);
				ffd=open("/dev/null",O_WRONLY);
				dup2(ffd,1);
				ffd=open("/dev/null",O_WRONLY);
				dup2(ffd,2);
				setsid();       // detach from old session;
			}	else if (daemon_pid != -1) { /* now we are still the old process */
				exit(0); 
			}	else { /* dirt! the fork failed! */
				perror("could not fork daemon");
				exit(1);
			}
		}


		int waitfd = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddress.sin_port = htons(listenPort);
		int tmp=1;
		setsockopt(waitfd,SOL_SOCKET,SO_REUSEADDR,&tmp,sizeof(tmp));
	
		if (bind(waitfd,
		         (struct sockaddr *) &serverAddress,
		         sizeof(serverAddress)) < 0) {
			perror("can't bind listening socket");
			exit(1);
		}
		listen(waitfd, 1);
		struct sockaddr_in clientAddress;
		socklen_t clientAddressLength;
		clientAddressLength = sizeof(clientAddress);
		

		for (;;) {
			int connectSocket = accept(waitfd,
			                           (struct sockaddr *) &clientAddress,
			                           &clientAddressLength);
			if (connectSocket < 0) {
				if (errno==EINVAL) { // probably the exit handler closed the socket
					perror("accept socket vanished, stopping thread");
					exit(1);
				}
				perror("can't accept incoming socket");
				continue;
			}
			struct pollfd pollfds[2];
			pollfds[0].fd = fd;
			pollfds[0].events = POLLIN | POLLERR;
			pollfds[1].fd = connectSocket;
			pollfds[1].events = POLLIN;
			
			talkLoop(pollfds,2,connectSocket);

			close(connectSocket);
		}
 
	} else {
		struct pollfd pollfds[2];
		pollfds[0].fd = fd;
		pollfds[0].events = POLLIN | POLLERR;
		pollfds[1].fd = 0;
		pollfds[1].events = POLLIN;
		
		talkLoop(pollfds,2,1);
	}
	close(fd);
}
