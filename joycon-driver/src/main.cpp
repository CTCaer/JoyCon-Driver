
#include <Windows.h>
#pragma comment(lib, "user32.lib")


#include <bitset>
#include <random>
#include <stdafx.h>
#include <string.h>
#include <chrono>
#include <iomanip>      // std::setprecision
#include <iostream>
#include <fstream>

#include <hidapi.h>



#include "public.h"
#include "vjoyinterface.h"

#include "packet.h"
#include "joycon.hpp"
#include "MouseController.hpp"
#include "tools.hpp"

// wxWidgets:
#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <cube.h>
#include <MyApp.h>

// glm:
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
//#include <glm/gtc/matrix_projection.hpp>
//#include <glm/gtc/matrix_transform.hpp>
//#include <glm/gtx/type_ptr.hpp>


#pragma warning(disable:4996)

#define JOYCON_VENDOR 0x057e

#define JOYCON_L_BT 0x2006
#define JOYCON_R_BT 0x2007

#define PRO_CONTROLLER 0x2009

#define JOYCON_CHARGING_GRIP 0x200e

#define SERIAL_LEN 18

#define PI 3.14159265359


//#define DEBUG_PRINT
//#define LED_TEST



// joycon_1 is R, joycon_2 is L
#define CONTROLLER_TYPE_BOTH 0x1
// joycon_1 is L, joycon_2 is R
#define CONTROLLER_TYPE_LONLY 0x2
// joycon_1 is R, joycon_2 is -1
#define CONTROLLER_TYPE_RONLY 0x3

#define L_OR_R(lr) (lr == 1 ? 'L' : (lr == 2 ? 'R' : '?'))

unsigned short product_ids[] = { JOYCON_L_BT, JOYCON_R_BT, PRO_CONTROLLER, JOYCON_CHARGING_GRIP };


float rand0t1() {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<> dis(0.0f, 1.0f);
	float rnd = dis(gen);
	return rnd;
}



std::vector<Joycon> joycons;

MouseController MC;

JOYSTICK_POSITION_V2 iReport; // The structure that holds the full position data
uint8_t global_count = 0;
unsigned char buf[65];
int res = 0;


struct Settings {

	// there appears to be a good amount of variance between JoyCons,
	// but they work well once you find the right offsets
	// these are the values that worked well for my JoyCons:
	int leftJoyConXOffset = 16000;
	int leftJoyConYOffset = 13000;

	int rightJoyConXOffset = 15000;
	int rightJoyConYOffset = 19000;

	// multipliers to go from the range (-128,128) to (-32768, 32768)
	// These shouldn't need to be changed,
	// but the option is there if you find the joysticks aren't reaching their max values, or hit the max too early
	float leftJoyConXMultiplier = 10.0f;
	float leftJoyConYMultiplier = 10.0f;
	float rightJoyConXMultiplier = 10.0f;
	float rightJoyConYMultiplier = 10.0f;

	// Enabling this combines both JoyCons to a single vJoy Device(#1)
	// when combineJoyCons == false:
	// JoyCon(L) is mapped to vJoy Device #1
	// JoyCon(R) is mapped to vJoy Device #2
	// when combineJoyCons == true:
	// JoyCon(L) and JoyCon(R) are mapped to vJoy Device #1
	bool combineJoyCons = false;

	bool reverseX = false;// reverses joystick x (both sticks)
	bool reverseY = false;// reverses joystick y (both sticks)

	// Automatically center sticks
	// works by getting joystick position at start
	// and assumes that to be (0,0), and uses that to calculate the offsets
	bool autoCenterSticks = false;

	bool usingGrip = false;
	bool usingBluetooth = true;
	bool disconnect = false;

	// enables motion controls
	bool enableGyro = false;

	// gyroscope (mouse) sensitivity:
	float gyroSensitivityX = 100.0f;
	float gyroSensitivityY = 100.0f;

	// enables 3D gyroscope visualizer
	bool gyroWindow = false;

	// plays a version of the mario theme by vibrating
	// the first JoyCon connected.
	bool marioTheme = false;

	// bool to restart the program
	bool restart = false;

} settings;


struct Tracker {
	int var1 = 0;
	int var2 = 0;
	int counter1 = 0;
	//float frequency = 500.0f;

	float low_freq = 200.0f;
	float high_freq = 500.0f;

	float relX = 0;
	float relY = 0;

	float anglex = 0;
	float angley = 0;
	float anglez = 0;
	//glm::qauternion q;
	glm::fquat quat = glm::angleAxis(0.0f, glm::vec3(1.0, 0.0, 0.0));

	float previousPitch = 0;
} tracker;





void found_joycon(struct hid_device_info *dev) {
	
	
	Joycon jc;

	if (dev->product_id == JOYCON_CHARGING_GRIP) {
		
		//if (dev->interface_number == 0) {
		if (dev->interface_number == 0 || dev->interface_number == -1) {
			jc.name = std::string("Joy-Con (R)");
			jc.left_right = 2;// right joycon
		} else if (dev->interface_number == 1) {
			jc.name = std::string("Joy-Con (L)");
			jc.left_right = 1;// left joycon
		}
	}

	if (dev->product_id == JOYCON_L_BT) {
		jc.name = std::string("Joy-Con (L)");
		jc.left_right = 1;// left joycon
	} else if (dev->product_id == JOYCON_R_BT) {
		jc.name = std::string("Joy-Con (R)");
		jc.left_right = 2;// right joycon
	}

	jc.serial = wcsdup(dev->serial_number);
	jc.buttons = 0;

	printf("Found joycon %c %i: %ls %s\n", L_OR_R(jc.left_right), joycons.size(), jc.serial, dev->path);
	//errno = 0;
	jc.handle = hid_open_path(dev->path);
	// set non-blocking:
	//hid_set_nonblocking(jc.handle, 1);


	if (jc.handle == nullptr) {
		printf("Could not open serial %ls: %s\n", jc.serial, strerror(errno));
		throw;
	}

	joycons.push_back(jc);
}

unsigned createMask(unsigned a, unsigned b) {
	unsigned r = 0;
	for (unsigned i = a; i <= b; i++)
		r |= 1 << i;

	return r;
}

inline int mk_even(int n) {
	return n - n % 2;
}

inline int mk_odd(int n) {
	return n - (n % 2 ? 0 : 1);
}

struct s_button_map {
	int bit;
	char *name;
};


struct s_button_map button_map[16] = {
    {0, "D"},   {1, "R"},   {2, "L"},   {3, "U"},    {4, "SL"},  {5, "SR"},
    {6, "?"},   {7, "?"},   {8, "-"},   {9, "+"},    {10, "LS"}, {11, "RS"},
    {12, "Ho"}, {13, "Sc"}, {14, "LR"}, {15, "ZLR"},
};

void print_buttons(Joycon *jc) {

	for (int i = 0; i < 16; i++) {
		if (jc->buttons & (1 << button_map[i].bit)) {
			printf("1");
		} else {
			printf("0");
		}
	}
	printf("\n");
}


void print_buttons2(Joycon *jc) {

	printf("Joycon %c (Unattached): ", L_OR_R(jc->left_right));
	
	for (int i = 0; i < 32; i++) {
		if (jc->buttons2[i]) {
			printf("1");
		} else {
			printf("0");
		}
		
	}
	printf("\n");
}

void print_stick2(Joycon *jc) {

	printf("Joycon %c (Unattached): ", L_OR_R(jc->left_right));

	printf("%d %d\n", jc->stick.horizontal, jc->stick.vertical);

	
}




const char *const dstick_names[9] = {"Up", "UR", "Ri", "DR", "Do", "DL", "Le", "UL", "Neu"};



void hex_dump(unsigned char *buf, int len) {
	for (int i = 0; i < len; i++) {
		printf("%02x ", buf[i]);
	}
	printf("\n");
}

void hex_dump2(unsigned char *buf, int len) {
	for (int i = 0; i < len; i++) {
		printf("%02x ", buf[i]);
	}
}

void hex_dump_0(unsigned char *buf, int len) {
	for (int i = 0; i < len; i++) {
		if (buf[i] != 0) {
			printf("%02x ", buf[i]);
		}
	}
}

void device_print(struct hid_device_info *dev) {
	printf("USB device info:\n  vid: 0x%04hX pid: 0x%04hX\n  path: %s\n  serial_number: %ls\n  interface_number: %d\n",
		dev->vendor_id, dev->product_id, dev->path, dev->serial_number, dev->interface_number);
	printf("  Manufacturer: %ls\n", dev->manufacturer_string);
	printf("  Product:      %ls\n\n", dev->product_string);
}



void print_dstick(Joycon *jc) {
	printf("%s\n", dstick_names[jc->dstick]);
}








void hid_dual_write(hid_device *handle_l, hid_device *handle_r, unsigned char *buf_l, unsigned char *buf_r, int len) {
	int res;

	if (handle_l && buf_l) {
		hid_set_nonblocking(handle_l, 1);
		res = hid_write(handle_l, buf_l, len);
		if (res < 0) {
			settings.disconnect = true;
			return;
		}

		hid_set_nonblocking(handle_l, 0);
	}

	if (handle_r && buf_r) {
		hid_set_nonblocking(handle_r, 1);
		res = hid_write(handle_r, buf_r, len);
		if (res < 0) {
			settings.disconnect = true;

			//throw;
			return;
		}

		hid_set_nonblocking(handle_r, 0);
	}
}



void handle_input(Joycon *jc, uint8_t *packet, int len) {



	
	// Upright: LDUR
	// Sideways: DRLU
	// bluetooth button pressed packet:
	if (packet[0] == 0x3F) {

		uint16_t old_buttons = jc->buttons;
		int8_t old_dstick = jc->dstick;

		jc->dstick = packet[3];
		// todo: get button states here aswell:
	}

	//printf("%02x\n", packet[0]);

	// input update packet:
	// 0x21 is just buttons, 0x30 includes gyro, 0x31 includes NFC (large packet size)
	if (packet[0] == 0x21 || packet[0] == 0x30 || packet[0] == 0x31) {
		
		// offset for usb or bluetooth data:
		int offset = settings.usingBluetooth ? 0 : 10;


		uint8_t *btn_data = packet + offset + 3;


		// get button states:
		{
			uint16_t states = 0;
			// Left JoyCon:
			if (jc->left_right == 1) {
				states = (btn_data[1] << 8) | (btn_data[2] & 0xFF);
			// Right JoyCon:
			} else if (jc->left_right == 2) {
				states = (btn_data[1] << 8) | (btn_data[0] & 0xFF);
			}

			jc->buttons = states;
		}

		// get stick data:
		uint8_t *stick_data = packet + offset;
		if (jc->left_right == 1) {
			stick_data += 6;
		} else if (jc->left_right == 2) {
			stick_data += 9;
		}

		

		// it appears that the X component of the stick data isn't just nibble reversed,
		// specifically, the second nibble of second byte is combined with the first nibble of the first byte
		// to get the correct X stick value:
		//uint8_t stick_horizontal = ((stick_data[1] & 0x0F) << 4) | ((stick_data[0] & 0xF0) >> 4);// horizontal axis is reversed / combined with byte 0
		//uint8_t stick_vertical = stick_data[2];

		//jc->stick.horizontal = -128 + (int)(unsigned int)stick_horizontal;
		//jc->stick.vertical = -128 + (int)(unsigned int)stick_vertical;


		uint16_t stick_horizontal = stick_data[0] | ((stick_data[1] & 0xF) << 8);
		uint16_t stick_vertical = (stick_data[1] >> 4) | (stick_data[2] << 4);
		jc->stick.horizontal = (int)(unsigned int)stick_horizontal;
		jc->stick.vertical = (int)(unsigned int)stick_vertical;

		jc->battery = (stick_data[1] & 0xF0) >> 4;

		//printf("Joycon battery: %d\n", jc->battery);


		uint8_t *gyro_data = nullptr;
		if (jc->left_right == 1) {
			gyro_data = packet + 13;// 13
		} else if (jc->left_right == 2) {
			gyro_data = packet + 13;// 13
		}


		// Accelerometer:
		// Accelerometer data is absolute
		//{
			// get Accelerometer X:
			uint16_t accelX = ((uint16_t)gyro_data[1] << 8) | gyro_data[2];
			jc->accel.x = (double)unsignedToSigned16(accelX);
			//jc->accel.x = accelX;

			// get Accelerometer Y:
			uint16_t accelY = ((uint16_t)gyro_data[3] << 8) | gyro_data[4];
			jc->accel.y = (double)unsignedToSigned16(accelY);
			//jc->accel.y = accelY;

			// get Accelerometer Z:
			uint16_t accelZ = ((uint16_t)gyro_data[5] << 8) | gyro_data[6];
			jc->accel.z = (double)unsignedToSigned16(accelZ);
			//jc->accel.z = accelZ;

			//jc->accel.x /= 257;
			//jc->accel.y /= 257;
			//jc->accel.z /= 257;
		//}



		// Gyroscope:
		// Gyroscope data is relative
		{
			// get relative roll:
			uint16_t relroll = ((uint16_t)gyro_data[7] << 8) | gyro_data[8];
			jc->gyro.relroll = (double)unsignedToSigned16(relroll);

			// get relative pitch:
			uint16_t relpitch = ((uint16_t)gyro_data[9] << 8) | gyro_data[10];
			jc->gyro.relpitch = (double)unsignedToSigned16(relpitch);

			// get relative yaw:
			uint16_t relyaw = ((uint16_t)gyro_data[11] << 8) | gyro_data[12];
			jc->gyro.relyaw = (double)unsignedToSigned16(relyaw);

			jc->gyro.rawrelyaw = relyaw;
			jc->gyro.rawrelpitch = relpitch;
			jc->gyro.rawrelroll = relroll;


			// to degrees/second;
			//jc->gyro.relroll = jc->gyro.relroll * 0.070f;
			//jc->gyro.relpitch = jc->gyro.relpitch * 0.070f;
			//jc->gyro.relyaw = jc->gyro.relyaw * 0.070f;
		}
		

		//hex_dump(gyro_data, 20);

		if (jc->left_right == 1) {
			//hex_dump(gyro_data, 20);
			//hex_dump(packet+12, 20);

			//printf("x: %f, y: %f, z: %f\n", tracker.anglex, tracker.angley, tracker.anglez);

			//printf("%d\n", jc->gyro.relyaw);
			//printf("%02x\n", jc->gyro.relroll);
			//printf("%04x\n", jc->gyro.relyaw);

			//printf("%04x\n", jc->gyro.relroll);

			//printf("%f\n", jc->gyro.relroll);

			//printf("%d\n", accelXA);

			/*printf("%d\n", jc->buttons);*/

			//printf("%.4f\n", jc->gyro.relpitch);

			//printf("%04x\n", accelX);
			//printf("%02x %02x\n", rollA, rollB);
		}
		
	}






	// handle button combos:
	{

		// press up, down, left, right, L, ZL to restart:
		if (jc->left_right == 1) {
			if (jc->buttons == 207) {
				settings.restart = true;
				//start();
				//accurateSleep(100.0);
				//goto init_start;
			}


			// remove this, it's just for rumble testing
			uint8_t hfa2 = 0x88;
			uint16_t lfa2 = 0x804d;

			tracker.low_freq = clamp(tracker.low_freq, 41.0f, 626.0f);
			tracker.high_freq = clamp(tracker.high_freq, 82.0f, 1252.0f);
			
			//// down:
			//if (jc->buttons == 1) {
			//	tracker.high_freq -= 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}
			//// down:
			//if (jc->buttons == 2) {
			//	tracker.high_freq += 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}

			//// left:
			//if (jc->buttons == 8) {
			//	tracker.low_freq -= 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}
			//// right:
			//if (jc->buttons == 4) {
			//	tracker.low_freq += 1;
			//	jc->rumble4(tracker.low_freq, tracker.high_freq, hfa2, lfa2);
			//}

			////printf("%i\n", jc->buttons);
			////printf("%f\n", tracker.frequency);
			//printf("%f %f\n", tracker.low_freq, tracker.high_freq);
		}

	}
}







void hid_dual_exchange(hid_device *handle_l, hid_device *handle_r, unsigned char *buf_l, unsigned char *buf_r, int len) {
	if (handle_l && buf_l) {
		hid_set_nonblocking(handle_l, 1);
		hid_write(handle_l, buf_l, len);
		hid_read(handle_l, buf_l, 65);
#ifdef DEBUG_PRINT
		hex_dump(buf_l, 0x40);
#endif
		hid_set_nonblocking(handle_l, 0);
	}

	if (handle_r && buf_r) {
		hid_set_nonblocking(handle_r, 1);
		hid_write(handle_r, buf_r, len);
		hid_read(handle_r, buf_r, 65);
#ifdef DEBUG_PRINT
		hex_dump(buf_r, 0x40);
#endif
		hid_set_nonblocking(handle_r, 0);
	}
}





int acquirevJoyDevice(int deviceID) {

	int stat;

	// Get the driver attributes (Vendor ID, Product ID, Version Number)
	if (!vJoyEnabled()) {
		printf("Function vJoyEnabled Failed - make sure that vJoy is installed and enabled\n");
		int dummy = getchar();
		stat = -2;
		throw;
		//goto Exit;
	} else {
		wprintf(L"Vendor: %s\nProduct :%s\nVersion Number:%s\n", static_cast<TCHAR *> (GetvJoyManufacturerString()), static_cast<TCHAR *>(GetvJoyProductString()), static_cast<TCHAR *>(GetvJoySerialNumberString()));
		wprintf(L"Product :%s\n", static_cast<TCHAR *>(GetvJoyProductString()));
	};

	// Get the status of the vJoy device before trying to acquire it
	VjdStat status = GetVJDStatus(deviceID);

	switch (status) {
		case VJD_STAT_OWN:
			printf("vJoy device %d is already owned by this feeder\n", deviceID);
			break;
		case VJD_STAT_FREE:
			printf("vJoy device %d is free\n", deviceID);
			break;
		case VJD_STAT_BUSY:
			printf("vJoy device %d is already owned by another feeder\nCannot continue\n", deviceID);
			return -3;
		case VJD_STAT_MISS:
			printf("vJoy device %d is not installed or disabled\nCannot continue\n", deviceID);
			return -4;
		default:
			printf("vJoy device %d general error\nCannot continue\n", deviceID);
			return -1;
	};

	// Acquire the vJoy device
	if (!AcquireVJD(deviceID)) {
		printf("Failed to acquire vJoy device number %d.\n", deviceID);
		int dummy = getchar();
		stat = -1;
		//goto Exit;
		throw;
	} else {
		printf("Acquired device number %d - OK\n", deviceID);
	}
}

void updatevJoyDevice(Joycon *jc) {

	// todo: calibration of some kind
	int leftJoyConXOffset = settings.leftJoyConXOffset;
	int leftJoyConYOffset = settings.leftJoyConYOffset;

	int rightJoyConXOffset = settings.rightJoyConXOffset;
	int rightJoyConYOffset = settings.rightJoyConYOffset;

	// multipliers, these shouldn't really be different from one another
	// but are there anyways
	// todo: add a logarithmic multiplier? it's already doable in x360ce though
	float leftJoyConXMultiplier = settings.leftJoyConXMultiplier;
	float leftJoyConYMultiplier = settings.leftJoyConYMultiplier;
	float rightJoyConXMultiplier = settings.rightJoyConXMultiplier;
	float rightJoyConYMultiplier = settings.rightJoyConYMultiplier;

	bool combineJoyCons = settings.combineJoyCons;

	bool reverseX = settings.reverseX;
	bool reverseY = settings.reverseY;





	UINT DevID;

	if (!combineJoyCons) {
		DevID = jc->left_right;
	} else {
		DevID = 1;
	}

	PVOID pPositionMessage;
	UINT	IoCode = LOAD_POSITIONS;
	UINT	IoSize = sizeof(JOYSTICK_POSITION);
	// HID_DEVICE_ATTRIBUTES attrib;
	BYTE id = 1;
	UINT iInterface = 1;

	// Set destination vJoy device
	id = (BYTE)DevID;
	iReport.bDevice = id;


	// Set Stick data
	
	int x, y, z;
	int rx = 16384;
	int ry = 16384;
	int rz = 0;

	if (!combineJoyCons) {
		if (jc->left_right == 1) {
			x = leftJoyConXMultiplier * (jc->stick.horizontal) + leftJoyConXOffset;
			y = leftJoyConYMultiplier * (jc->stick.vertical) + leftJoyConYOffset;
		} else if (jc->left_right == 2) {
			x = rightJoyConXMultiplier * (jc->stick.horizontal) + rightJoyConXOffset;
			y = rightJoyConYMultiplier * (jc->stick.vertical) + rightJoyConYOffset;
		}
	} else {
		if (jc->left_right == 1) {
			x = leftJoyConXMultiplier * (jc->stick.horizontal) + leftJoyConXOffset;
			y = leftJoyConYMultiplier * (jc->stick.vertical) + leftJoyConYOffset;
		} else if (jc->left_right == 2) {
			rx = rightJoyConXMultiplier * (jc->stick.horizontal) + rightJoyConXOffset;
			ry = rightJoyConYMultiplier * (jc->stick.vertical) + rightJoyConYOffset;
		}
	}

	if (reverseX) {
		x = 32768 - x;
	}
	if (reverseY) {
		y = 32768 - y;
	}

	// both left and right joycons
	if (!combineJoyCons) {
		iReport.wAxisX = x;
		iReport.wAxisY = y;
		iReport.wAxisXRot = rx;
		iReport.wAxisYRot = ry;
	} else {

		// Set position data
		if (jc->left_right == 1) {
			iReport.wAxisX = x;
			iReport.wAxisY = y;
		}

		if (jc->left_right == 2) {
			iReport.wAxisXRot = rx;
			iReport.wAxisYRot = ry;
		}
	}



	// gyro data:
	//if (settings.enableGyro) {
	if ((jc->left_right == 2) || (joycons.size() == 1 && jc->left_right == 1)) {
		//if (jc->left_right == 2) {
			//rz = jc->gyro.roll*240;
			//iReport.wAxisZRot = jc->gyro.roll * 120;
			//iReport.wSlider = jc->gyro.pitch * 120;

		int multiplier;


		// Gyroscope (roll, pitch, yaw):
		multiplier = 1000;


		// gyro:
		{
			//float coeff = 0.001;//54000;//54000.0;// 1000.0
			//float dx = -jc->gyro.relpitch * coeff;
			//float dy = jc->gyro.relyaw * coeff;
			//float dz = -jc->gyro.relroll * coeff;

			//glm::fquat delx = glm::angleAxis(dx, glm::vec3(1.0, 0.0, 0.0));
			//glm::fquat dely = glm::angleAxis(dy, glm::vec3(0.0, 1.0, 0.0));
			//glm::fquat delz = glm::angleAxis(dz, glm::vec3(0.0, 0.0, 1.0));

			//float smallest = glm::radians(0.25f);// 0.25
			//if (abs(dx) > smallest) {
			//	tracker.quat = tracker.quat*delx;
			//}
			//if (abs(dy) > smallest) {
			//	tracker.quat = tracker.quat*dely;
			//}
			//if (abs(dz) > smallest) {
			//	tracker.quat = tracker.quat*delz;
			//}
		}



		////float gyro_cal_coeff = (float)(936.0 / (float)(13371 - unsignedToSigned16(16)));
		//float gyro_cal_coeff = 0.00007f;
		////float div2 = 100000;//54000.0;// 1000.0
		//float dx2 = -jc->gyro.relpitch * gyro_cal_coeff;
		//float dy2 = jc->gyro.relyaw * gyro_cal_coeff;
		//float dz2 = -jc->gyro.relroll * gyro_cal_coeff;
		//tracker.anglex += dx2;
		//tracker.angley += dy2;
		//tracker.anglez += dz2+0.01;




		// accelerometer
		{
			////float ax = jc->accel.x*0.00039f;// radians
			////float ax = jc->accel.x*0.0221f;// degrees
			////float ax = glm::radians(jc->accel.x*0.000244f);

			//float ax = glm::degrees((atan2(-jc->accel.x, -jc->accel.z) + PI));
			//float ay = glm::degrees((atan2(-jc->accel.y, -jc->accel.z) + PI));
			////float az = glm::degrees((atan2(-jc->accel.x, -jc->accel.z) + PI));

			//// set to 0:
			//tracker.quat = glm::angleAxis(0.0f, glm::vec3(1.0, 0.0, 0.0));

			//// x:
			//glm::fquat delx = glm::angleAxis(glm::radians(ax), glm::vec3(1.0, 0.0, 0.0));
			//tracker.quat = tracker.quat*delx;

			//// y:
			//glm::fquat dely = glm::angleAxis(glm::radians(-ay), glm::vec3(0.0, 0.0, 1.0));
			//tracker.quat = tracker.quat*dely;

			//// z:
			////glm::fquat delz = glm::angleAxis(glm::radians(az), glm::vec3(0.0, 0.0, 1.0));
			////tracker.quat = tracker.quat*delz;

			////printf("%f   %f\n", jc->accel.x*0.0221f, jc->accel.z*0.0221f);
			////printf("%f\n", ax);
			////printf("%f\n", ay);
		}




		// complementary filtered tracking
		// uses gyro + accelerometer

		// set to 0:
		tracker.quat = glm::angleAxis(0.0f, glm::vec3(1.0, 0.0, 0.0));

		float gyroCoeff = 0.001;


		// x:
		float pitchInDegreesAccel = glm::degrees((atan2(-jc->accel.x, -jc->accel.z) + PI));
		float relPitchDegreesGyro = -jc->gyro.relpitch * gyroCoeff;
		float pitch = 0;

		tracker.anglex += relPitchDegreesGyro;
		if ((pitchInDegreesAccel - tracker.anglex) > 180) {
			tracker.anglex += 360;
		} else if ((tracker.anglex - pitchInDegreesAccel) > 180) {
			tracker.anglex -= 360;
		}
		tracker.anglex = (tracker.anglex * 0.98) + (pitchInDegreesAccel * 0.02);
		pitch = tracker.anglex;

		glm::fquat delx = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0, 0.0, 0.0));
		tracker.quat = tracker.quat*delx;











		// y:
		float rollInDegreesAccel = -glm::degrees((atan2(-jc->accel.y, -jc->accel.z) + PI));
		float relRollDegreesGyro = -jc->gyro.relroll * gyroCoeff;
		float roll = 0;

		tracker.angley += relRollDegreesGyro;
		if ((rollInDegreesAccel - tracker.angley) > 180) {
			tracker.angley += 360;
		} else if ((tracker.angley - rollInDegreesAccel) > 180) {
			tracker.angley -= 360;
		}
		tracker.angley = (tracker.angley * 0.98) + (rollInDegreesAccel * 0.02);
		//tracker.angley = -rollInDegreesAccel;
		roll = tracker.angley;

		
		glm::fquat dely = glm::angleAxis(glm::radians(roll), glm::vec3(0.0, 0.0, 1.0));
		tracker.quat = tracker.quat*dely;

		//printf("%f\n", roll);








		// z:
		float yawInDegreesAccel = glm::degrees((atan2(-jc->accel.y, -jc->accel.x) + PI));
		float relyawDegreesGyro = -jc->gyro.relyaw * gyroCoeff;
		float yaw = 0;

		tracker.anglez += lowpassFilter(relyawDegreesGyro, 0.5);
		//if ((yawInDegreesAccel - tracker.anglez) > 180) {
		//	tracker.anglez += 360;
		//} else if ((tracker.anglez - yawInDegreesAccel) > 180) {
		//	tracker.anglez -= 360;
		//}
		//tracker.anglez = (tracker.anglez * 0.98) + (yawInDegreesAccel * 0.02);
		yaw = tracker.anglez;


		glm::fquat delz = glm::angleAxis(glm::radians(-yaw), glm::vec3(0.0, 1.0, 0.0));
		tracker.quat = tracker.quat*delz;






		//printf("%f\n", pitch);

		//printf("%f\n", tracker.anglez);
		//printf("%f\n", (float)unsignedToSigned16(jc->gyro.rawrelroll) * 0.07f);


		//glm::toQuat(glm::vec3(0.0, 0.0, 0.0));

		// move with absolute (tracked) gyro:
		// todo: add a reset button
		//MC.moveRel(jc->gyro.yaw, -jc->gyro.relpitch);
		//MC.moveRel(jc->gyro.yaw, -jc->gyro.pitch);

		//printf("%.5f\n", jc->gyro.pitch);

		float relX2 = -jc->gyro.relyaw / settings.gyroSensitivityX;
		float relY2 = jc->gyro.relpitch / settings.gyroSensitivityY;

		if (settings.enableGyro) {
			MC.moveRel2(relX2, relY2);
		}

		//multiplier = 200;

		//iReport.wAxisZRot = (jc->gyro.roll * multiplier);
		//iReport.wSlider = (jc->gyro.pitch * multiplier);
		//iReport.wDial = (jc->gyro.yaw * multiplier);

	}
	//}



	// Set button data
	// JoyCon(L) is the first 16 bits
	// JoyCon(R) is the last 16 bits

	long btns = 0;
	if (!combineJoyCons) {
		btns = jc->buttons;
	} else {

		if (jc->left_right == 1) {
			btns = ((iReport.lButtons >> 16) << 16) | (jc->buttons);

		} else if (jc->left_right == 2) {
			unsigned r = createMask(0, 15);// 15
			btns = ((jc->buttons) << 16) | (r & iReport.lButtons);

		}
	}

	iReport.lButtons = btns;

	// Send data to vJoy device
	pPositionMessage = (PVOID)(&iReport);
	if (!UpdateVJD(DevID, pPositionMessage)) {
		printf("Feeding vJoy device number %d failed - try to enable device then press enter\n", DevID);
		getchar();
		AcquireVJD(DevID);
	}
}


void parseSettings(int length, char *args[]) {
	for (int i = 0; i < length; ++i) {
		if (std::string(args[i]) == "--combine") {
			settings.combineJoyCons = true;
			printf("JoyCon combining enabled.\n");
		}
		if (std::string(args[i]) == "--LXO") {
			settings.leftJoyConXOffset = std::stoi(args[i + 1]);
		}
		if (std::string(args[i]) == "--LYO") {
			settings.leftJoyConYOffset = std::stoi(args[i + 1]);
		}
		if (std::string(args[i]) == "--RXO") {
			settings.rightJoyConXOffset = std::stoi(args[i + 1]);
		}
		if (std::string(args[i]) == "--RYO") {
			settings.rightJoyConYOffset = std::stoi(args[i + 1]);
		}

		if (std::string(args[i]) == "--REVX") {
			settings.reverseX = true;
		}
		if (std::string(args[i]) == "--REVY") {
			settings.reverseY = true;
		}
		if (std::string(args[i]) == "--auto-center") {
			settings.autoCenterSticks = true;
		}
		if (std::string(args[i]) == "--mario-theme") {
			settings.marioTheme = true;
		}
		if (std::string(args[i]) == "--enable-gyro") {
			settings.enableGyro = true;
		}
	}
}


void parseSettings2() {

	//setupConsole("Debug");

	std::map<std::string, std::string> cfg = LoadConfig("config.txt");

	settings.combineJoyCons = (bool)stoi(cfg["CombineJoyCons"]);
	settings.autoCenterSticks = (bool)stoi(cfg["AutoCenterSticks"]);
	settings.enableGyro = (bool)stoi(cfg["GyroControls"]);

	settings.gyroSensitivityX = stof(cfg["gyroSensitivityX"]);
	settings.gyroSensitivityY = stof(cfg["gyroSensitivityY"]);

	settings.gyroWindow = (bool)stoi(cfg["GyroWindow"]);
	settings.marioTheme = (bool)stoi(cfg["MarioTheme"]);

	settings.reverseX = (bool)stoi(cfg["reverseX"]);
	settings.reverseY = (bool)stoi(cfg["reverseY"]);


	settings.leftJoyConXOffset = stof(cfg["leftJoyConXOffset"]);
	settings.leftJoyConYOffset = stof(cfg["leftJoyConYOffset"]);
	settings.rightJoyConXOffset = stof(cfg["rightJoyConXOffset"]);
	settings.rightJoyConYOffset = stof(cfg["rightJoyConYOffset"]);

	settings.leftJoyConXMultiplier = stof(cfg["leftJoyConXMultiplier"]);
	settings.leftJoyConYMultiplier = stof(cfg["leftJoyConYMultiplier"]);
	settings.rightJoyConXMultiplier = stof(cfg["rightJoyConXMultiplier"]);
	settings.rightJoyConYMultiplier = stof(cfg["rightJoyConYMultiplier"]);

}

void pollLoop() {

	// poll joycons:
	for (int i = 0; i < joycons.size(); ++i) {
		Joycon *jc = &joycons[i];

		if (!jc->handle) { continue; }

		// set to be non-blocking:

		//hid_set_nonblocking(jc->handle, 1);

		// set to be blocking:
		hid_set_nonblocking(jc->handle, 0);

		// get input:
		memset(buf, 0, 65);
		//if (settings.enableGyro) {
		//	// seems to have slower response time:
		//	jc->send_command(0x1F, buf, 0);
		//} else {
		//	// may reset MCU data, not sure:
		//	jc->send_command(0x01, buf, 0);
		//}
		//jc->send_command(0x1F, buf, 0);

		jc->send_command(0x1E, buf, 0);
		handle_input(jc, buf, 0x40);
	}

	// update vjoy:
	for (int i = 0; i < joycons.size(); ++i) {
		updatevJoyDevice(&joycons[i]);
	}


	// sleep:
	accurateSleep(2.00);// 8.00

	if (settings.restart) {
		settings.restart = false;
		//goto init_start;
		//start();
	}
}



void start() {


	// get vJoy Device 1
	acquirevJoyDevice(1);
	// get vJoy Device 2
	acquirevJoyDevice(2);

	int missedPollCount = 0;
	int res;
	int i;
	unsigned char buf[65];
	unsigned char buf2[65];

	int read;	// number of bytes read
	int written;// number of bytes written
	const char *device_name;

	// Enumerate and print the HID devices on the system
	struct hid_device_info *devs, *cur_dev;

	res = hid_init();


init_start:

	devs = hid_enumerate(JOYCON_VENDOR, 0x0);
	cur_dev = devs;
	while (cur_dev) {
		// identify by vendor:
		if (cur_dev->vendor_id == JOYCON_VENDOR) {

			//device_print(cur_dev);
			Joycon jc;

			// bluetooth, left / right joycon:
			if (cur_dev->product_id == JOYCON_L_BT || cur_dev->product_id == JOYCON_R_BT) {
				found_joycon(cur_dev);
				settings.usingBluetooth = true;
			}

			// pro controller:
			// (probably won't work right, I'm just putting this here so it detects it,
			// I don't have a pro controller to test with.)
			if (cur_dev->product_id == PRO_CONTROLLER) {
				found_joycon(cur_dev);
			}

			// charging grip:
			//if (cur_dev->product_id == JOYCON_CHARGING_GRIP) {
			//	settings.usingGrip = true;
			//	settings.usingBluetooth = false;
			//	settings.combineJoyCons = true;
			//	found_joycon(cur_dev);
			//}
		}


		cur_dev = cur_dev->next;
	}
	hid_free_enumeration(devs);



	// init joycons:
	if (settings.usingGrip) {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].init_usb();
		}
	} else {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].init_bt();
		}
	}

	// use stick data to calibrate:
	if (settings.autoCenterSticks) {
		printf("Auto centering sticks...\n");
		// do a few polls to get stick data:
		for (int j = 0; j < 10; ++j) {
			pollLoop();
		}

		for (int i = 0; i < joycons.size(); ++i) {
			Joycon *jc = &joycons[i];
			// left joycon:
			if (jc->left_right == 1) {
				int x = (settings.leftJoyConXMultiplier * (jc->stick.horizontal));
				settings.leftJoyConXOffset = -x + (16384);
				int y = (settings.leftJoyConYMultiplier * (jc->stick.vertical));
				settings.leftJoyConYOffset = -y + (16384);

			// right joycon:
			} else if (jc->left_right == 2) {
				int x = (settings.rightJoyConXMultiplier * (jc->stick.horizontal));
				settings.rightJoyConXOffset = -x + (16384);
				int y = (settings.rightJoyConYMultiplier * (jc->stick.vertical));
				settings.rightJoyConYOffset = -y + (16384);
			}
		}
		//printf("Done centering sticks.\n");
	}


	pollLoop();
	for (int i = 0; i < joycons.size(); ++i) {
		printf("battery level: %u\n", joycons[i].battery);
	}
	


	// set lights:
	printf("setting LEDs...\n");
	for (int r = 0; r < 5; ++r) {
		for (int i = 0; i < joycons.size(); ++i) {
			Joycon *jc = &joycons[i];
			// Player LED Enable
			memset(buf, 0x00, 0x40);
			if (i == 0) {
				buf[0] = 0x0 | 0x0 | 0x0 | 0x1;		// solid 1
			}
			if (i == 1) {
				if (settings.combineJoyCons) {
					buf[0] = 0x0 | 0x0 | 0x0 | 0x1; // solid 1
				} else if (!settings.combineJoyCons) {
					buf[0] = 0x0 | 0x0 | 0x2 | 0x0; // solid 2
				}
			}
			//buf[0] = 0x80 | 0x40 | 0x2 | 0x1; // Flash top two, solid bottom two
			//buf[0] = 0x8 | 0x4 | 0x2 | 0x1; // All solid
			//buf[0] = 0x80 | 0x40 | 0x20 | 0x10; // All flashing
			//buf[0] = 0x80 | 0x00 | 0x20 | 0x10; // All flashing except 3rd light (off)
			jc->send_subcommand(0x01, 0x30, buf, 1);
		}
	}


	// give a small rumble to all joycons:
	printf("vibrating JoyCon(s).\n");
	//for (int k = 0; k < 1; ++k) {
	//	for (int i = 0; i < joycons.size(); ++i) {
	//		joycons[i].rumble(100, 1);
	//		Sleep(20);
	//		joycons[i].rumble(10, 3);
	//		//Sleep(100);
	//	}
	//}

	// Plays the Mario theme on the JoyCons:
	// I'm bad with music I just did this by
	// using a video of a piano version of the mario theme.
	// maybe eventually I'll be able to play something like sound files.

	// notes arbitrarily defined:
	#define C3 110
	#define D3 120
	#define E3 130
	#define F3 140
	#define G3 150
	#define G3A4 155
	#define A4 160
	#define A4B4 165
	#define B4 170
	#define C4 180
	#define D4 190
	#define D4E4 195
	#define E4 200
	#define F4 210
	#define F4G4 215
	#define G4 220
	#define A5 230
	#define B5 240
	#define C5 250



	if (settings.marioTheme) {
		for (int i = 0; i < 1; ++i) {

			printf("Playing mario theme...\n");

			float spd = 1;
			float spd2 = 1;

			//goto N1;

			Joycon joycon = joycons[0];

			Sleep(1000);

			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(400 / spd2);

			joycon.rumble(mk_odd(A4), 1); Sleep(400 / spd); joycon.rumble(1, 3);	// too low for joycon
			Sleep(50 / spd2);

			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(G3), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G1
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E3), 2); Sleep(200 / spd); joycon.rumble(1, 3);	// E1
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2

			Sleep(100 / spd2);
			joycon.rumble(mk_odd(B4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// B2

			Sleep(50 / spd2);
			joycon.rumble(mk_odd(A4B4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// A2-B2?
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(G3), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G1


			Sleep(100 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(A5), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A3



			Sleep(200 / spd2);
			joycon.rumble(mk_odd(F4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// F2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2

			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2

			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// D2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(B4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// B2


			Sleep(200 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(G3), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G1
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E3), 2); Sleep(200 / spd); joycon.rumble(1, 3);	// E1

			Sleep(200 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(B4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// B2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(A4B4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// A2-B2?
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2


			Sleep(100 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(A5), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A3
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(F4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// F2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2


			Sleep(200 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// D2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(B4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// B2

																					// new:

			Sleep(500 / spd2);

			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// F2-G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// F2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// D2-E2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2

			Sleep(200 / spd2);

			joycon.rumble(mk_odd(G3A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// G1-A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2


			Sleep(200 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// D2


			Sleep(300 / spd2);

			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// F2-G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// F2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// D2-E2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2


																					// three notes:
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(C5), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C3
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(C3), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C3
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C3), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C3


		N1:


			Sleep(500 / spd2);
			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// F2G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// F2

			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// D2E2

			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2



			Sleep(200 / spd2);
			joycon.rumble(mk_odd(G3A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// G1A2

			Sleep(50 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// D2


			Sleep(300 / spd2);
			joycon.rumble(mk_odd(D4E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// D2E2
			Sleep(300 / spd2);
			joycon.rumble(mk_odd(D4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// D2
			Sleep(300 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2


			Sleep(800 / spd2);


			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// F2G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// F2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// D2E2
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2

			Sleep(200 / spd2);


			joycon.rumble(mk_odd(G3A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// G1A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2

			Sleep(200 / spd2);

			joycon.rumble(mk_odd(A4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// D2

			Sleep(300 / spd2);


			joycon.rumble(mk_odd(G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4G4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// F2G2
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(F4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// F2


			Sleep(50 / spd2);
			joycon.rumble(mk_odd(D4E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);// D2E2
			Sleep(100 / spd2);
			joycon.rumble(mk_odd(E4), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// E2

																					// 30 second mark

																					// three notes:

			Sleep(300 / spd2);
			joycon.rumble(mk_odd(C5), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C3
			Sleep(200 / spd2);
			joycon.rumble(mk_odd(C5), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C3
			Sleep(50 / spd2);
			joycon.rumble(mk_odd(C5), 1); Sleep(200 / spd); joycon.rumble(1, 3);	// C3


			Sleep(1000);
		}
	}








	//Joycon *jc = &joycons[0];
	//for (int i = 40; i < 1000; ++i) {
	//	uint8_t hfa2 = 0x88;
	//	uint16_t lfa2 = 0x804d;
	//	jc->rumble3(i, hfa2, lfa2);
	//}




	#define MusicOffset 600

	// notes in hertz:
	#define C3 131 + MusicOffset
	#define D3 146 + MusicOffset
	#define E3 165 + MusicOffset
	#define F3 175 + MusicOffset
	#define G3 196 + MusicOffset
	#define G3A4 208 + MusicOffset
	#define A4 440 + MusicOffset
	#define A4B4 466 + MusicOffset
	#define B4 494 + MusicOffset
	#define C4 262 + MusicOffset
	#define D4 294 + MusicOffset
	#define D4E4 311 + MusicOffset
	#define E4 329 + MusicOffset
	#define F4 349 + MusicOffset
	#define F4G4 215 + MusicOffset
	#define G4 392 + MusicOffset
	#define A5 880 + MusicOffset
	#define B5 988 + MusicOffset
	#define C5 523 + MusicOffset

	#define hfa 0xb0	// 8a
	#define lfa 0x006c	// 8062


	if (false) {
		for (int i = 0; i < 1; ++i) {

			printf("Playing mario theme...\n");

			float spd = 1;
			float spd2 = 1;

			Joycon joycon = joycons[0];

			Sleep(1000);

			joycon.rumble3(E4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble3(E4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble3(E4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble3(C4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble3(E4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble3(G4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(400 / spd2);

			joycon.rumble3(A4, hfa, lfa); Sleep(400 / spd); joycon.rumble(1, 3);	// too low for joycon
			Sleep(50 / spd2);

			joycon.rumble3(C4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(200 / spd2);
			joycon.rumble3(G3, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// G1
			Sleep(200 / spd2);
			joycon.rumble3(E3, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E1
			Sleep(200 / spd2);
			joycon.rumble3(A4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// A2

			Sleep(100 / spd2);
			joycon.rumble3(B4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// B2

			Sleep(50 / spd2);
			joycon.rumble3(A4B4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);// A2-B2?
			Sleep(50 / spd2);
			joycon.rumble3(A4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(100 / spd2);
			joycon.rumble3(G3, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// G1


			Sleep(100 / spd2);
			joycon.rumble3(E4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E2
			Sleep(100 / spd2);
			joycon.rumble3(G4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// G2
			Sleep(100 / spd2);
			joycon.rumble3(A5, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// A3



			Sleep(200 / spd2);
			joycon.rumble3(F4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// F2
			Sleep(50 / spd2);
			joycon.rumble3(G4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// G2

			Sleep(200 / spd2);
			joycon.rumble3(E4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E2

			Sleep(50 / spd2);
			joycon.rumble3(C4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(50 / spd2);
			joycon.rumble3(D4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// D2
			Sleep(50 / spd2);
			joycon.rumble3(B4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// B2


			Sleep(200 / spd2);
			joycon.rumble3(C4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// C2
			Sleep(200 / spd2);
			joycon.rumble3(G3, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// G1
			Sleep(200 / spd2);
			joycon.rumble3(E3, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// E1

			Sleep(200 / spd2);
			joycon.rumble3(A4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(200 / spd2);
			joycon.rumble3(B4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// B2
			Sleep(200 / spd2);
			joycon.rumble3(A4B4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);// A2-B2?
			Sleep(50 / spd2);
			joycon.rumble3(A4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// A2
			Sleep(200 / spd2);
			joycon.rumble3(G4, hfa, lfa); Sleep(200 / spd); joycon.rumble(1, 3);	// G2


			Sleep(1000);
		}
	}

	printf("Done.\n");

	int counter = 0;




}



void exit() {

	RelinquishVJD(1);
	RelinquishVJD(2);

	if (settings.usingGrip) {
		for (int i = 0; i < joycons.size(); ++i) {
			joycons[i].deinit_usb();
		}
	}
	// Finalize the hidapi library
	res = hid_exit();
}







































// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// control ids
enum {
	SpinTimer = wxID_HIGHEST + 1
	//MyTimer = wxID_HIGHEST + 1,
};

// ----------------------------------------------------------------------------
// helper functions
// ----------------------------------------------------------------------------

static void CheckGLError() {
	GLenum errLast = GL_NO_ERROR;

	for (;; ) {
		GLenum err = glGetError();
		if (err == GL_NO_ERROR)
			return;

		// normally the error is reset by the call to glGetError() but if
		// glGetError() itself returns an error, we risk looping forever here
		// so check that we get a different error than the last time
		if (err == errLast) {
			wxLogError(wxT("OpenGL error state couldn't be reset."));
			return;
		}

		errLast = err;

		wxLogError(wxT("OpenGL error %d"), err);
	}
}

// function to draw the texture for cube faces
static wxImage DrawDice(int size, unsigned num) {
	wxASSERT_MSG(num >= 1 && num <= 6, wxT("invalid dice index"));

	const int dot = size / 16;        // radius of a single dot
	const int gap = 5 * size / 32;      // gap between dots

	wxBitmap bmp(size, size);
	wxMemoryDC dc;
	dc.SelectObject(bmp);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();
	dc.SetBrush(*wxBLACK_BRUSH);

	// the upper left and lower right points
	if (num != 1) {
		dc.DrawCircle(gap + dot, gap + dot, dot);
		dc.DrawCircle(size - gap - dot, size - gap - dot, dot);
	}

	// draw the central point for odd dices
	if (num % 2) {
		dc.DrawCircle(size / 2, size / 2, dot);
	}

	// the upper right and lower left points
	if (num > 3) {
		dc.DrawCircle(size - gap - dot, gap + dot, dot);
		dc.DrawCircle(gap + dot, size - gap - dot, dot);
	}

	// finally those 2 are only for the last dice
	if (num == 6) {
		dc.DrawCircle(gap + dot, size / 2, dot);
		dc.DrawCircle(size - gap - dot, size / 2, dot);
	}

	dc.SelectObject(wxNullBitmap);

	return bmp.ConvertToImage();
}

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// TestGLContext
// ----------------------------------------------------------------------------

TestGLContext::TestGLContext(wxGLCanvas *canvas) : wxGLContext(canvas) {
	SetCurrent(*canvas);

	// set up the parameters we want to use
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glEnable(GL_TEXTURE_2D);

	// add slightly more light, the default lighting is rather dark
	GLfloat ambient[] = { 0.5, 0.5, 0.5, 0.5 };
	glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);

	// set viewing projection
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-0.5f, 0.5f, -0.5f, 0.5f, 1.0f, 3.0f);

	// create the textures to use for cube sides: they will be reused by all
	// canvases (which is probably not critical in the case of simple textures
	// we use here but could be really important for a real application where
	// each texture could take many megabytes)
	glGenTextures(WXSIZEOF(m_textures), m_textures);

	for (unsigned i = 0; i < WXSIZEOF(m_textures); i++) {
		glBindTexture(GL_TEXTURE_2D, m_textures[i]);

		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		const wxImage img(DrawDice(256, i + 1));

		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.GetWidth(), img.GetHeight(),
			0, GL_RGB, GL_UNSIGNED_BYTE, img.GetData());
	}

	CheckGLError();
}

void TestGLContext::DrawRotatedCube(float xangle, float yangle) {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0f, 0.0f, -2.0f);
	glRotatef(xangle, 1.0f, 0.0f, 0.0f);
	glRotatef(yangle, 0.0f, 1.0f, 0.0f);

	// draw six faces of a cube of size 1 centered at (0, 0, 0)
	glBindTexture(GL_TEXTURE_2D, m_textures[0]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 0.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, -0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, -0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[1]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 0.0f, -1.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, 0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, 0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, -0.5f, -0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[2]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 1.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, 0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[3]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, -1.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, -0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, -0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[4]);
	glBegin(GL_QUADS);
	glNormal3f(1.0f, 0.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, -0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, 0.5f, -0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[5]);
	glBegin(GL_QUADS);
	glNormal3f(-1.0f, 0.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, -0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, -0.5f);
	glEnd();

	glFlush();

	CheckGLError();
}


void TestGLContext::DrawRotatedCube(glm::fquat q) {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();


	//glTranslatef(0.0f, 0.0f, -2.0f);


	//glm::vec3 eulerAngles = glm::eulerAngles(q);

	//glRotatef(glm::degrees(eulerAngles.x), 1.0f, 0.0f, 0.0f);
	//glRotatef(glm::degrees(eulerAngles.y), 0.0f, 1.0f, 0.0f);
	//glRotatef(glm::degrees(eulerAngles.z), 0.0f, 0.0f, 1.0f);

	


	//glm::mat4 m = glm::toMat4(q);
	glm::mat4 m = glm::mat4(1.0);
	m = glm::translate(m, glm::vec3(0.0f, 0.0f, -2.0f));
	m = m * glm::toMat4(q);
	glLoadMatrixf(&m[0][0]);

	// draw six faces of a cube of size 1 centered at (0, 0, 0)
	glBindTexture(GL_TEXTURE_2D, m_textures[0]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 0.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, -0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, -0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[1]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 0.0f, -1.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, 0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, 0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, -0.5f, -0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[2]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 1.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, 0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[3]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, -1.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, -0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, -0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[4]);
	glBegin(GL_QUADS);
	glNormal3f(1.0f, 0.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, -0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, 0.5f, -0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[5]);
	glBegin(GL_QUADS);
	glNormal3f(-1.0f, 0.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, -0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, -0.5f);
	glEnd();

	glFlush();

	CheckGLError();
}

void TestGLContext::DrawRotatedCube(float xangle, float yangle, float zangle) {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0f, 0.0f, -2.0f);
	glRotatef(xangle, 1.0f, 0.0f, 0.0f);
	glRotatef(yangle, 0.0f, 1.0f, 0.0f);
	glRotatef(zangle, 0.0f, 0.0f, 1.0f);

	// draw six faces of a cube of size 1 centered at (0, 0, 0)
	glBindTexture(GL_TEXTURE_2D, m_textures[0]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 0.0f, 1.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, -0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, -0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[1]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 0.0f, -1.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, 0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, 0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, -0.5f, -0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[2]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, 1.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, 0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[3]);
	glBegin(GL_QUADS);
	glNormal3f(0.0f, -1.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, -0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, -0.5f, 0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[4]);
	glBegin(GL_QUADS);
	glNormal3f(1.0f, 0.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(0.5f, 0.5f, 0.5f);
	glTexCoord2f(1, 0); glVertex3f(0.5f, -0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(0.5f, -0.5f, -0.5f);
	glTexCoord2f(0, 1); glVertex3f(0.5f, 0.5f, -0.5f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_textures[5]);
	glBegin(GL_QUADS);
	glNormal3f(-1.0f, 0.0f, 0.0f);
	glTexCoord2f(0, 0); glVertex3f(-0.5f, -0.5f, -0.5f);
	glTexCoord2f(1, 0); glVertex3f(-0.5f, -0.5f, 0.5f);
	glTexCoord2f(1, 1); glVertex3f(-0.5f, 0.5f, 0.5f);
	glTexCoord2f(0, 1); glVertex3f(-0.5f, 0.5f, -0.5f);
	glEnd();

	glFlush();

	CheckGLError();
}

// ----------------------------------------------------------------------------
// MyApp: the application object
// ----------------------------------------------------------------------------

//IMPLEMENT_APP(app);
wxIMPLEMENT_APP_NO_MAIN(MyApp);

//wxBEGIN_EVENT_TABLE(MyApp)
//EVT_TIMER(MyTimer, MyApp::OnMyTimer)
//wxEND_EVENT_TABLE()

//MyApp::MyApp()
//	: m_myTimer(this, MyTimer) {
//
//}

bool MyApp::OnInit() {
	if (!wxApp::OnInit()) {
		return false;
	}

	Connect(wxID_ANY, wxEVT_IDLE, wxIdleEventHandler(MyApp::onIdle));


	new MainFrame();

	//new MyFrame();

	//m_myTimer.Start(0);

	return true;
}

int MyApp::OnExit() {
	delete m_glContext;
	delete m_glStereoContext;

	return wxApp::OnExit();
}

void MyApp::onIdle(wxIdleEvent& evt) {
	pollLoop();
}

void MyApp::OnMyTimer(wxTimerEvent & WXUNUSED) {
	pollLoop();
}

TestGLContext& MyApp::GetContext(wxGLCanvas *canvas, bool useStereo) {
	TestGLContext *glContext;
	if (useStereo) {
		if (!m_glStereoContext) {
			// Create the OpenGL context for the first stereo window which needs it:
			// subsequently created windows will all share the same context.
			m_glStereoContext = new TestGLContext(canvas);
		}
		glContext = m_glStereoContext;
	} else {
		if (!m_glContext) {
			// Create the OpenGL context for the first mono window which needs it:
			// subsequently created windows will all share the same context.
			m_glContext = new TestGLContext(canvas);
		}
		glContext = m_glContext;
	}

	glContext->SetCurrent(*canvas);

	return *glContext;
}



MainFrame::MainFrame() : wxFrame(NULL, wxID_ANY, wxT("Joycon Driver by fosse �2017")) {

	wxPanel *panel = new wxPanel(this, wxID_ANY);


	CB1 = new wxCheckBox(panel, wxID_ANY, wxT("Combine JoyCons"), wxPoint(20, 20));
	CB1->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &MainFrame::toggleCombine, this);
	CB1->SetValue(settings.combineJoyCons);

	CB2 = new wxCheckBox(panel, wxID_ANY, wxT("Auto Center Sticks"), wxPoint(20, 40));
	CB2->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &MainFrame::toggleCenter, this);
	CB2->SetValue(settings.autoCenterSticks);

	CB3 = new wxCheckBox(panel, wxID_ANY, wxT("Gyro Controls"), wxPoint(20, 60));
	CB3->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &MainFrame::toggleGyro, this);
	CB3->SetValue(settings.enableGyro);

	CB4 = new wxCheckBox(panel, wxID_ANY, wxT("Gyro Window"), wxPoint(20, 80));
	CB4->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &MainFrame::toggleGyroWindow, this);
	CB4->SetValue(settings.gyroWindow);

	CB5 = new wxCheckBox(panel, wxID_ANY, wxT("Mario Theme"), wxPoint(20, 100));
	CB5->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &MainFrame::toggleMario, this);
	CB5->SetValue(settings.marioTheme);
	//CB4->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &[](wxCommandEvent&){}, this);



	CB6 = new wxCheckBox(panel, wxID_ANY, wxT("Reverse X"), wxPoint(20, 120));
	CB6->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &MainFrame::toggleReverseX, this);
	CB6->SetValue(settings.reverseX);

	CB7 = new wxCheckBox(panel, wxID_ANY, wxT("Reverse Y"), wxPoint(20, 140));
	CB7->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &MainFrame::toggleReverseY, this);
	CB7->SetValue(settings.reverseY);

	wxStaticText *slider1Text = new wxStaticText(panel, wxID_ANY, wxT("Gyro Controls Sensitivity X"), wxPoint(20, 160));
	slider1 = new wxSlider(panel, wxID_ANY, settings.gyroSensitivityX, 0, 1000, wxPoint(180, 140), wxDefaultSize, wxSL_LABELS);
	slider1->Bind(wxEVT_SLIDER, &MainFrame::setGyroSensitivityX, this);


	wxStaticText *slider2Text = new wxStaticText(panel, wxID_ANY, wxT("Gyro Controls Sensitivity Y"), wxPoint(20, 200));
	slider2 = new wxSlider(panel, wxID_ANY, settings.gyroSensitivityY, 0, 1000, wxPoint(180, 180), wxDefaultSize, wxSL_LABELS);
	slider2->Bind(wxEVT_SLIDER, &MainFrame::setGyroSensitivityY, this);




	wxStaticText *st1 = new wxStaticText(panel, wxID_ANY, wxT("Change the default settings and more in the config file!"), wxPoint(20, 220));


	wxButton *startButton = new wxButton(panel, wxID_EXIT, wxT("Start"), wxPoint(150, 240));
	startButton->Bind(wxEVT_BUTTON, &MainFrame::onStart, this);

	wxButton *quitButton = new wxButton(panel, wxID_EXIT, wxT("Quit"), wxPoint(250, 240));
	quitButton->Bind(wxEVT_BUTTON, &MainFrame::onQuit, this);

	SetClientSize(350, 280);
	Show();
}

void MainFrame::on_button_clicked(wxCommandEvent&) {
	wxMessageBox("pressed.", "Info");
}

void MainFrame::onStart(wxCommandEvent&) {
	setupConsole("Debug");
	if (settings.gyroWindow) {
		new MyFrame();
	}
	start();
	if (!settings.gyroWindow) {
		while (true) {
			pollLoop();
		}
	}
}

void MainFrame::onQuit(wxCommandEvent&) {
	exit(0);
	//close(true);
}

void MainFrame::toggleCombine(wxCommandEvent&) {
	settings.combineJoyCons = !settings.combineJoyCons;
}

void MainFrame::toggleCenter(wxCommandEvent&) {
	settings.autoCenterSticks = !settings.autoCenterSticks;
}

void MainFrame::toggleGyro(wxCommandEvent&) {
	settings.enableGyro = !settings.enableGyro;
}

void MainFrame::toggleGyroWindow(wxCommandEvent&) {
	settings.gyroWindow = !settings.gyroWindow;
}

void MainFrame::toggleMario(wxCommandEvent&) {
	settings.marioTheme = !settings.marioTheme;
}

void MainFrame::toggleReverseX(wxCommandEvent&) {
	settings.reverseX = !settings.reverseX;
}

void MainFrame::toggleReverseY(wxCommandEvent&) {
	settings.reverseY = !settings.reverseY;
}

void MainFrame::setGyroSensitivityX(wxCommandEvent&) {
	settings.gyroSensitivityX = slider1->GetValue();
}

void MainFrame::setGyroSensitivityY(wxCommandEvent&) {
	settings.gyroSensitivityY = slider2->GetValue();
}

// ----------------------------------------------------------------------------
// TestGLCanvas
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(TestGLCanvas, wxGLCanvas)
EVT_PAINT(TestGLCanvas::OnPaint)
EVT_KEY_DOWN(TestGLCanvas::OnKeyDown)
EVT_TIMER(SpinTimer, TestGLCanvas::OnSpinTimer)
wxEND_EVENT_TABLE()

// With perspective OpenGL graphics, the wxFULL_REPAINT_ON_RESIZE style
// flag should always be set, because even making the canvas smaller should
// be followed by a paint event that updates the entire canvas with new
// viewport settings.
TestGLCanvas::TestGLCanvas(wxWindow *parent, int *attribList)
	: wxGLCanvas(parent, wxID_ANY, attribList,
		wxDefaultPosition, wxDefaultSize,
		wxFULL_REPAINT_ON_RESIZE),
	m_xangle(0.0),
	m_yangle(0.0),
	m_spinTimer(this, SpinTimer),
	m_useStereo(false),
	m_stereoWarningAlreadyDisplayed(false)
{
	if (attribList) {
		int i = 0;
		while (attribList[i] != 0) {
			if (attribList[i] == WX_GL_STEREO) {
				m_useStereo = true;
			}
			++i;
		}
	}
	m_spinTimer.Start(0);
}

void TestGLCanvas::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	// This is required even though dc is not used otherwise.
	wxPaintDC dc(this);

	// Set the OpenGL viewport according to the client size of this canvas.
	// This is done here rather than in a wxSizeEvent handler because our
	// OpenGL rendering context (and thus viewport setting) is used with
	// multiple canvases: If we updated the viewport in the wxSizeEvent
	// handler, changing the size of one canvas causes a viewport setting that
	// is wrong when next another canvas is repainted.
	const wxSize ClientSize = GetClientSize();

	TestGLContext& canvas = wxGetApp().GetContext(this, m_useStereo);
	glViewport(0, 0, ClientSize.x, ClientSize.y);

	// Render the graphics and swap the buffers.
	GLboolean quadStereoSupported;
	glGetBooleanv(GL_STEREO, &quadStereoSupported);

	canvas.DrawRotatedCube(tracker.quat);
	//canvas.DrawRotatedCube(tracker.anglex, tracker.angley, tracker.anglez);
	if (m_useStereo && !m_stereoWarningAlreadyDisplayed) {
		m_stereoWarningAlreadyDisplayed = true;
		wxLogError("Stereo not supported by the graphics card.");
	}

	SwapBuffers();
}

void TestGLCanvas::Spin(float xSpin, float ySpin) {
	//m_xangle += xSpin;
	//m_yangle += ySpin;
	Refresh(false);
}

void TestGLCanvas::OnKeyDown(wxKeyEvent& event) {

	glm::fquat del;
	float angle = 0.25f;

	switch (event.GetKeyCode()) {
	case WXK_RIGHT:
		del = glm::angleAxis(-angle, glm::vec3(0.0, 0.0, 1.0));
		tracker.quat = tracker.quat*del;
		break;

	case WXK_LEFT:
		del = glm::angleAxis(angle, glm::vec3(0.0, 0.0, 1.0));
		tracker.quat = tracker.quat*del;
		break;

	case WXK_DOWN:
		del = glm::angleAxis(angle, glm::vec3(1.0, 0.0, 0.0));
		tracker.quat = tracker.quat*del;
		break;

	case WXK_UP:
		del = glm::angleAxis(-angle, glm::vec3(1.0, 0.0, 0.0));
		tracker.quat = tracker.quat*del;
		break;

	case WXK_SPACE:
		tracker.anglex = 0;
		tracker.angley = 0;
		tracker.anglez = 0;
		tracker.quat = glm::angleAxis(0.0f, glm::vec3(1.0, 0.0, 0.0));
		break;

	default:
		event.Skip();
		return;
	}
}

void TestGLCanvas::OnSpinTimer(wxTimerEvent& WXUNUSED(event)) {
	//Spin(tracker.relX, tracker.relY);
	Refresh(false);
	pollLoop();
}

wxString glGetwxString(GLenum name) {
	const GLubyte *v = glGetString(name);
	if (v == 0) {
		// The error is not important. It is GL_INVALID_ENUM.
		// We just want to clear the error stack.
		glGetError();

		return wxString();
	}

	return wxString((const char*)v);
}


// ----------------------------------------------------------------------------
// MyFrame: main application window
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
//EVT_MENU(wxID_NEW, MyFrame::OnNewWindow)
//EVT_MENU(wxID_CLOSE, MyFrame::OnClose)
wxEND_EVENT_TABLE()

MyFrame::MyFrame(bool stereoWindow) : wxFrame(NULL, wxID_ANY, wxT("3D JoyCon gyroscope visualizer")) {
	int stereoAttribList[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_STEREO, 0 };

	new TestGLCanvas(this, stereoWindow ? stereoAttribList : NULL);

	SetIcon(wxICON(sample));

	// Make a menubar
	//wxMenu *menu = new wxMenu;
	//menu->Append(wxID_NEW);
	//menu->Append(NEW_STEREO_WINDOW, "New Stereo Window");
	//menu->AppendSeparator();
	//menu->Append(wxID_CLOSE);
	//wxMenuBar *menuBar = new wxMenuBar;
	//menuBar->Append(menu, wxT("&Cube"));

	//SetMenuBar(menuBar);

	//CreateStatusBar();

	SetClientSize(400, 400);
	Show();

	// test IsDisplaySupported() function:
	static const int attribs[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, 0 };
	wxLogStatus("Double-buffered display %s supported", wxGLCanvas::IsDisplaySupported(attribs) ? "is" : "not");

	if (stereoWindow) {
		const wxString vendor = glGetwxString(GL_VENDOR).Lower();
		const wxString renderer = glGetwxString(GL_RENDERER).Lower();
		if (vendor.find("nvidia") != wxString::npos && renderer.find("quadro") == wxString::npos) {
			ShowFullScreen(true);
		}
	}
}


//int main(int argc, char *argv[]) {
int wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR cmdLine, int cmdShow) {

	parseSettings2();

	wxEntry(hInstance);

	return 0;
}
