#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <spnav.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
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

#ifndef BUILD_AF_UNIX
#define BUILD_AF_UNIX
#endif

void print_dev_info(void);

bool b_abort = false;
spnav_event sev;

void sigterm(int s);
int send_ev(spnav_event& sev);
void hex_dump(const unsigned char* p, int  size, bool with_address);
void dec_dump(int counter, const int* p, int  size, bool with_address);
bool bSendCan = true;

pthread_mutex_t g_mutex_recvdata;
pthread_cond_t g_cond_recvdata;

/// @brief 
/// @param  
/// @return 
double get_timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double) ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}



/// @brief 
/// @return 
int init_can()
{
	char chbuf[128] = {0};
	sprintf(chbuf,"sudo ip link set can0 type can bitrate %lu restart-ms 100", CAN_BITRATE );
	system(chbuf);

	sprintf(chbuf,"sudo ifconfig can0 txqueuelen %lu", CAN_TXQUEUELEN );
	system(chbuf);

				system("sudo ip link set can0 up");

	static int j = 0;
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

		return s;
}

/// @brief 
void fin_can(int s)
{
	if (s) {
		close(s);
	}
	system("sudo ifconfig can0 down");
}


/// @brief 
/// @param sev 
/// @return 
bool send_ev0(int s, spnav_event& sev)
{
	bool result = true;
	int nbytes;

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
		frame.data[5] = 0;	//(j >> 16) & 0xff;
		frame.data[6] = 0;	//(j >> 8) & 0xff;
		frame.data[7] = 0;	//j & 0xff;
	} else if(sev.type == SPNAV_EVENT_BUTTON ) {
		//5.Set send data
		frame.can_id = 0x222 + 0x111 * sev.button.bnum;
		frame.can_dlc = 1;
		frame.data[0] = sev.button.bnum;
		frame.data[1] = sev.button.press ? 1 : 0;
		frame.data[2] = 0;
		frame.data[3] = 0;
		frame.data[4] = 0;	//(j >> 24) & 0xff;
		frame.data[5] = 0;	//(j >> 16) & 0xff;
		frame.data[6] = 0;	//(j >> 8) & 0xff;
		frame.data[7] = 0;	//j & 0xff;
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

	return nbytes == sizeof(frame);
}

/// @brief 
/// @param user_param 
/// @return 

void* pump_thread(void* user_param)
{
	double prev_t = get_timestamp();
	double now_t = now_t;

	fin_can(0);
	int sock_can = init_can();
	double interval = 1 * 1e-3;

	int loop_count = 0;
	int error_count = 0;
	while (!b_abort)
	{
		now_t = get_timestamp();
		loop_count++;
		if (now_t - prev_t > interval) {
			prev_t = now_t;
			if (send_ev0(sock_can, sev)) {
				usleep(1000);
			}
			else {
				error_count++;
				fin_can(sock_can);
				usleep(1000);
				sock_can = init_can();
				printf("%f: (%lu) %.2f\n", 
					now_t, 
					loop_count, 
					(float)error_count/(float)loop_count*(float)100.0
					);
			}
		}
	}
	fin_can(sock_can);
	printf("Term: PUMP\n");
	return nullptr;
}

/// @brief 
/// @param user_param 
/// @return 

void* spnav_thread(void* user_param)
{
	if(spnav_open()==-1) {
		fprintf(stderr, "failed to connect to spacenavd\n");
		return nullptr;
	}

	if (spnav_evmask(SPNAV_EVMASK_MOTION|SPNAV_EVMASK_BUTTON)==-1) {
		return nullptr;
	}

	int ev_count = 0;
	spnav_event last_sev;
	while(!b_abort) {
		if (spnav_wait_event(&sev)) {
			if (sev.type != last_sev.type) {
				last_sev.type = sev.type;
				printf("Change[%d]\n", sev.type);
			}
			ev_count++;
		}
	}
	spnav_close();

	printf("Term: SPNAV\n");
	return nullptr;
}


/// @brief 
/// @param argc 
/// @param argv 
/// @return 
int main(int argc, char** argv)
{
	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	pthread_mutex_init(&g_mutex_recvdata, NULL);
	pthread_cond_init(&g_cond_recvdata, NULL);

	pthread_t tid_pump;
	pthread_create(&tid_pump, NULL, pump_thread, NULL);

	pthread_t tid_spnav;
	pthread_create(&tid_spnav, NULL, spnav_thread, NULL);

	pthread_join(tid_pump, NULL);
	pthread_join(tid_spnav, NULL);

	return EXIT_SUCCESS;
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

/// @brief 
/// @param s 
void sigterm(int s)
{
	b_abort = true;
	printf("*************\n");
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

