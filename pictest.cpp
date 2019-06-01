#include <cstdio>
/*******************************************************
 PIC reader based on HIDAPI

 George Sun
 A-Concept Inc. 

 03/22/2019
********************************************************/

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
//#include <time.h>
#include <iostream>
#include "hidapi.h"

// Headers needed for sleeping.
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;

int sendPIC(hid_device *handle, string *cmd); 

string getDateTime(time_t tv_sec, time_t tv_usec)
{
	struct tm *nowtm;
	char tmbuf[64], buf[64];

	nowtm = localtime(&tv_sec);
	strftime(tmbuf, sizeof tmbuf, "%Y-%m-%d %H:%M:%S", nowtm);
	snprintf(buf, sizeof buf, "%s.%06ld", tmbuf, tv_usec);
	return buf;
}

int main(int argc, char* argv[])
{
	int res;
#define MAX_STR 255
	wchar_t wstr[MAX_STR];
	hid_device *handle = NULL;
	unsigned short VID = 0x04d8;
	unsigned short PID = 0xf2bf;
	unsigned char buf[255];
	string commandQueue = "";
	int i;
	
	string readSerialNumber = "B20525000100";
	string readGPS = "B2032200";
	string readRadar = "B2032300";
	string readTrigger = "B2032400";
	string readFirmwareVersion = "B204250700";
	string readHardwareVersion = "B20525080100";
	string turnLEDOn = "B20541000100";
	string turnLEDOff = "B20541000000";
	string turnMIC1On = "B2063200020100";
	string turnMIC1Off = "B2063200020000";
	string turnMIC2On = "B2063200010100";
	string turnMIC2Off = "B2063200010100";
	string setHeartbeatOn = "B204F10100";
	string setHeartbeatOff = "B204F10200";
	string getSystemIssueFlags = "B2032700";

#ifdef WIN32
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
#endif

	if (hid_init())
		return -1;

	// Open the device using the VID, PID,
	handle = hid_open(VID, PID, NULL);
	if (!handle) 
	{
		printf("unable to open device\n");
		return 1;
	}

	// Read the Manufacturer String
	wstr[0] = 0x0000;
	res = hid_get_manufacturer_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read manufacturer string\n");
	printf("\nManufacturer String: %ls\n", wstr);

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
	printf("Serial Number String: (%d) %ls\n\n", wstr[0], wstr);

	// Set the hid_read() function to be non-blocking.
	hid_set_nonblocking(handle, 1);
	

	key_t m_key = 12345;
	int s_ID = 0;
	int m_ID = msgget(m_key, 0666 | IPC_CREAT);
	printf("Create a message queue of ID %d using %d as the key for receiving the main module key.\n", m_ID, m_key);
	// structure that holds the whole packet of a message in message queue
	struct MsgBuf
	{
		long rChn;  // Receiver type
		long sChn; // Sender Type
		long sec;    // timestamp sec
		long usec;   // timestamp usec
		size_t type; // type of this property, first bit 0 indicates string, 1 indicates interger. Any value greater than 32 is a command.
		size_t len;   //length of message payload in mText
		char mText[255];  // message payload
	};
	struct MsgBuf m_buf;
	int offset;
	long Chn;
	memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
	int m_HeaderLength = sizeof(m_buf.sChn) + sizeof(m_buf.sec) + sizeof(m_buf.usec) + sizeof(m_buf.type) + sizeof(m_buf.len);

	// Read the serial number (cmd 0x25). The first byte is always (0xB2).
	commandQueue = turnMIC1On + readSerialNumber + readHardwareVersion + readFirmwareVersion;

	// Read requested state. hid_read() has been set to be
	// non-blocking by the call to hid_set_nonblocking() above.
	// This loop demonstrates the non-blocking nature of hid_read().
	//int ticksEverySecond = 4;
	//int intervalTime = CLOCKS_PER_SEC / ticksEverySecond;
	//clock_t timerSafeWrite = 0;  // timer used to guard safe writing of new commands to PIC. 0 will allow immidiate writting in main loop
	//clock_t timerPeriodicCommands = clock() + intervalTime;	// timer used to tracking periodic commands. Next commands are added later.
	//clock_t t, t0=0;

	res = 0;
	int count = 0;
	struct timespec tim = {0, 990000L}; // sleep for almost 1ms
	struct timeval tv;
	long lastSend = 0;
	long dt;
	string triggers = "";
	string lastTriggers = "";
	memset(buf, 0, sizeof(buf));
	printf("Start dialogue with the PIC.\n");
	while (true)
	{
		// To sleep for 1ms. This may significantly reduce the CPU usage
		clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);
		gettimeofday(&tv, NULL);
		
		int l = msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, IPC_NOWAIT);
		l -= m_HeaderLength;
		if (l >= 0)
		{
			m_key = m_buf.sChn;
			s_ID = msgget(m_key, 0444 | IPC_CREAT);
			printf("\nGet a new key %d, with which creates a message queue of ID %d.\n", m_key, s_ID);
		}
		
		// This handles the lost of PIC
		if (!handle)
		{
			handle = hid_open(VID, PID, NULL);
			if (!handle)
				continue;
			
			commandQueue = getSystemIssueFlags;
			hid_set_nonblocking(handle, 1);
		}

		// Try to read from the PIC
		res = hid_read(handle, buf, sizeof(buf));
		if (res < 0)
		{
			printf("\nUnable to read from the PIC.\n");
			printf("Error: %ls\n", hid_error(handle));
			hid_close(handle);
			//hid_exit();
			handle = 0;
			continue;
		}

		// Parse the reading from the PIC
		if (res > 0)
		{
			i = buf[1];
			buf[i] = 0;
			string ID = "Serial Number";
			dt = tv.tv_usec - lastSend;
			if (dt < 0)
				dt += 1000000L;

			switch (buf[2])
			{
				case 0x22 : // GPS reading
					printf("\r%s %ld ", getDateTime(tv.tv_sec, tv.tv_usec).c_str(), dt);
					
					// print the latest GPS data in case cannot send it out
					if (s_ID <= 0)
					{
						printf("GPS: %s %s ", buf + 3, buf + 48);
						break;
					}
					
					Chn = 3; // GPS channel
					offset = 0;
					// readoff all the messages at that channel first in case it does not work any more
					while (msgrcv(s_ID, &m_buf, sizeof(m_buf), Chn, IPC_NOWAIT) > 0)
						offset++;
					if (offset)
						printf("There are %d stale messages in the GPS channel.\n", offset);
					
					// position
					strcpy(m_buf.mText, "position");
					offset = strlen("position") + 1;
					m_buf.mText[offset++] = 0;	// specify it is a string
					l = offset;
					for (i = 3; i < 3 + 9; i++)	// xxxx.xxxx
						m_buf.mText[offset++] = buf[i];
					m_buf.mText[offset++] = buf[13]; // N
					m_buf.mText[offset++] = buf[14]; // ,
					for (i = 15; i < 15 +10; i++)	// xxxxx.xxxx
						m_buf.mText[offset++] = buf[i];
					m_buf.mText[offset++] = buf[26]; // W
					m_buf.mText[offset++] = 0;	// specify it is end of a string
					
					printf("%s=%s, ", m_buf.mText, m_buf.mText + l);
					l = offset;
					
					// altitute
					strcpy(m_buf.mText + offset, "altitute");
					offset += strlen("altitute") + 1;
					m_buf.mText[offset++] = 0;	// specify it is a string
					i = offset;
					m_buf.mText[offset++] = buf[28];
					m_buf.mText[offset++] = buf[29];
					m_buf.mText[offset++] = buf[30];
					m_buf.mText[offset++] = 0;
					
					printf("%s=%s, ", m_buf.mText + l, m_buf.mText + i);
					l = offset;
					
					// time
					strcpy(m_buf.mText + offset, "time");
					offset += strlen("time") + 1;
					m_buf.mText[offset++] = 0;	// specify it is a string
					i = offset;
					m_buf.mText[offset++] = buf[48];
					m_buf.mText[offset++] = buf[49];
					m_buf.mText[offset++] = ':';
					m_buf.mText[offset++] = buf[50];
					m_buf.mText[offset++] = buf[51];
					m_buf.mText[offset++] = ':';
					m_buf.mText[offset++] = buf[52];
					m_buf.mText[offset++] = buf[53];
					m_buf.mText[offset++] = 0;
					
					printf("%s=%s, ", m_buf.mText + l, m_buf.mText + i);
					l = offset;
					
					// date
					strcpy(m_buf.mText + offset, "date");
					offset += strlen("date") + 1;
					m_buf.mText[offset++] = 0;	// specify it is a string
					i = offset;
					m_buf.mText[offset++] = '2';
					m_buf.mText[offset++] = '0';
					m_buf.mText[offset++] = buf[59];
					m_buf.mText[offset++] = buf[60];
					m_buf.mText[offset++] = '-';
					m_buf.mText[offset++] = buf[57];
					m_buf.mText[offset++] = buf[58];
					m_buf.mText[offset++] = '-';
					m_buf.mText[offset++] = buf[55];
					m_buf.mText[offset++] = buf[56];
					m_buf.mText[offset++] = 0;

					printf("%s=%s, ", m_buf.mText + l, m_buf.mText + i);
					
					m_buf.len = offset;	
					m_buf.type = 14; // CMD_PUBLISHDATA
					m_buf.sec = tv.tv_sec;
					m_buf.usec = tv.tv_usec;
					m_buf.sChn = Chn;
					m_buf.rChn = Chn;
					
					if (msgsnd(s_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
						printf("\n(Debug) Critical error. Unable to send the message to queue %d. Message is of length %d, and header length %d.", 
							s_ID, offset, m_HeaderLength);

					break;
					
				case 0x25 : // Serial number
					if (buf[3] > 47 && buf[3] < 58)
						ID = "Firmware version";
					if (i > 20)
						ID = "Hardware version";
					printf("\n%s %ld %s %s", getDateTime(tv.tv_sec, tv.tv_usec).c_str(), dt, ID.c_str(), buf + 3);
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
						printf("\n%s %ld Triggers: %s\n", getDateTime(tv.tv_sec, tv.tv_usec).c_str(), dt, triggers.c_str());
						lastTriggers = triggers;
					}
					
					if (s_ID <= 0)
						break;
					
					offset = 0;
					// Trigger
					strcpy(m_buf.mText, "Trigger");
					offset += strlen("Trigger") + 1;
					m_buf.mText[offset++] = 0;	// specify it is a string
					strcpy(m_buf.mText + offset, triggers.c_str());
					offset += triggers.length() + 1;

					Chn = 5; // trigger channel
					m_buf.sec = tv.tv_sec;
					m_buf.usec = tv.tv_usec;
					m_buf.sChn = Chn;
					m_buf.rChn = Chn;
					m_buf.len = offset;	
					m_buf.type = 14; // CMD_PUBLISHDATA
					if (msgsnd(s_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
						printf("\n(Debug) Critical error. Unable to publish Trigger data. length %ld, and header length %d.\n", m_buf.len, m_HeaderLength);
					break;
					
				case 0x41 : // LED reading
				case 0x32 : // MIC on/off
					break;
					
				default :
					printf("\n%s %ld: ", getDateTime(tv.tv_sec, tv.tv_usec).c_str(), dt);
					for (unsigned short i = 0; i < buf[1] + 1; i++)
						printf("%02hx ", buf[i]);
					printf("\n");
			}
			
			memset(buf, 0, sizeof(buf));
			fflush(stdout);	
			continue;
		}
		
		// Check if it is time to add a periodic command
		count++;
		if (count >= 1000)
			count = 0;

		switch (count)
		{
			case 0 :  // read Triggers every 250ms
			case 250 :
			case 500 :
			case 750 :
				commandQueue.append(readTrigger);
				break;
			case 50 :  // turn LED off every second at 50ms
				commandQueue.append(turnLEDOff);
				break;
			case 400 :  // read GPS every second at 400ms
				commandQueue.append(readGPS);
				break;
			case 550 : // turn LED on every second at 550ms
				commandQueue.append(turnLEDOn);
		}			

		// Check if it is safe to send a new command to PIC
		if (commandQueue.length() > 3)
		{
			//timerSafeWrite = timerPeriodicCommands; // hold next sending till successful read or next new command
			lastSend = tv.tv_usec; // sending moment in microseconds
			res = sendPIC(handle, &commandQueue);
			if (res < 0)
			{
				printf("\nUnable to write to the PIC.\n");
				printf("Error: %ls\n", hid_error(handle));
				hid_close(handle);
				//hid_exit();
				handle = 0;
				continue;
			}
		}
		else
			commandQueue.assign("");
	}

	hid_close(handle);

	/* Free static HIDAPI objects. */
	hid_exit();

#ifdef WIN32
	system("pause");
#endif

	return 0;
}

int sendPIC(hid_device *handle, string *cmd)
{
	int len = cmd->length();
	// the command shall be lat least 6 characters
	if (len < 6)
	{
		cmd->assign("");
		return 0;
	}
	
	unsigned char buf[65];
	unsigned char crc = 0;
	
	memset(buf, 0, sizeof(buf));
	buf[0] = 0xB2;
	buf[1] = 0x03; // assign an initial length
	len = len / 2;
	for (int i = 0; i < buf[1] + 1; i++)
	{
		buf[i] = stoi(cmd->substr(i + i, 2), nullptr, 16) & 0xFF;
		crc += buf[i];
	}
	crc = (crc ^ 0xFF) + 1;
	len = buf[1] & 0xFF;	
	buf[len] = crc;

	if (len < 3 || len > 20)
	{
		printf("\nInvalid command %s in queue.\n", cmd->c_str());
		cmd->assign("");
		return -1;
	}
	
	// Update the command queue
	if (buf[1] < cmd->length())
		cmd->assign(cmd->substr(len + len + 2));
	else
		cmd->assign("");
	
	return hid_write(handle, buf, len + 1);
};