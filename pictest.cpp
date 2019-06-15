/*******************************************************
 PIC reader for Roswell based on HIDAPI

 George Sun
 A-Concept Inc. 

 06/05/2019
********************************************************/

#include <cstdio>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <unistd.h>

#include "hidapi.h"

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

pid_t proc_find(const char* name) 
{
    DIR* dir;
    struct dirent* ent;
    char buf[512];

    long  pid;
    char pname[100] = {0,};
    char state;
    FILE *fp=NULL; 

    if (!(dir = opendir("/proc"))) 
	{
        perror("can't open /proc");
        return -1;
    }

    while((ent = readdir(dir))) 
	{
        long lpid = atol(ent->d_name);
        if(lpid < 0)
            continue;
        snprintf(buf, sizeof(buf), "/proc/%ld/stat", lpid);
        fp = fopen(buf, "r");

        if (fp) 
		{
            if ( (fscanf(fp, "%ld (%[^)]) %c", &pid, pname, &state)) != 3 )
			{
                printf("fscanf failed \n");
                fclose(fp);
                closedir(dir);
                return -1; 
            }
            if (!strcmp(pname, name)) 
			{
                fclose(fp);
                closedir(dir);
                return (pid_t)lpid;
            }
            fclose(fp);
        }
    }

	closedir(dir);
	return -1;
}

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
	
	string readGPS = "B2032200";
	string readRadar = "B2032300";
	string readTrigger = "B2032400";
	string readSerialNumber = "B20525000100";
	string readFirmwareVersion = "B20525070100";
	string readHardwareVersion = "B20525080100";
	string turnLEDOn = "B20541000100";
	string turnLEDOff = "B20541000000";
	string turnMIC1On = "B2063200020100";
	string turnMIC1Off = "B2063200020000";
	string turnMIC2On = "B2063200010100";
	string turnMIC2Off = "B2063200010100";
	string setHeartbeatOn = "B204F10100";
	string setHeartbeatOff = "B204F10000";
	string getSystemIssueFlags = "B2032700";

	bool ConsolePrint = argc > 1;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	string currentDateTime = getDateTime(tv.tv_sec, tv.tv_usec);
	FILE * fp = fopen("/tmp/pic.log", "a");
	fprintf(fp, "\n%s : Start picRoswell with command %s.\n", currentDateTime.c_str(), argv[0]);
	
	if (hid_init())
		return -1;

	// Open the device using the VID, PID,
	handle = hid_open(VID, PID, NULL);
	if (!handle) 
	{
		printf("\nUnable to open device. Please start again as: sudo %s.\n", argv[0]);
		fprintf(fp, "\nUnable to open device. Please start again as: sudo %s.\n", argv[0]);
		fflush(fp);
		return 1;
	}

	// Read the Manufacturer String
	wstr[0] = 0x0000;
	res = hid_get_manufacturer_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read manufacturer string\n");
	printf("\nManufacturer String: %ls\n", wstr);
	fprintf(fp, "Manufacturer String: %ls\n", wstr);

	// Read the Product String
	wstr[0] = 0x0000;
	res = hid_get_product_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read product string\n");
	printf("Product String: %ls\n", wstr);
	fprintf(fp, "Product String: %ls\n", wstr);

	// Read the Serial Number String
	wstr[0] = 0x0000;
	res = hid_get_serial_number_string(handle, wstr, MAX_STR);
	if (res < 0)
		printf("Unable to read serial number string\n");
	printf("Serial Number String: (%d) %ls\n\n", wstr[0], wstr);
	fprintf(fp, "Serial Number String: (%d) %ls\n\n", wstr[0], wstr);

	// Set the hid_read() function to be non-blocking.
	hid_set_nonblocking(handle, 1);

	// Setup the communication message queue
	key_t m_key = VID;  // using the VID as the key for message queue
	int s_ID = 0;
	int m_ID = msgget(m_key, 0666 | IPC_CREAT);
	printf("Start dialogue with the PIC.\n");
	printf("The log is saved in /tmp/pic.log.\n");
	printf("I can be reached at message queue of ID %d or of key %d.\n", m_ID, m_key);
	fprintf(fp, "Using message queue of ID %d or of key %d for communication.\n", m_ID, m_key);
	fflush(fp);
	
	struct MsgBuf m_buf;
	long sChn;
	int m_HeaderLength = sizeof(m_buf.sChn) + sizeof(m_buf.sec) + sizeof(m_buf.usec) + sizeof(m_buf.type) + sizeof(m_buf.len);
	string cmd;

	struct timespec tim = {0, 999999L}; // sleep for almost 1ms
	time_t LastSendSec = tv.tv_sec;
	time_t LastSendUSec = tv.tv_usec;
	time_t TimeElapsed;

	bool idle = false;
	bool WriteLock = false;
	memset(m_buf.mText, 0, sizeof(m_buf.mText));  // fill 0 before reading, make sure no garbage left over
	memset(buf, 0, sizeof(buf));
	commandQueue = readSerialNumber + readFirmwareVersion + readHardwareVersion + setHeartbeatOff;
	while (true)
	{
		// To sleep for 1ms. This may significantly reduce the CPU usage
		clock_nanosleep(CLOCK_REALTIME, 0, &tim, NULL);
		gettimeofday(&tv, NULL);
		currentDateTime = getDateTime(tv.tv_sec, tv.tv_usec);
		TimeElapsed = (tv.tv_sec - LastSendSec) * 1000000 + tv.tv_usec - LastSendUSec; // Time elapsed in microseconds since last command send 
		
		// read from message queue for any channel
		int l = msgrcv(m_ID, &m_buf, sizeof(m_buf), 0, IPC_NOWAIT);
		l -= m_HeaderLength;
		if (l >= 0)
		{
			// using rChn as the message queue ID
			if (s_ID != m_buf.rChn)
			{
				s_ID = m_buf.rChn;
				printf("\n%s Switch to a new message queue ID %d.\n", currentDateTime.c_str(), s_ID);
				fprintf(fp, "%s : Switch to a new message queue ID %d.\n", currentDateTime.c_str(), s_ID);
				fflush(fp);
			}
			
			// get the command from the message queue
			if (m_buf.mText[0] == 'B')
			{
				cmd.assign(m_buf.mText);
				commandQueue.append(cmd);
			}
			
			// update the receiving channel
			sChn = m_buf.sChn;
			idle = false;
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
			if (ConsolePrint)
				printf("\n%s : Unable to read from the pic with error %ls\n", currentDateTime.c_str(), hid_error(handle));
			fprintf(fp, "%s : Error: %ls\n", currentDateTime.c_str(), hid_error(handle));
			fflush(fp);
			hid_close(handle);
			//hid_exit();
			handle = 0;
			continue;
		}

		// Parse the reading from the PIC
		if (res > 0)
		{
			// print the reply from the pic
			if (ConsolePrint)
			{
				printf("\r%s (%ldus) %ld %02hx: ", getDateTime(LastSendSec, LastSendUSec).c_str(), TimeElapsed, sChn, buf[2]); // print the command byte
				for (int i = 3; i < buf[1] + 1; i++)
					if (buf[i] < 32 || buf[i] > 128)
						printf("<%02hx>", buf[i]);
					else
						printf("%c", buf[i]);					
			}
			
			if (buf[2] == 0x25)
			{
				// keep the system infomation in the log
				fprintf(fp, "%s (%ldus) %ld %02hx: ", getDateTime(LastSendSec, LastSendUSec).c_str(), TimeElapsed, sChn, buf[2]); 
				for (int i = 3; i < buf[1]; i++)
					fprintf(fp, "%c", buf[i]);
				fprintf(fp, "\n");
				fflush(fp);
			}

			m_buf.sec = tv.tv_sec;
			m_buf.usec = tv.tv_usec;
			m_buf.type = 11; // CMD_SERVICEDATA
			m_buf.rChn = sChn; // reply back to the sender's channel
			m_buf.sChn = PID;
			m_buf.len = buf[1] + 2; // the length of the pic report plus 0xB2 and len
			memcpy(m_buf.mText, buf, m_buf.len);
			m_buf.mText[m_buf.len] = 0; // put a /0 at the end. This may help to convert it into a string.
					
			if (s_ID && msgsnd(s_ID, &m_buf, m_buf.len + m_HeaderLength, IPC_NOWAIT))
			{
				if (ConsolePrint)
					printf("\n%s (Critical error) Unable to send the message to queue %d at channel %ld. Message is %s.\n", 
						currentDateTime.c_str(), s_ID, sChn, m_buf.mText);
				fprintf(fp, "%s Unable to send the message to queue %d at channel %ld. Message is ", 
					currentDateTime.c_str(), s_ID, sChn);
				for (int i = 0; i < m_buf.len - 2; i++)
					fprintf(fp, "%02hx", m_buf.mText[i]);
				fprintf(fp, "\n");
				fflush(fp);
				s_ID = 0;
			}
			
			WriteLock = false;
		}

		// Handle heartbeat after a long idle period > 30s
		if (TimeElapsed > 30000000L)
		{
			if (idle)
				continue;

			if (ConsolePrint)
				printf("\n%s send command HeartbeatOff\n", currentDateTime.c_str()); // print the command byte
			fprintf(fp, "%s send command HeartbeatOff to pic.\n", currentDateTime.c_str()); // print the command byte
			fflush(fp);

			commandQueue.append(setHeartbeatOff);
			idle = true;
		}
		
		if (TimeElapsed > 100000L) // timeout 100ms
			WriteLock = false;

		// Check if it is safe to send a new command to PIC
		if (commandQueue.length() > 3)
		{
			if (WriteLock)
				continue;
			
			LastSendSec = tv.tv_sec; // sending moment in seconds
			LastSendUSec = tv.tv_usec; // sending moment in microseconds
			res = sendPIC(handle, &commandQueue);
			if (res < 0)
			{
				if (ConsolePrint)
					printf("\nUnable to write to the PIC.\nError: %ls\n", hid_error(handle));
				fprintf(fp, "%s : Unable to write to the PIC.\nError: %ls\n", currentDateTime.c_str(), hid_error(handle));
				fflush(fp);
				hid_close(handle);
				handle = 0;
			}
		}
		else
			commandQueue.clear();

		WriteLock = true;
	}

	// Close the HID handle
	hid_close(handle);

	// Free static HIDAPI objects.
	hid_exit();

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
		cmd->clear();
	
	return hid_write(handle, buf, len + 1);
};