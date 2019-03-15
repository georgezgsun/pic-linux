#include <cstdio>
/*******************************************************
 Windows HID simplification

 Alan Ott
 Signal 11 Software

 8/22/2009

 Copyright 2009

 This contents of this file may be used by anyone
 for any reason without any conditions and may be
 used as a starting point for your own applications
 which use HIDAPI.
********************************************************/

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include "hidapi.h"

// Headers needed for sleeping.
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int sendPIC(hid_device *handle, unsigned char *buf);
int readPIC(hid_device *handle, unsigned char *buf);

int main(int argc, char* argv[])
{
	int res;
	unsigned char buf[256];
#define MAX_STR 255
	wchar_t wstr[MAX_STR];
	hid_device *handle = NULL;
	unsigned short VID = 0x04d8;
	unsigned short PID = 0xf2bf;
	int i;

#ifdef WIN32
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
#endif

	struct hid_device_info *devs, *cur_dev;

	if (hid_init())
		return -1;

	devs = hid_enumerate(0x0, 0x0);
	cur_dev = devs;
	while (cur_dev) {
		printf("Device Found\n  type: %04hx %04hx\n  path: %s\n  serial_number: %ls", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path, cur_dev->serial_number);
		printf("\n");
		printf("  Manufacturer: %ls\n", cur_dev->manufacturer_string);
		printf("  Product:      %ls\n", cur_dev->product_string);
		printf("  Release:      %hx\n", cur_dev->release_number);
		printf("  Interface:    %d\n", cur_dev->interface_number);
		printf("\n");
		cur_dev = cur_dev->next;
	}
	hid_free_enumeration(devs);

	// Set up the command buffer.
	memset(buf, 0x00, sizeof(buf));

	// Open the device using the VID, PID,
	handle = hid_open(VID, PID, NULL);
	if (!handle) {
		printf("unable to open device\n");
		return 1;
	}

	// Read the Manufacturer String
	wstr[0] = 0x0000;
	res = hid_get_manufacturer_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read manufacturer string\n");
	printf("Manufacturer String: %ls\n", wstr);

	// Read the Product String
	wstr[0] = 0x0000;
	res = hid_get_product_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read product string\n");
	printf("Product String: %ls\n", wstr);

	// Read the Serial Number String
	wstr[0] = 0x0000;
	res = hid_get_serial_number_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read serial number string\n");
	printf("Serial Number String: (%d) %ls", wstr[0], wstr);
	printf("\n");

	// Set the hid_read() function to be non-blocking.
	hid_set_nonblocking(handle, 1);

	// Try to read from the device. There shoud be no
	// data here, but execution should not block.
	res = hid_read(handle, buf, 64);

	memset(buf, 0, sizeof(buf));

	// Read the serial number (cmd 0x25). The first byte is always (0xB2).
	buf[0] = 0xB2;
	buf[1] = 0x05;
	buf[2] = 0x25; // Read Device ID
	buf[3] = 0x00;
	buf[4] = 0x01;
	res = sendPIC(handle, buf);
	if (res < 0)
	{
		printf("Unable to write()\n");
		printf("Error: %ls\n", hid_error(handle));
	}

	// Read requested state. hid_read() has been set to be
	// non-blocking by the call to hid_set_nonblocking() above.
	// This loop demonstrates the non-blocking nature of hid_read().
	clock_t t0 = clock() + CLOCKS_PER_SEC / 8;
	clock_t t;

	res = 0;
	int count = 0;
	std::string dataGPS = "";
	printf("Start dialogue with the PIC\n");
	while (true)
	{
		t = clock();
		res = hid_read(handle, buf, sizeof(buf));
		if (res < 0)
		{
			printf("Unable to read()\n");
			break;
		}

		if (res > 0)
		{
			//printf("Get reply from PIC, length=%d, data=", buf[1]);
			switch (buf[2])
			{
			case 0x22 : // GPS reading
			case 0x25 : // Serial number
				for (i = 3; i < buf[1] - 1; i++)
					printf("%c", buf[i]);
				printf("\n");
				break;
			case 0x24 :  // Trigger
				for (i = 3; i < buf[1] - 2; i++)
				{
					if ((i-2) % 4 == 0)
						printf(" ");
					if (buf[i])
						printf("^");
					else
						printf("_");
				}
				printf("\n");
				break;
			case 0x41 : // LED reading
				break;
			default :
				for (unsigned short i = 0; i < buf[1] + 1; i++)
					printf("%02hx ", buf[i]);
				printf("\n");
			}
		}

		if (t > t0)
		{
			t0 += CLOCKS_PER_SEC / 4;
			count++;
			//printf("Count = %d\n", count);

			switch (count % 4)
			{
			case 0 :  // turn LED off
				buf[1] = 0x05;
				buf[2] = 0x41;
				buf[3] = 0x00;
				buf[4] = 0x00;
				break;
			case 1 :  // read GPS
				buf[1] = 0x03;
				buf[2] = 0x22;
				break;
			case 2 : // turn LED on
				buf[1] = 0x05;
				buf[2] = 0x41;
				buf[3] = 0x00;
				buf[4] = 0x01;
				break;
			case 3 : // read Trigger
				buf[1] = 0x03;
				buf[2] = 0x24;
				break;
			}
			res = sendPIC(handle, buf);
			if (res < 0)
			{
				printf("Unable to write()\n");
				printf("Error: %ls\n", hid_error(handle));
			}
		}
		//usleep(10);
	}

	printf("Data read:\n   ");
	// Print out the returned buffer.
	for (i = 0; i < res; i++)
		printf("%02hhx ", buf[i]);
	printf("\n");

	hid_close(handle);

	/* Free static HIDAPI objects. */
	hid_exit();

#ifdef WIN32
	system("pause");
#endif

	return 0;
}

int sendPIC(hid_device *handle, unsigned char *buf)
{
	int index = buf[1] & 0xFF;
	if (!buf || index < 3 || index > 63)
		return -1;

	buf[0] = 0xB2;  // 	the first byte is always B2

	// Calculate the CRC
	unsigned char crc = 0;
	for (int i = 0; i < index; i++)
	{
		crc += buf[i];
		//printf("%02hx ", buf[i]);
	}
	crc = (crc ^ 0xFF) + 1;
	
	buf[index] = crc; 
	//printf("%02hx %04hx\n", buf[buf[1]], crc);

	return hid_write(handle, buf, index+1);
}

int readPIC(hid_device *handle, unsigned char *buf)
{
	return hid_read(handle, buf, 65);
}
