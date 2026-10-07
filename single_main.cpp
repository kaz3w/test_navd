#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <spnav.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <time.h>
#include <pthread.h>
#include <spnav.h>
#include "main.h"

#undef	SEND_EV0


#if defined(BUILD_AF_UNIX)
void print_dev_info(void);
#endif

void sig(int s);
bool send_ev(int s, spnav_event& sev);
bool send_ev0(spnav_event &sev);
void hex_dump(const unsigned char* p, int  size, bool with_address);
void dec_dump(int counter, const int* p, int  size, bool with_address);
bool bSendCan = true;


int init_can()
{
	char chbuf[128] = {0};
	sprintf(chbuf,"sudo ip link set can0 type can bitrate %lu restart-ms 100", CAN_BITRATE );
	system(chbuf);

	sprintf(chbuf,"sudo ifconfig can0 txqueuelen %lu", CAN_TXQUEUELEN );
	system(chbuf);

	system("sudo ip link set can0 up");

#ifdef SEND_EV0
	return 0;
#else
	struct sockaddr_can addr;
	struct ifreq ifr;

	int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0)
	{
		perror("socket PF_CAN failed");
		// return 1;
		exit(EXIT_FAILURE);
	}

	//2.Specify can0 device
	strcpy(ifr.ifr_name, "can0");
	int ret = ioctl(s, SIOCGIFINDEX, &ifr);
	if (ret < 0) {
			perror("ioctl failed");
		exit(EXIT_FAILURE);
	}

	//3.Bind the socket to can0
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	ret = bind(s, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	//4.Disable filtering rules, do not receive packets, only send
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);
	return s;
#endif
}

void fin_can()
{
	system("sudo ifconfig can0 down");
}



int main(int argc, char** argv)
{
	int ev_count = 0;

	spnav_event sev;
	signal(SIGINT, sig);
	signal(SIGTERM, sig);
	signal(SIGHUP, sig);
	
	if(spnav_open()==-1) {
		fprintf(stderr, "failed to connect to spacenavd\n");
		return 1;
	}
	spnav_evmask(SPNAV_EVMASK_MOTION|SPNAV_EVMASK_BUTTON);
	// print_dev_info();


fin_can();
int s = init_can();

	/* spnav_wait_event() and spnav_poll_event(), will silently ignore any non-spnav X11 events.
	 *
	 * If you need to handle other X11 events you will have to use a regular XNextEvent() loop,
	 * and pass any ClientMessage events to spnav_x11_event, which will return the event type or
	 * zero if it's not an spnav event (see spnav.h).
	 */
	while(spnav_wait_event(&sev)) {
		if(sev.type == SPNAV_EVENT_MOTION) {
			if (bSendCan) {
#ifdef SEND_EV0
				send_ev0(sev);
#else
				if (!send_ev(s,sev)) {
					close(s);
					fin_can();
					s = init_can();
				}
#endif
			}
			else {
				printf("[%u] got motion event: t(%d, %d, %d) ", ev_count, sev.motion.x, sev.motion.y, sev.motion.z);
				printf("r(%d, %d, %d)\n", sev.motion.rx, sev.motion.ry, sev.motion.rz);
			}
		} else if(sev.type == SPNAV_EVENT_BUTTON ) {
			if (bSendCan) {
#ifdef SEND_EV0
				send_ev0(sev);
#else
				if (!send_ev(s,sev)) {
					close(s);
					fin_can();
					s = init_can();
				}
			// else {
			// 	printf("[%u] got button %s event b(%d)\n",ev_count,  sev.button.press ? "press" : "release", sev.button.bnum);
			// }
#endif
		}
		}
		ev_count++;
	}
	spnav_close();

	close(s);
	fin_can();

	return EXIT_SUCCESS;
}




bool send_ev(int s, spnav_event& sev)
{
	static int j = 0;
    bool ret;
    int nbytes = 0;

    struct can_frame frame;
    memset(&frame, 0, sizeof(struct can_frame));

	if(sev.type == SPNAV_EVENT_MOTION) {
		frame.can_id = 0x111;
		frame.can_dlc = 8;
		frame.data[0] = sev.motion.x & 0xff;
		frame.data[1] = sev.motion.y & 0xff;
		frame.data[2] = sev.motion.z & 0xff;
		frame.data[3] = sev.motion.rx & 0xff;
		frame.data[4] = sev.motion.ry & 0xff;
		frame.data[5] = (j >> 16) & 0xff;
		frame.data[6] = (j >> 8) & 0xff;
		frame.data[7] = j & 0xff;
	} else if(sev.type == SPNAV_EVENT_BUTTON ) {
		frame.can_id = 0x222 + 0x111 * sev.button.bnum;
		frame.can_dlc = 1;
		frame.data[0] = sev.button.bnum;
		frame.data[1] = sev.button.press ? 1 : 0;
		frame.data[2] = 0;
		frame.data[3] = 0;
		frame.data[4] = (j >> 24) & 0xff;
		frame.data[5] = (j >> 16) & 0xff;
		frame.data[6] = (j >> 8) & 0xff;
		frame.data[7] = j & 0xff;
	}

	int nTmp[8] = {0};
	int indexTmp = 0;
	nTmp[indexTmp++] = frame.can_id;
	nTmp[indexTmp++] = sev.motion.x;
	nTmp[indexTmp++] = sev.motion.y;
	nTmp[indexTmp++] = sev.motion.z;
	nTmp[indexTmp++] = sev.motion.rx;
	nTmp[indexTmp++] = sev.motion.ry;
	nTmp[indexTmp++] = sev.motion.rz;

	dec_dump( j, nTmp, sizeof(nTmp)/sizeof(nTmp[0]), true );

	//6.Send message
	printf("write:\n");
	nbytes = write(s, &frame, sizeof(frame)); 

	if(nbytes != sizeof(frame)) {
		printf("NG [%d] [%d] nbytes!! [%d]\n", sev.type, j, nbytes);
		usleep(100000);
		ret = false;
	}
	else {
		printf("OK [%d] [%d] nbytes [%d]\n", sev.type, j, nbytes);
		// printf("[%d] OK [%d]\n", j, nbytes);
		ret = true;
		usleep(1000);
	}
	j++;
	return ret;
}


bool send_ev0(spnav_event& sev)
{
	fin_can();
	init_can();

	int ret;
	int s, nbytes;
	struct sockaddr_can addr;
	struct ifreq ifr;

	// 1.Create can0 RAW socket
	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0)
	{
		perror("socket PF_CAN failed");
		return 1;
    }
    
    //2.Specify can0 device
    strcpy(ifr.ifr_name, "can0");
    ret = ioctl(s, SIOCGIFINDEX, &ifr);
    if (ret < 0) {
        perror("ioctl failed");
        return 1;
    }

    //3.Bind the socket to can0
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    ret = bind(s, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        perror("bind failed");
        return 1;
    }
    //4.Disable filtering rules, do not receive packets, only send
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

    struct can_frame frame;
    memset(&frame, 0, sizeof(struct can_frame));

		if(sev.type == SPNAV_EVENT_MOTION) {
			// printf("[%u] got motion event: t(%d, %d, %d) ", ev_count, sev.motion.x, sev.motion.y, sev.motion.z);
			// printf("r(%d, %d, %d)\n", sev.motion.rx, sev.motion.ry, sev.motion.rz);
			//5.Set send data
			frame.can_id = 0x111;
			frame.can_dlc = 8;
			frame.data[0] = sev.motion.x & 0xff;
			frame.data[1] = sev.motion.y & 0xff;
			frame.data[2] = sev.motion.z & 0xff;
			frame.data[3] = sev.motion.rx & 0xff;
			frame.data[4] = sev.motion.ry & 0xff;
			frame.data[5] = 0;
			frame.data[6] = 0;
			frame.data[7] = 0;

		} else if(sev.type == SPNAV_EVENT_BUTTON ) {
			// printf("[%u] got button %s event b(%d)\n",ev_count,  sev.button.press ? "press" : "release", sev.button.bnum);
			//5.Set send data
			frame.can_id = 0x222 + 0x111 * sev.button.bnum;
			frame.can_dlc = 1;
			frame.data[0] = sev.button.bnum;
			frame.data[1] = sev.button.press ? 1 : 0;
			frame.data[2] = 0;
			frame.data[3] = 0;
			frame.data[4] = 0;
			frame.data[5] = 0;
			frame.data[6] = 0;
			frame.data[7] = 0;
		}

		int nTmp[8] = {0};
		int indexTmp = 0;
		nTmp[indexTmp++] = frame.can_id;
		nTmp[indexTmp++] = sev.motion.x;
		nTmp[indexTmp++] = sev.motion.y;
		nTmp[indexTmp++] = sev.motion.z;
		nTmp[indexTmp++] = sev.motion.rx;
		nTmp[indexTmp++] = sev.motion.ry;
		nTmp[indexTmp++] = sev.motion.rz;
		// dec_dump( j, nTmp, sizeof(nTmp)/sizeof(nTmp[0]), true );

    //6.Send message
    nbytes = write(s, &frame, sizeof(frame)); 
    if(nbytes != sizeof(frame)) {
      printf("Send Error frame[0]!\r\n");
			close (s);
			// fin_can();
		}
		else {
			usleep(1000);
			//Close the socket and can0
			close(s);
    }
	return true;
}




#if defined(BUILD_AF_UNIX)
void print_dev_info(void)
{
	int proto;
	char buf[256];

	if((proto = spnav_protocol()) == -1) {
		fprintf(stderr, "failed to query protocol version\n");
		return;
	}

	printf("spacenav AF_UNIX protocol version: %d\n", proto);

	spnav_client_name("simple example");

	if(proto >= 1) {
		spnav_dev_name(buf, sizeof(buf));
		printf("Device: %s\n", buf);
		spnav_dev_path(buf, sizeof(buf));
		printf("Path: %s\n", buf);
		printf("Buttons: %d\n", spnav_dev_buttons());
		printf("Axes: %d\n", spnav_dev_axes());
	}

	putchar('\n');
}

#endif

void sig(int s)
{
	spnav_close();
	exit(0);
}


/************************************************************/
/* 								*/
/************************************************************/
static void _do_dump_hex(unsigned char c) {
    if (c) {
        printf( "%s%02x%s ", DBG_COLOR_WHITE, c, DBG_EOF_COLOR );
    }
    else {
        printf( "%s%02x%s ", DBG_COLOR_GRAY, c, DBG_EOF_COLOR );
    }
}

/************************************************************/
/* 								*/
/************************************************************/
static void _do_dump_dechex(int v) {
    if (v) {
		switch(v) {
		case 0x111:
			printf( "%sRot.[%02x%02x]%s ", DBG_COLOR_CYAN, (v>>8)&0xff, v&0xff, DBG_EOF_COLOR );
			break;
		case 0x222:
			printf( "%sBtnL[%02x%02x]%s ", DBG_COLOR_YELLOW, (v>>8)&0xff, v&0xff, DBG_EOF_COLOR );
			break;
		case 0x333:
		default:
			printf( "%sBtnR[%02x%02x]%s ", DBG_COLOR_MAGENDA, (v>>8)&0xff, v&0xff, DBG_EOF_COLOR );
			break;
		}
    }
    else {
        printf( "%s0000%s ", DBG_COLOR_GRAY, DBG_EOF_COLOR );
    }
}


/************************************************************/
/* 								*/
/************************************************************/
static void _do_dump_dec(int index, int c) {
		switch(index) {
		default:
			if (c) {
				printf( "%s%4d%s ", DBG_COLOR_WHITE, c, DBG_EOF_COLOR );
			}
			else {
		        printf( "%s%4d%s ", DBG_COLOR_GRAY, c, DBG_EOF_COLOR );
			}
			break;
		case 1:
		case 2:
		case 3:
			if (c) {
				printf( "%s%4d%s ", DBG_COLOR_WHITE, c, DBG_EOF_COLOR );
			}
			else {
				printf( "%s%4d%s ", DBG_COLOR_GRAY, c, DBG_EOF_COLOR );
			}
			break;
		case 4:
		case 5:
		case 6:
			if (c) {
				printf( "%s%4d%s ", DBG_COLOR_GREEN, c, DBG_EOF_COLOR );
			}
			else {
				printf( "%s%4d%s ", DBG_COLOR_GRAY, c, DBG_EOF_COLOR );
			}
			break;
		}
}

/************************************************************/
/* 								*/
/************************************************************/
void dec_dump(int counter, const int* p, int  size, bool with_address)
{
    // pthread_mutex_lock(&mutex_test_common);

    int* d = (int *) p;
    int remain = size;
    int col_count = 16;

    while(remain>col_count ) {
        if (with_address) {
    		printf("%08X: ", counter);
        }

        for (int ii=0; ii<col_count; ii++) {
	        int v = *(d+ii);
			if (ii) {
	            _do_dump_dec(ii, v);
			}
			else {
	            _do_dump_dechex(v);
			}
        }
        printf("\n");
        remain -= col_count;
        d += col_count;
    }

    if (with_address) {
    	printf("%08X: ", counter);
    }
    for (int ii=0; ii<remain; ii++) {
		int v = *(d+ii);
		if (ii) {
			_do_dump_dec(ii, v);
		}
		else {
			_do_dump_dechex(v);
		}
    }
    printf("\n");

    // pthread_mutex_unlock(&mutex_test_common);
}


/************************************************************/
/* 								*/
/************************************************************/
static void _do_dump_char(unsigned char c) {    if (c) {
        printf( "%s%c%s", DBG_COLOR_WHITE,  c<0x20 || 0x7f<c ? '.': c, DBG_EOF_COLOR );
    }
    else {
        printf( "%s%c%s", DBG_COLOR_GRAY,  c<0x20 || 0x7f<c ? '.': c, DBG_EOF_COLOR );
    }
}


/************************************************************/
/* 								*/
/************************************************************/
void hex_dump(const unsigned char* p, int  size, bool with_address)
{
    // pthread_mutex_lock(&mutex_test_common);

    char* d = (char *) p;
    int remain = size;
    int col_count = 16;

    while(remain>col_count ) {
        if (with_address) {
    		printf("%04X: ", size-remain);
        }

        for (int ii=0; ii<col_count; ii++) {
	        unsigned char v = *((unsigned char *)d+ii);
            _do_dump_hex(v);
            // printf("%02X ", v);
        }
        printf("   ");
        for (int ii=0; ii<col_count; ii++) {
	        unsigned char v = *((unsigned char *)d+ii);
            _do_dump_char(v);
            // printf("%c", v<0x20 || 0x7f<v ? '.': v);
        }
        printf("\n");
        remain -= col_count;
        d += col_count;
    }

    if (with_address) {
    	printf("%04X: ", size-remain);
    }
    for (int ii=0; ii<remain; ii++) {
		unsigned char v = *((unsigned char *)d+ii);
        // printf("%02X ", v);
        _do_dump_hex(v);
    }
    for (int ii=0; ii<col_count-remain; ii++) {
        printf(".. ");
    }
    printf("    ");
    for (int ii=0; ii<remain; ii++) {
        unsigned char v = *((unsigned char *)d+ii);
        // printf("%c",v<0x20 || 0x7f<v ? '.': v);
        _do_dump_char(v);
    }
    for (int ii=0; ii<col_count-remain; ii++) {
        printf(".");
    }
    printf("\n");

    // pthread_mutex_unlock(&mutex_test_common);
}



/************************************************************/
/* 								*/
/************************************************************/

void simple_dump(char *pstr, int str_size)
{
    hex_dump((const unsigned char*) pstr, str_size, false);
}

