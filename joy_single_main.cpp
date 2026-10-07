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
#include "main.h"


#ifndef BUILD_AF_UNIX
#define BUILD_AF_UNIX
#endif

#define JOY_DEV "/dev/input/js0"

using namespace std;


#if defined(BUILD_AF_UNIX)
void print_dev_info(void);
#endif

void print_dev_info(void);

bool b_abort = false;

void sig(int s);
int send_ev( int joy_fd, js_event jsev, vector<char>& joy_button, vector<int>& joy_axis);
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

	while(!b_abort) {
		t = get_timestamp();
		if (t-prev_t > 10 * 1e-3) {
			prev_t = t;
		}
	}
	return nullptr;
}


int main(int argc, char** argv)
{

	pthread_mutex_init(&g_mutex_recvdata, NULL);
	pthread_cond_init(&g_cond_recvdata, NULL);
	signal(SIGINT, sig);


	/* spnav_wait_event() and spnav_poll_event(), will silently ignore any non-spnav X11 events.
	 *
	 * If you need to handle other X11 events you will have to use a regular XNextEvent() loop,
	 * and pass any ClientMessage events to spnav_x11_event, which will return the event type or
	 * zero if it's not an spnav event (see spnav.h).
	 */

  int joy_fd = -1;
  int num_of_axis = 0;
  int num_of_buttons = 0;

  char name_of_joystick[80] = {0,}; 
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


	while(!b_abort)
	{
		js_event jsev;
		while (read (joy_fd, &jsev, sizeof(jsev)) > 0) {
			if (bSendCan) {
				send_ev(joy_fd, jsev, joy_button, joy_axis);
			}
		}
		/* EAGAIN is returned when the queue is empty */
		if (errno != EAGAIN) {
				/* error */
		}
		/* do something interesting with processed events */
	}

	close(joy_fd);
	return EXIT_SUCCESS;
}


int send_ev( int joy_fd, js_event jsev, vector<char>& joy_button, vector<int>& joy_axis)
{
	int ev_count = 0;

	static int j = 0;
	int ret;
	int fd_can, nbytes;
	struct sockaddr_can addr;
	struct ifreq ifr;

	fd_can = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (fd_can < 0) {
		perror("socket PF_CAN failed");
		return 1;
	}
	
	//2.Specify can0 device
	strcpy(ifr.ifr_name, "can0");
	ret = ioctl(fd_can, SIOCGIFINDEX, &ifr);
	if (ret < 0) {
		perror("ioctl failed");
		return 1;
	}

	//3.Bind the socket to can0
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	ret = bind(fd_can, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		perror("bind failed");
		return 1;
	}
	//4.Disable filtering rules, do not receive packets, only send
	setsockopt(fd_can, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);


				// process_event (jsev);

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

			cout<< jsev.time << ": ";

			const int axis_unit = 1000;
			cout<<"axis/" << axis_unit << ": ";
			for(size_t i(0);i<joy_axis.size();++i)
				cout<<" "<<setw(2)<<joy_axis[i]/axis_unit;

			cout<<"  button: ";
			for(size_t i(0);i<joy_button.size();++i)
				cout<<" "<<(int)joy_button[i];
			cout<<endl;

			ev_count++;

			struct can_frame frame;
			memset(&frame, 0, sizeof(struct can_frame));

			// printf("[%u] got motion event: t(%d, %d, %d) ", ev_count, sev.motion.x, sev.motion.y, sev.motion.z);
			// printf("r(%d, %d, %d)\n", sev.motion.rx, sev.motion.ry, sev.motion.rz);
			//5.Set send data
			frame.can_id = 0x111;
			frame.can_dlc = 8;
			frame.data[0] = joy_axis[0] & 0xff;
			frame.data[1] = joy_axis[1] & 0xff;
			frame.data[2] = joy_axis[2] & 0xff;
			frame.data[3] = joy_axis[3] & 0xff;
			// frame.data[4] = sev.motion.ry & 0xff;
			// frame.data[5] = (j >> 16) & 0xff;
			// frame.data[6] = (j >> 8) & 0xff;
			// frame.data[7] = j & 0xff;

			int nTmp[8] = {0};
			int indexTmp = 0;
			nTmp[indexTmp++] = frame.can_id;
			nTmp[indexTmp++] = joy_axis[0] & 0xff;
			nTmp[indexTmp++] = joy_axis[1] & 0xff;
			nTmp[indexTmp++] = joy_axis[2] & 0xff;
			nTmp[indexTmp++] = joy_axis[3] & 0xff;
			// nTmp[indexTmp++] = jsev.motion.ry;
			// nTmp[indexTmp++] = jsev.motion.rz;

			// KW dec_dump( j, nTmp, sizeof(nTmp)/sizeof(nTmp[0]), true );

			//6.Send message
			nbytes = write(fd_can, &frame, sizeof(frame)); 

			if(nbytes != sizeof(frame)) {
				printf("Send Error frame[0]: %d - %d!\r\n",nbytes, (int)sizeof(frame) );
				close (fd_can);
					system("sudo ifconfig can0 down");
				sleep(1);

				char chbuf[128] = {0};
				sprintf(chbuf,"sudo ip link set can0 type can bitrate %lu restart-ms 100", CAN_BITRATE );
				system(chbuf);

				sprintf(chbuf,"sudo ifconfig can0 txqueuelen %lu", CAN_TXQUEUELEN );
				system(chbuf);

				system("sudo ip link set can0 up");
			}
			else {
				usleep(1000);
				//Close the socket and can0
				close(fd_can);
			}
			j++;
			ev_count++;

	return 0;
}



#if defined(BUILD_AF_UNIX)
void print_dev_info(void)
{
	// int proto;
	// char buf[256];

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
