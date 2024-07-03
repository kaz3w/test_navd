// #include <stdlib.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/joystick.h>
#include <time.h>
#include <pthread.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdio>
#include <fcntl.h>
#include <iostream>

#ifndef BUILD_AF_UNIX
#define BUILD_AF_UNIX
#endif

#define JOY_DEV "/dev/input/js0"

using namespace std;


#if defined(BUILD_AF_UNIX)
void print_dev_info(void);
#endif

void print_dev_info(void);

bool b_sig = false;

void sig(int s);
int send_ev( int joy_fd, vector<char>& joy_button, vector<int>& joy_axis);
void hex_dump(const unsigned char* p, int  size, bool with_address);
void dec_dump(int counter, const int* p, int  size, bool with_address);
bool bSendCan = true;

pthread_mutex_t g_mutex_recvdata;
pthread_cond_t g_cond_recvdata;

double get_timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double) ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}



void* pump_thread(void* user_param)
{
	double prev_t = get_timestamp();
	double t = get_timestamp();

	while(!b_sig) {
		t = get_timestamp();
		if (t-prev_t > 10 * 1e-3) {
			prev_t = t;
		}
	}
	return nullptr;
}


int main(int argc, char** argv)
{
	int ev_count = 0;

	pthread_mutex_init(&g_mutex_recvdata, NULL);
	pthread_cond_init(&g_cond_recvdata, NULL);
	signal(SIGINT, sig);


	/* spnav_wait_event() and spnav_poll_event(), will silently ignore any non-spnav X11 events.
	 *
	 * If you need to handle other X11 events you will have to use a regular XNextEvent() loop,
	 * and pass any ClientMessage events to spnav_x11_event, which will return the event type or
	 * zero if it's not an spnav event (see spnav.h).
	 */
#if 1

  int joy_fd(-1), num_of_axis(0), num_of_buttons(0);

  char name_of_joystick[80];
  vector<char> joy_button;
  vector<int> joy_axis;

  if((joy_fd=open(JOY_DEV,O_RDONLY)) < 0)
  {
    cerr<<"Failed to open "<<JOY_DEV<<endl;
    return -1;
  }

  ioctl(joy_fd, JSIOCGAXES, &num_of_axis);
  ioctl(joy_fd, JSIOCGBUTTONS, &num_of_buttons);
  ioctl(joy_fd, JSIOCGNAME(80), &name_of_joystick);

  joy_button.resize(num_of_buttons,0);
  joy_axis.resize(num_of_axis,0);

  cout<<"Joystick: "<<name_of_joystick<<endl
    <<"  axis: "<<num_of_axis<<endl
    <<"  buttons: "<<num_of_buttons<<endl;

  fcntl(joy_fd, F_SETFL, O_NONBLOCK);   // using non-blocking mode

  send_ev(joy_fd, joy_button, joy_axis);

  close(joy_fd);

#else
	while(spnav_wait_event(&sev)) {
		if(sev.type == SPNAV_EVENT_MOTION) {
			if (bSendCan) {
				send_ev(sev);
			}
			else {
				printf("[%u] got motion event: t(%d, %d, %d) ", ev_count, sev.motion.x, sev.motion.y, sev.motion.z);
				printf("r(%d, %d, %d)\n", sev.motion.rx, sev.motion.ry, sev.motion.rz);
			}
		} else if(sev.type == SPNAV_EVENT_BUTTON ) {
			if (bSendCan) {
				send_ev(sev);
			}
			else {
				printf("[%u] got button %s event b(%d)\n",ev_count,  sev.button.press ? "press" : "release", sev.button.bnum);
			}
		}
		else {
			// TODO: ???
		}
		ev_count++;
	}
	spnav_close();
#endif

	return EXIT_SUCCESS;
}


int send_ev( int joy_fd, vector<char>& joy_button, vector<int>& joy_axis)
{
	static int j = 0;
    int ret;
    int s, nbytes;
    struct sockaddr_can addr;
    struct ifreq ifr;

    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
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


	while(true)
	{
		js_event jsev;
		read(joy_fd, &jsev, sizeof(js_event));

		switch (jsev.type & ~JS_EVENT_INIT)
		{
		case JS_EVENT_AXIS:
			if((int)jsev.number>=joy_axis.size())  {cerr<<"err:"<<(int)jsev.number<<endl;}
			joy_axis[(int)jsev.number]= jsev.value;
			break;

		case JS_EVENT_BUTTON:
			if((int)jsev.number>=joy_button.size())  {cerr<<"err:"<<(int)jsev.number<<endl; }
			joy_button[(int)jsev.number]= jsev.value;
			break;
		}

		const int axis_unit = 1000;
		cout<<"axis/" << axis_unit << ": ";
		for(size_t i(0);i<joy_axis.size();++i)
			cout<<" "<<setw(2)<<joy_axis[i]/axis_unit;

		cout<<"  button: ";
		for(size_t i(0);i<joy_button.size();++i)
			cout<<" "<<(int)joy_button[i];
		cout<<endl;
		usleep(100);
	}

    struct can_frame frame;
    memset(&frame, 0, sizeof(struct can_frame));

		// printf("[%u] got motion event: t(%d, %d, %d) ", ev_count, sev.motion.x, sev.motion.y, sev.motion.z);
		// printf("r(%d, %d, %d)\n", sev.motion.rx, sev.motion.ry, sev.motion.rz);
		//5.Set send data
		frame.can_id = 0x111;
		frame.can_dlc = 8;
		// frame.data[0] = jsev.x & 0xff;
		// frame.data[1] = jsev.y & 0xff;
		// frame.data[2] = jsev.buttons & 1)? 1 : 0;
		// frame.data[3] = sev.motion.rx & 0xff;
		// frame.data[4] = sev.motion.ry & 0xff;
		// frame.data[5] = (j >> 16) & 0xff;
		// frame.data[6] = (j >> 8) & 0xff;
		// frame.data[7] = j & 0xff;

	int nTmp[8] = {0};
	int indexTmp = 0;
	// nTmp[indexTmp++] = frame.can_id;
	// nTmp[indexTmp++] = sev.motion.x;
	// nTmp[indexTmp++] = sev.motion.y;
	// nTmp[indexTmp++] = sev.motion.z;
	// nTmp[indexTmp++] = sev.motion.rx;
	// nTmp[indexTmp++] = sev.motion.ry;
	// nTmp[indexTmp++] = sev.motion.rz;

	dec_dump( j, nTmp, sizeof(nTmp)/sizeof(nTmp[0]), true );

    //6.Send message
    nbytes = write(s, &frame, sizeof(frame)); 

    if(nbytes != sizeof(frame)) {
        printf("Send Error frame[0]!\r\n");
		close (s);
			system("sudo ifconfig can0 down");
		sleep(1);
			system("sudo ip link set can0 type can bitrate 500000");
			system("sudo ifconfig can0 up");
    }
    else {
		usleep(1000);

		//Close the socket and can0
		close(s);
    }
	j++;
	return 0;
}



#if defined(BUILD_AF_UNIX)
void print_dev_info(void)
{
	int proto;
	char buf[256];

	// if((proto = spnav_protocol()) == -1) {
	// 	fprintf(stderr, "failed to query protocol version\n");
	// 	return;
	// }

	// printf("spacenav AF_UNIX protocol version: %d\n", proto);

	// spnav_client_name("simple example");

	// if(proto >= 1) {
	// 	spnav_dev_name(buf, sizeof(buf));
	// 	printf("Device: %s\n", buf);
	// 	spnav_dev_path(buf, sizeof(buf));
	// 	printf("Path: %s\n", buf);
	// 	printf("Buttons: %d\n", spnav_dev_buttons());
	// 	printf("Axes: %d\n", spnav_dev_axes());
	// }

	// putchar('\n');
}

#endif

void sig(int s)
{
	exit(0);
}


#define DBG_COLOR_BLACK "\033[0;30m"
#define DBG_COLOR_GRAY "\033[1;30m"
#define DBG_COLOR_RED "\033[1;31m"
#define DBG_COLOR_GREEN "\033[0;32m"
#define DBG_COLOR_YELLOW "\033[0;33m"
#define DBG_COLOR_BLUE "\033[0;34m"
#define DBG_COLOR_MAGENDA "\033[0;35m"
#define DBG_COLOR_CYAN "\033[0;36m"
#define DBG_COLOR_WHITE "\033[1;37m"

#define DBG_COLOR_DK_WHITE "\033[37m"
#define DBG_EOF_COLOR   "\033[0;37m"


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