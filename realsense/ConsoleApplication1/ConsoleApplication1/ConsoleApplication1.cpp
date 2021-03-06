// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2017 Intel Corporation. All Rights Reserved.

#include "stdafx.h"
#define	LINUX 01
#define	WINDOWS 02
#define OS_SYS WINDOWS


#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <librealsense2/rs_advanced_mode.hpp>
#include "example.hpp"          // Include short list of convenience functions for rendering
#include <signal.h>
#include <iomanip>
#include <stdio.h>

#define RSDEVICE RS2_PRODUCT_LINE_SR300

#if OS_SYS == LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#elif OS_SYS == WINDOWS
#include <windows.h>
#include <winsock.h>
#pragma comment(lib,"ws2_32.lib") //Winsock Library
#endif

#include <string.h>
#include <errno.h>

#if OS_SYS == LINUX
#include <unistd.h>
#endif

#include <stdlib.h>
#include <iostream>


/*** Define mutex ****/
#define MUTEX_TYPE             HANDLE
#define MUTEX_INITIALIZER      NULL
#define MUTEX_SETUP(x)         (x) = CreateMutex(NULL, FALSE, NULL)
#define MUTEX_CLEANUP(x)       (CloseHandle(x) == 0)
#define MUTEX_LOCK(x)          emulate_pthread_mutex_lock(&(x))
#define MUTEX_UNLOCK(x)        (ReleaseMutex(x) == 0)

int emulate_pthread_mutex_lock(volatile MUTEX_TYPE *mx)
{
	if (*mx == NULL) /* static initializer? */
	{
		HANDLE p = CreateMutex(NULL, FALSE, NULL);
		if (InterlockedCompareExchangePointer((PVOID*)mx, (PVOID)p, NULL) != NULL)
			CloseHandle(p);
	}
	return WaitForSingleObject(*mx, INFINITE) == WAIT_FAILED;
}

//------------------- TCP Server Code -------------------
//-------------------------------------------------------

typedef void * (*THREADFUNCPTR)(void *);

class Server {

public:
	Server(int port);
#if OS_SYS == WINDOWS
	~Server();
	static DWORD WINAPI listener_thread(LPVOID p)
	{

		return ((Server*)p)->S_listener_thread();

	}
	DWORD S_listener_thread();
#elif OS_SYS == LINUX
	void * listener_thread();
#endif
	void init_listener_thread();
	void update_buffer(const unsigned char * data, int offset, unsigned long numbytes);

private:
#if OS_SYS == WINDOWS
	SOCKET init_sock, conn_sock;
	char * send_buffer;
	int buffer_size = 1024;
	char receive_buffer[1024];
	struct sockaddr_in serv_addr;
	struct sockaddr serv_storage;
	int addr_size;
	MUTEX_TYPE buffer_access_mutex;
	DWORD listener_thread_id;
	unsigned long frame_size;
#elif OS_SYS == LINUX
	int init_sock, conn_sock;
	char * send_buffer;
	int buffer_size = 1024;
	char receive_buffer[1024];
	struct sockaddr_in serv_addr;
	struct sockaddr_storage serv_storage;
	socklen_t addr_size;
	pthread_mutex_t buffer_access_mutex;
	pthread_t listener_thread_id;
	unsigned long frame_size;
#endif
};

Server::Server(int port) {
	init_sock = socket(PF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	memset(serv_addr.sin_zero, '\0', sizeof(serv_addr.sin_zero));
	bind(init_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
	send_buffer = new char[buffer_size];
}

#if OS_SYS == WINDOWS
Server::~Server() {
	MUTEX_CLEANUP(buffer_access_mutex);
}
void Server::init_listener_thread() {
	//LPTHREAD_START_ROUTINE
	CreateThread(NULL, 0, this->listener_thread, this, 0, &listener_thread_id);
	//pthread_mutex_init(&buffer_access_mutex, NULL);
}
#elif OS_SYS == LINUX
void Server::init_listener_thread() {
	pthread_create(&listener_thread_id, NULL, (THREADFUNCPTR)&Server::listener_thread, this);
	pthread_mutex_init(&buffer_access_mutex, NULL);
}
#endif
#if OS_SYS == WINDOWS
DWORD Server::S_listener_thread() {
	while (true) {
		if (listen(init_sock, 5) == 0)
			printf("Listening...\n");
		else
			printf("Error.\n");

		// Creates new socket for incoming connection
		addr_size = sizeof(serv_storage);
		conn_sock = accept(init_sock, (struct sockaddr *) &serv_storage, &addr_size);
		printf("Connected to client.\n");

		while (true) {

			// Parse ping from client
			memset(receive_buffer, 0, sizeof(receive_buffer));
			int resp_msg_size = recv(conn_sock, receive_buffer, 64, 0);
			if (resp_msg_size <= 0) break;

			// Send buffer data
			MUTEX_LOCK(buffer_access_mutex);
			int msg_size = send(conn_sock, send_buffer, buffer_size, MSG_PARTIAL);
			if (msg_size == 0) printf("Warning: No data was sent to client.\n");
			int tmp = errno;
			if (msg_size < 0) printf("Errno %d\n", tmp);
			MUTEX_UNLOCK(buffer_access_mutex);
		}
	}
}
void Server::update_buffer(const unsigned char * data, int offset, unsigned long numbytes) {
	MUTEX_LOCK(buffer_access_mutex);

	// Update buffer size
	unsigned long new_buffer_size = numbytes + offset;
	if (new_buffer_size > buffer_size) {
		delete[] send_buffer;
		buffer_size = new_buffer_size;
		send_buffer = new char[buffer_size];
	}

	// Copy data
	memcpy(send_buffer + offset, data, numbytes);
	MUTEX_UNLOCK(buffer_access_mutex);
}
#elif OS_SYS == LINUX
void * Server::listener_thread() {
	while (true) {
		if (listen(init_sock, 5) == 0)
			printf("Listening...\n");
		else
			printf("Error.\n");

		// Creates new socket for incoming connection
		addr_size = sizeof(serv_storage);
		conn_sock = accept(init_sock, (struct sockaddr *) &serv_storage, &addr_size);
		printf("Connected to client.\n");

		while (true) {

			// Parse ping from client
			memset(receive_buffer, 0, sizeof(receive_buffer));
			int resp_msg_size = recv(conn_sock, receive_buffer, 64, 0);
			if (resp_msg_size <= 0) break;

			// Send buffer data
			pthread_mutex_lock(&buffer_access_mutex);
			int msg_size = send(conn_sock, send_buffer, buffer_size, MSG_MORE);
			if (msg_size == 0) printf("Warning: No data was sent to client.\n");
			int tmp = errno;
			if (msg_size < 0) printf("Errno %d\n", tmp);
			pthread_mutex_unlock(&buffer_access_mutex);
		}
	}
}
void Server::update_buffer(const unsigned char * data, int offset, unsigned long numbytes) {
	pthread_mutex_lock(&buffer_access_mutex);

	// Update buffer size
	unsigned long new_buffer_size = numbytes + offset;
	if (new_buffer_size > buffer_size) {
		delete[] send_buffer;
		buffer_size = new_buffer_size;
		send_buffer = new char[buffer_size];
	}

	// Copy data
	memcpy(send_buffer + offset, data, numbytes);
	pthread_mutex_unlock(&buffer_access_mutex);
}

#endif
//-------------------------------------------------------
//-------------------------------------------------------
#if RSDEVICE == RS2_PRODUCT_LINE_SR400
// Configure all streams to run at 1280x720 resolution at 30 frames per second
const int stream_width = 1280;
const int stream_height = 720;
const int stream_fps = 30;
const int depth_disparity_shift = 50;
#elif RSDEVICE == RS2_PRODUCT_LINE_SR300
const int stream_width = 640;
const int stream_height = 480;
const int stream_fps = 30;
const int depth_disparity_shift = 50;
#endif

WSADATA wsaData;

// Capture color and depth video streams, render them to the screen, send them through TCP
int main(int argc, char * argv[]) try {

#if OS_SYS == WINDOWS
	int iResult;
	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}
#endif

	Server realsense_server(50000);
	realsense_server.init_listener_thread();

	// Create a simple OpenGL window for rendering:
	window app(2560, 720, "RealSense Stream");

	// Check if RealSense device is connected
	rs2::context ctx;
	rs2::device_list devices = ctx.query_devices();
	if (devices.size() == 0) {
		std::cerr << "No device connected, please connect a RealSense device" << std::endl;
		return EXIT_FAILURE;
	}

	// Configure streams
	rs2::config config_pipe;
	config_pipe.enable_stream(rs2_stream::RS2_STREAM_DEPTH, stream_width, stream_height, RS2_FORMAT_Z16, stream_fps);
	config_pipe.enable_stream(rs2_stream::RS2_STREAM_COLOR, stream_width, stream_height, RS2_FORMAT_RGB8, stream_fps);

	// Declare two textures on the GPU, one for color and one for depth
	texture depth_image, color_image;

	// Declare depth colorizer for pretty visualization of depth data
	rs2::colorizer color_map;

	// Start streaming
	rs2::pipeline pipe;
	pipe.start(config_pipe);

	// Print active device information
	rs2::pipeline_profile active_pipe_profile = pipe.get_active_profile();
	rs2::device dev = active_pipe_profile.get_device();
	std::cout << "Device information: " << std::endl;
	for (int i = 0; i < static_cast<int>(RS2_CAMERA_INFO_COUNT); i++) {
		rs2_camera_info info_type = static_cast<rs2_camera_info>(i);
		std::cout << "  " << std::left << std::setw(20) << info_type << " : ";
		if (dev.supports(info_type))
			std::cout << dev.get_info(info_type) << std::endl;
		else
			std::cout << "N/A" << std::endl;
	}

#if RSDEVICE == RS2_PRODUCT_LINE_SR400
	// Create advanced mode abstraction for RS400 device and set disparity shift
	rs400::advanced_mode advanced(dev);
	STDepthTableControl depth_table_control;
	if (advanced.is_enabled()) {
		depth_table_control = advanced.get_depth_table(); // Initialize depth table control group
		depth_table_control.depthUnits = 100; // Interpret RMS error at 100 micrometers
		depth_table_control.disparityShift = depth_disparity_shift; // Set disparity shift
		advanced.set_depth_table(depth_table_control);
	}
#endif

	// Get active device sensors
	std::vector<rs2::sensor> sensors = dev.query_sensors();
	rs2::sensor depth_sensor = sensors[0];
	rs2::sensor color_sensor = sensors[1];

	// Enable auto white balancing for color sensor
	rs2_option wb_option_type = static_cast<rs2_option>(11);
	if (color_sensor.supports(wb_option_type))
		color_sensor.set_option(wb_option_type, 1);

	// Capture 30 frames to give autoexposure, etc. a chance to settle
	for (int i = 0; i < 30; ++i) pipe.wait_for_frames();

	// Print camera intrinsics of color sensor
	rs2::video_stream_profile color_stream_profile = active_pipe_profile.get_stream(rs2_stream::RS2_STREAM_COLOR).as<rs2::video_stream_profile>();
	rs2_intrinsics color_intrinsics = color_stream_profile.get_intrinsics();
	float color_intrinsics_arr[9] = { color_intrinsics.fx, 0.0f, color_intrinsics.ppx,
		0.0f, color_intrinsics.fy, color_intrinsics.ppy,
		0.0f, 0.0f, 1.0f };

	// Get depth scale for converting depth pixel values into distances in meters
	float depth_scale = depth_sensor.as<rs2::depth_sensor>().get_depth_scale();

	// Create alignment object (for aligning depth frame to color frame)
	rs2::align align(rs2_stream::RS2_STREAM_COLOR);

	while (app) // Application still alive?
	{
		// Wait for next set of frames from the camera
		rs2::frameset data = pipe.wait_for_frames();
		rs2::frame color = data.get_color_frame();
		// rs2::depth_frame raw_depth = data.get_depth_frame();       

		// Get both raw and aligned depth frames
		auto processed = align.process(data);
		rs2::depth_frame aligned_depth = processed.get_depth_frame();

		// Find and colorize the depth data
		rs2::frame depth_colorized = color_map.colorize(aligned_depth);

		int depth_size = aligned_depth.get_width()*aligned_depth.get_height()*aligned_depth.get_bytes_per_pixel();
		realsense_server.update_buffer((unsigned char*)aligned_depth.get_data(), 10 * 4, depth_size);

		int color_size = data.get_color_frame().get_width()*data.get_color_frame().get_height()*data.get_color_frame().get_bytes_per_pixel();
		realsense_server.update_buffer((unsigned char*)color.get_data(), 10 * 4 + depth_size, color_size);

		// Send camera intrinsics and depth scale
		realsense_server.update_buffer((unsigned char*)color_intrinsics_arr, 0, 9 * 4);
		realsense_server.update_buffer((unsigned char*)&depth_scale, 9 * 4, 4);

		// Render depth on to the first half of the screen and color on to the second
		depth_image.render(depth_colorized, { 0, 0, app.width() / 2, app.height() });
		color_image.render(color, { app.width() / 2, 0, app.width() / 2, app.height() });
	}

	return EXIT_SUCCESS;
}
catch (const rs2::error & e)
{
	std::cerr << "RealSense error calling " << e.get_failed_function() << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
	return EXIT_FAILURE;
}
catch (const std::exception& e)
{
	std::cerr << e.what() << std::endl;
	return EXIT_FAILURE;
}
