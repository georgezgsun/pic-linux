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
#include <sys/time.h>
#include <math.h>
#include <iostream>
#include "hidapi.h"

// Headers needed for sleeping.
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int sendPIC(hid_device *handle, std::string *cmd); 
int readPIC(hid_device *handle, unsigned char *buf);

int main(int argc, char* argv[])
{
	int res;
#define MAX_STR 255
	wchar_t wstr[MAX_STR];
	hid_device *handle = NULL;
	unsigned short VID = 0x04d8;
	unsigned short PID = 0xf2bf;
	unsigned char buf[255];
	std::string cmd = "";
	int i;
	
	std::string readSerialNumber = "B20525000100";
	std::string readGPS = "B2032200";
	std::string readRadar = "B2032300";
	std::string readTrigger = "B2032400";
	std::string readFirmwareVersion = "B204250700";
	std::string readHardwareVersion = "B20525080100";
	std::string turnLEDOn = "B20541000100";
	std::string turnLEDOff = "B20541000000";
	std::string turnMIC1On = "B2063200020100";
	std::string turnMIC1Off = "B2063200020000";
	std::string turnMIC2On = "B2063200010100";
	std::string turnMIC2Off = "B2063200010100";
	std::string setHeartbeatOn = "B204F10100";
	std::string setHeartbeatOff = "B204F10200";	

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

	// Read the serial number (cmd 0x25). The first byte is always (0xB2).
	cmd = turnMIC1On + readHardwareVersion + readSerialNumber + readFirmwareVersion;
	printf("Commands in queue: %s\n", cmd.c_str());
	res = sendPIC(handle, &cmd);
	if (res < 0)
	{
		printf("Unable to write()\n");
		printf("Error: %ls\n", hid_error(handle));
	}

	// Read requested state. hid_read() has been set to be
	// non-blocking by the call to hid_set_nonblocking() above.
	// This loop demonstrates the non-blocking nature of hid_read().
	/*
	long intervalTime = 100 * 1000; // 100ms
	long dt = 0;
	struct timeval tv, tv0;
	gettimeofday(&tv0, nullptr);
	*/
	int intervalTime = rint(CLOCKS_PER_SEC / 10);
	clock_t t0 = clock() + intervalTime;
	clock_t t;

	res = 0;
	int count = 0;
	std::string dataGPS = "";
	std::string triggers = "";
	std::string lastTriggers = "";
	memset(buf, 0, sizeof(buf));
	printf("Start dialogue with the PIC.\n");
	while (true)
	{
		//gettimeofday(&tv, nullptr);
		t = clock();
		res = hid_read(handle, buf, sizeof(buf));
		if (res < 0)
		{
			printf("Unable to read()\n");
			break;
		}

		if (res > 0)
		{	
			switch (buf[2])
			{
				case 0x22 : // GPS reading
				case 0x25 : // Serial number
					//printf("\r%ld.%06ld \t", tv.tv_sec, tv.tv_usec);
					printf("\r%ld \t", t);
					for (i = 3; i < buf[1]; i++)
						printf("%c", buf[i]);
					break;
					
				case 0x24 :  // Trigger
					triggers = "";
					for (i = 3; i < buf[1] - 1; i++)
					{
						if (buf[i])
							triggers.append("^");
						else
							triggers.append("_");
						
						if ((i-2) % 4 == 0)
							triggers.append(" ");
					}
					
					if (triggers != lastTriggers)
					{
						//printf("\n%ld.%06ld Triggers: %s\n", tv.tv_sec, tv.tv_usec, triggers.c_str());
						printf("\n%ld Triggers: %s\n", t, triggers.c_str());
						lastTriggers = triggers;
					}
					break;
					
				case 0x41 : // LED reading
				case 0x32 : // MIC on/off
					break;
					
				default :
					for (unsigned short i = 0; i < buf[1] + 1; i++)
						printf("%02hx ", buf[i]);
					printf("\n");
			}
			
		
			if (cmd.length() > 2)
			{
				// wait 1ms before send new command
				nanosleep((const struct timespec[]){{0, 1000000L}}, NULL);
				//printf("\nCommands in queue %s before sending to PIC. ", cmd.c_str());
				res = sendPIC(handle, &cmd);
				if (res < 0)
				{
					printf("Unable to write to PIC. %s\n", cmd.c_str());
					printf("Error: %ls\n", hid_error(handle));
				}
			}
			memset(buf, 0, sizeof(buf));
			fflush(stdout);
		}

		//if ((tv.tv_sec - tv0.tv_sec) * 1000000 + tv.tv_usec - tv0.tv_usec > intervalTime)
		if (t > t0)
		{
			/*
			tv0.tv_usec += intervalTime;
			if (tv0.tv_usec >= 1000000)
			{
				tv0.tv_sec++;
				tv0.tv_usec -= 1000000;
			}
			*/
			t0 += intervalTime;
			
			count++;
			cmd.append(readTrigger);
			res = sendPIC(handle, &cmd);
			if (res < 0)
			{
				printf("Unable to write to PIC\n");
				printf("Error: %ls %s\n", hid_error(handle), cmd.c_str());
			}

			//cmd = "";
			switch (count)
			{
				case 1 :  // turn LED off
					cmd.append(turnLEDOff);
					break;
				case 2 :  // read GPS
					cmd.append(readGPS);
					break;
				case 6 : // turn LED on
					cmd.append(turnLEDOn);
					break;
			}
			
			if (count >= 10)
				count = 0;
		}
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

int readPIC(hid_device *handle, unsigned char *buf)
{
	return hid_read(handle, buf, 65);
}

int sendPIC(hid_device *handle, std::string *cmd)
{
	int len = cmd->length();
	// the command shall be lat least 6 characters and in even number length
	if (len < 6 || len % 2 == 1)
	{
		cmd->assign("");
		return -1;
	}
	
	unsigned char buf[65];
	unsigned char crc = 0;
	
	memset(buf, 0, sizeof(buf));
	buf[0] = 0xB2;
	buf[1] = 0x03; // assign an initial length
	len = len / 2;
	for (int i = 0; i < buf[1] + 1; i++)
	{
		buf[i] = std::stoi(cmd->substr(i + i, 2), nullptr, 16) & 0xFF;
		crc += buf[i];
	}
	crc = (crc ^ 0xFF) + 1;
	len = buf[1] & 0xFF;	
	buf[len] = crc;

	if (len < 3 || len > 20)
	{
		cmd->assign("");
		return -1;
	}
	
	//printf("\n Original command: %s %d %s", cmd->c_str(), len, cmd->substr(len + 1).c_str());
	if (buf[1] < cmd->length())
		cmd->assign(cmd->substr(len + len + 2));
	else
		cmd->assign("");
	//printf("\n Processed command: %s", cmd->c_str());
	
	return hid_write(handle, buf, len + 1);
};