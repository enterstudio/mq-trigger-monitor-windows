/*******************************************************************************
 * Copyright (c) 1998, 2014 IBM Corporation and other Contributors.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *   Jeff Lowrey, Wayne Schutz - Initial Contribution
 */

////////////////////////////////////////////////////////////////////////////////////
// This program install the trigsvc program, creates the necessary registry entries
// and registers the message resources with the NT Event Log facility
//
/* ***************************************************************/
// setup.cpp
//
// Author: Wayne M. Schutz, Sterling Forest NY, IBM
//
// See trigsvc.cpp for change log
#include <windows.h>
#include <iostream>
#include <stdio.h>
#include <cmqc.h>
#include <cmqxc.h>
#include <Wincrypt.h>
//using namespace std;

#include "trigsvc.h"

#define USAGE "\nParameters are:\n\n" \
  "setup <TriggerQueueName <<QueueManagerName >>\n" \
  "  or:\n" \
  "setup -f configurationFile\n" \
  "       where \"configurationFile\" is the configuration file\n\n" \
  "If not specified, triggerqueue defaults to SYSTEM.DEFAULT.INITIATION.QUEUE\n\n"
;

#define DEFAULTID "LocalSystem"

void ErrorHandler(char *s, DWORD err) {
	LPVOID lpMsgBuf;
//	    LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();
//
	FormatMessage(
	FORMAT_MESSAGE_ALLOCATE_BUFFER |
	FORMAT_MESSAGE_FROM_SYSTEM |
	FORMAT_MESSAGE_IGNORE_INSERTS,
	NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0,
	NULL);

	printf("%s\nError number %d with message %s \n", s, err, (char*) lpMsgBuf);
	ExitProcess(err);
}

int processini(char *);
void makeGoodForRegistry(char *, char *, int, int *);
char* encryptData(char* inStr);
void hexArrayToStr(unsigned char* info, unsigned int infoLength, char **buffer);
// big arrays to hold the registry keys

CHAR Comment[MAXTHREADS * (MAX_PATH + 1) + 1];
CHAR QueueName[MAXTHREADS * (MQ_Q_NAME_LENGTH + 1) + 1];
char ServiceName[MAXTHREADS * (MQ_SERVICE_NAME_LENGTH + 1) + 1];
CHAR QueueMgrName[MAXTHREADS * (MQ_Q_MGR_NAME_LENGTH + 1) + 1];
CHAR NotesIni[MAXTHREADS * (MAX_PATH + 1) + 1];
CHAR Conname[MAXTHREADS * (MQ_CONN_NAME_LENGTH + 1) + 1];
CHAR Channel[MAXTHREADS * (MQ_CHANNEL_NAME_LENGTH + 1) + 1];
CHAR Locladdr[MAXTHREADS * (MQ_LOCAL_ADDRESS_LENGTH + 1) + 1];
CHAR RcvData[MAXTHREADS * (MQ_EXIT_DATA_LENGTH + 1) + 1];
CHAR ScyData[MAXTHREADS * (MQ_EXIT_DATA_LENGTH + 1) + 1];
CHAR SendData[MAXTHREADS * (MQ_EXIT_DATA_LENGTH + 1) + 1];
CHAR RcvExit[MAXTHREADS * (MQ_EXIT_NAME_LENGTH + 1) + 1];
CHAR ScyExit[MAXTHREADS * (MQ_EXIT_NAME_LENGTH + 1) + 1];
CHAR SendExit[MAXTHREADS * (MQ_EXIT_NAME_LENGTH + 1) + 1];
CHAR Userid[MAXTHREADS * (MQ_USER_ID_LENGTH + 1) + 1];
CHAR Sslciph[MAXTHREADS * (MQ_SSL_CIPHER_SPEC_LENGTH + 1) + 1];
CHAR Sslpeer[MAXTHREADS * (MQ_SSL_PEER_NAME_LENGTH + 1) + 1];
// CHAR  Trptype[MAXTHREADS*(10)+1];
CHAR Hbint[MAXTHREADS * (10) + 1];
CHAR Kaint[MAXTHREADS * (10) + 1];
CHAR mqcdVersion[MAXTHREADS * (3) + 1];
CHAR ChannelUserId[MAXTHREADS * (1024) + 1];
char ChannelPassword[MAXTHREADS * (MQ_CSP_PASSWORD_LENGTH * 4) + 1];
//I am guessing right now that 4x length is sufficient to hold an encrypted password of normal MQ_CSP_PASSWORD_LENGTH

DWORD WaitIntervalDefault = 60000; /* default wait interval in millibvseconds */
DWORD LongRtyDefault = 999999999;
DWORD LongTmrDefault = 1200; //
DWORD ShortRtyDefault = 10;
DWORD ShortTmrDefault = 60;
DWORD EventLevelDefault = 2; // 1 = normal msgs, 2 = detailed (process), 3 = debug
CHAR AgentRedirDefault[MAX_PATH] = "Yes"; // whether or not to redirect Notes agent output
CHAR MQdllDefault[MAX_PATH]; // default .dll to use for mqseries
int debug = 0; // is debug output enabled?
int qmgrindx = 0; // length of qmgr namelist
int qindx = 0; // length of queue namelist
int sindx = 0; // length of service namelist
int niindx = 0; // length of notesIni namelist 
int con_indx = 0, chan_indx = 0, locl_indx = 0, rcvd_indx = 0, scyd_indx = 0,
		sndd_indx = 0, rcve_indx = 0;
int scye_indx = 0, snde_indx = 0, user_indx = 0, sslc_indx = 0, sslp_indx = 0,
		trpt_indx = 0, hbin_indx = 0;
int kain_indx = 0, chlui_indx = 0, chlpw_indx = 0, mqcd_indx = 0;

CHAR keyRepos[MAX_PATH]; // location of ssl key repository

CHAR serviceUserid[128]; // the userid the service should run as
CHAR servicePassword[128]; // the password the service should run as

int install_flag = 0; // 0 = do not (re)install, 1 = (re)install application

// ************************************* main process ***************************************
int main(int argc, char *argv[]) {

	LONG ret;
	HKEY keyHandle;
	DWORD eventTypes;
	CHAR strBuf[500];
	DWORD disposition;
	DWORD categoryCount;
	DWORD len;
	CHAR PathProgram[MAX_PATH + 11];/* Program path plus executable name*/
	CHAR Path[MAX_PATH]; /* Program path */
	SC_HANDLE service, scm, newService;
	BOOL success;
	SERVICE_STATUS status;
	HANDLE fileHandle;
	DWORD version;
	CHAR configFile[MAX_PATH]; // configuration file
	int rc = 0;

	void getServiceName(char *);

	char serviceName[128];

	memset(serviceUserid, '\0', sizeof(serviceUserid));
	memset(servicePassword, '\0', sizeof(servicePassword));
	strcpy(serviceUserid, DEFAULTID);

	getServiceName(serviceName);

	printf("Using Windows service name \"%s\".\n", &serviceName);

	memset(MQdllDefault, '\0', sizeof(MQdllDefault));
	strcpy(MQdllDefault, "MQIC.DLL");

	// initialize all array members

	memset(Comment, '\0', sizeof(Comment));
	memset(QueueName, '\0', sizeof(QueueName));
	memset(ServiceName, '\0', sizeof(ServiceName));
	memset(QueueMgrName, '\0', sizeof(QueueMgrName));
	memset(NotesIni, '\0', sizeof(NotesIni));
	memset(Conname, '\0', sizeof(Conname));

	memset(Channel, '\0', sizeof(Channel));
	memset(Locladdr, '\0', sizeof(Locladdr));
	memset(RcvData, '\0', sizeof(RcvData));
	memset(ScyData, '\0', sizeof(ScyData));
	memset(SendData, '\0', sizeof(SendData));
	memset(RcvExit, '\0', sizeof(RcvExit));
	memset(ScyExit, '\0', sizeof(ScyExit));
	memset(SendExit, '\0', sizeof(SendExit));
	memset(Userid, '\0', sizeof(Userid));
	memset(Sslciph, '\0', sizeof(Sslciph));
	memset(Sslpeer, '\0', sizeof(Sslpeer));
	memset(Hbint, '\0', sizeof(Hbint));
	memset(Kaint, '\0', sizeof(Kaint));
	memset(ChannelUserId, '\0', sizeof(ChannelUserId));
	memset(ChannelPassword, '\0', sizeof(ChannelPassword));
	memset(mqcdVersion, '\0', sizeof(mqcdVersion));

	// argv[1], if present, is the queue names
	// argv[2], if present, is the qmgr name

	if (argc > 1 && argv[1][0] == '?') {
		printf("%s", USAGE);
		return 4;
	}
	if (argc > 1 && (argv[1][0] == '-' && (strcmp(argv[1], "-f")))) {
		printf("%s", USAGE);
		return 4;
	}
	if (argc > 1 && !strcmp(argv[1], "-f") && argc != 3) {
		printf("%s", USAGE);
		return 4;
	}/* End if*/
	if (argc > 3) {
		printf("%s", USAGE);
		return 4;
	}

	// first, check if we have a configuration file
	if (argc > 1 && !strcmp(argv[1], "-f")) {
		strcpy(configFile, argv[2]);
		sprintf(Comment, "Built from ini file: %s", argv[2]);
		printf("Using configuration file: %s\n", configFile);
	} else if (argc > 1 && !strcmp(argv[2], "-f")) {
		strcpy(configFile, argv[3]);
		sprintf(Comment, "Built from ini file: %s", argv[3]);
		printf("Using configuration file: %s\n", configFile);
	} else {
		configFile[0] = '\0';
		sprintf(Comment, "Built from default configuration");
		printf("Using default configuration");
	}
	if (argc > 1 && !strcmp(argv[1], "-d")) {
		printf("Enabled debug mode\n");
		debug = 1;
	} else if (argc > 3 && !strcmp(argv[3], "-d")) {
		printf("Enabled debug mode\n");
		debug = 1;
	} else {
		debug = 0;
	}
	if (debug) {
		printf("finished parsing arguments");
	}

	if (configFile[0]) { // are we using a configuration file ?
		if (debug) {
			printf("Parsing configuration file: %s\n", configFile);
		}
		rc = processini(configFile);
		if (rc)
			return (rc);

	} else { // queue names on cmd line
		if (debug) {
			printf("Parsing configuration file: %s\n", configFile);
		}
		printf("Parsing comamand line arguments.\n");

		if (argc > 1)
			strcpy(QueueName, argv[1]);
		else
			strcpy(QueueName, DEFAULTQUEUE);
		if (argc > 2)
			strcpy(QueueMgrName, argv[2]);
		else
			strcpy(QueueMgrName, DEFAULTQMGR);
		// put two nulls at the end for multi_sz
		QueueName[strlen(QueueName) + 1] = '\0';
		qindx = strlen(QueueName) + 1;
		QueueMgrName[strlen(QueueMgrName) + 1] = '\0';
		qmgrindx = strlen(QueueMgrName) + 1;
		printf("Using queue \"%s\" and Qmgr \"%s\".\n", &QueueName,
				&QueueMgrName);
		Conname[0] = ' ';
		con_indx = 2;
		niindx = 1;
		chan_indx = 1;
		locl_indx = 1;
		rcvd_indx = 1;
		scyd_indx = 1;
		sndd_indx = 1;
		rcve_indx = 1;
		scye_indx = 1;
		snde_indx = 1;
		user_indx = 1;
		sslc_indx = 1;
		sslp_indx = 1;
		trpt_indx = 1;
		hbin_indx = 1;
		kain_indx = 1;
		mqcd_indx = 1;

	}/* End if*/

	// Get the path we are running under and append program name.
	len = GetCurrentDirectory(MAX_PATH, Path);
	if (!len) {
		ErrorHandler("Unable to get current directory", GetLastError());
	}
	if (debug) {
		printf("Current directory is: %s\n", Path);
	}

	// check that we have the executable in the current directory.

	strcpy(PathProgram, Path);
	strcat(PathProgram, "\\");
	strcat(PathProgram, SERVICEPROGRAM);
	if (debug) {
		printf("\"%s\" built using Path \"%s\" and SERVICEPROGRAM \"%s\".\n",
				serviceName, Path,
				SERVICEPROGRAM);
	}
	fileHandle = CreateFile(PathProgram, 0, FILE_SHARE_WRITE, 0, OPEN_EXISTING,
			0, 0);
	if (fileHandle == INVALID_HANDLE_VALUE) {
		strcpy(strBuf, "Cound not find service executable \"");
		strcat(strBuf, SERVICEPROGRAM);
		strcat(strBuf, "\" in the current directory");
		ErrorHandler(strBuf, GetLastError());
	} else {
		CloseHandle(fileHandle);
		if (debug) {
			printf("Found service executable: %s\n", PathProgram);
		}
	}

	//
	// This segment installs the registry entries for event messages
	//

	// build the path for the new key
	strcpy(strBuf, "SYSTEM\\CurrentControlSet\\");
	strcat(strBuf, "Services\\EventLog\\Application\\");
	strcat(strBuf, serviceName);

	if (debug) {
		printf("Parsing configuration file: %s\n", configFile);
	}
	printf("Creating registry keys for event messages at %s.\n", strBuf);

	// add a key for the new source
	ret = RegCreateKeyEx(HKEY_LOCAL_MACHINE, strBuf, 0, NULL,
	REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &keyHandle, &disposition);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler(
				"Unable to create key: Check and make sure you are a member of Administrators.",
				GetLastError());
	}
	printf("Registry Key created successfully.\n");
	if (disposition == REG_OPENED_EXISTING_KEY) {
		printf("\nUpdating existing Registry information.\n");
	}

	// add the EventMessageFile value to key
	ret = RegSetValueEx(keyHandle, "EventMessageFile", 0, REG_EXPAND_SZ,
			(LPBYTE) PathProgram, strlen(PathProgram) + 1);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key EventMessageFile",
				GetLastError());
	} else if (debug) {
		printf("\nCreated Registry Entry for Event Message File of %s",
				PathProgram);
	}
	// specify the event types supported
	eventTypes = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE
			| EVENTLOG_INFORMATION_TYPE;

	// add the TypesSupported value to key
	ret = RegSetValueEx(keyHandle, "TypesSupported", 0, REG_DWORD,
			(LPBYTE) &eventTypes, sizeof(DWORD));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key TypesSupported",
				GetLastError());
	} else {
		if (debug) {
			printf("Parsing configuration file: %s\n", configFile);
		}

	}

	categoryCount = 0; // change this if other categories are defined

	if (categoryCount) {
		// add the CategoryCount value to key
		ret = RegSetValueEx(keyHandle, "CategoryCount", 0, REG_DWORD,
				(LPBYTE) &categoryCount, sizeof(DWORD));
		if (ret != ERROR_SUCCESS) {
			ErrorHandler("Unable to add value to key CategoryCount",
					GetLastError());

		} else {
			if (debug) {
				printf("Parsing configuration file: %s\n", configFile);
			}

		}
		// add the CategoryMessageFile value to key
		ret = RegSetValueEx(keyHandle, "CategoryMessageFile", 0, REG_EXPAND_SZ,
				(LPBYTE) PathProgram, strlen(PathProgram) + 1);
		if (ret != ERROR_SUCCESS) {
			ErrorHandler("Unable to add value to key CategoryMessageFile",
					GetLastError());
		} else {
			if (debug) {
				printf("Parsing configuration file: %s\n", configFile);
			}

		}
	}

	// close the key
	RegCloseKey(keyHandle);
	//
	// This service creates the service entry in the applet
	//

	// open a connection to the SCM
	scm = OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE);
	if (!scm) {
		ErrorHandler("In OpenScManager", GetLastError());
	} else {
		if (debug) {
			printf("Parsing configuration file: %s\n", configFile);
		}
	}
	//
	// The service might already be installed, try to open it and delete
	// it if it already exists
	//
	// Get the service's handle
	printf("Service name after opening the SCM is %s\n", serviceName);
	service = OpenService(scm, serviceName, SERVICE_ALL_ACCESS | DELETE);
	if (!service) {
		printf("Installing New Service \"%s\"\n", serviceName);
	} else {
		printf("Updating Service\n");
		// Stop the service if necessary
		success = QueryServiceStatus(service, &status);
		if (!success) {
			ErrorHandler("In QueryServiceStatus", GetLastError());
		} else {
			if (debug) {
				printf("Parsing configuration file: %s\n", configFile);
			}

		}
		if (status.dwCurrentState != SERVICE_STOPPED) {
			printf("Stopping service...(this will take awhile)\n");
			success = ControlService(service, SERVICE_CONTROL_STOP, &status);
			if (!success)
				ErrorHandler("In ControlService", GetLastError());
			Sleep(5000);
		}
		// Remove the service
		success = DeleteService(service);
		if (success) {
			printf("%s Service removed\n", serviceName);
		} else {
			ErrorHandler("In DeleteService", GetLastError());
		}
		CloseServiceHandle(service);
		printf("Syncing....\n");
		Sleep(5000);
	} // end of service opened

	if (strcmp(serviceUserid, DEFAULTID)) {

		printf("Creating New Service \"%s\"\n", serviceName);
		// Install the new service using a specified ID
		if (debug) {
			printf("Path to program before creating service is %s.\n",
					PathProgram);
		}
		newService = CreateService(scm, serviceName, serviceName,
		SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START, // when to start
				SERVICE_ERROR_NORMAL, // severity if doesnt start
				PathProgram, 0, // load order group
				0, // ID
				0, // dependencies
				serviceUserid, // service account "domain\account"
				servicePassword // password
				);
		if (!newService) {
			printf(" \"%s\" Failure installing Service, \n\tServiceUserid=%s\n",
					serviceName, serviceUserid);
			printf("Path = %s\n", PathProgram);
			ErrorHandler("In CreateService", GetLastError());
		} else {
			printf(" \"%s\" Service installed, \n\tServiceUserid=%s\n",
					serviceName, serviceUserid);
		}
	} else { // using default ID
		// Install the new service using default ID
		printf("Creating New Service with default userid \"%s\"\n",
				serviceName);
		if (debug) {
			printf("Path to program before creating service is %s.\n",
					PathProgram);
		}
		newService = CreateService(scm, serviceName, serviceName,
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS,
		SERVICE_AUTO_START, // when to start
				SERVICE_ERROR_NORMAL, // severity if doesnt start
				PathProgram, 0, // load order group
				0, // ID
				0, // dependencies
				0, // service account "domain\account"
				0 // password
				);
		if (!newService) {
			printf(" \"%s\" Failure installing Service\n", serviceName);
			printf("Path = %s\n", PathProgram);
			ErrorHandler("In CreateService", GetLastError());
		} else {
			printf(" \"%s\" Service installed. \n", serviceName);
		}

	}
	// clean up
	CloseServiceHandle(newService);
	CloseServiceHandle(scm);
	if (debug) {
		printf("Service handle for service and scm were closed.\n");
	}

	//
	// Lastly, we create / update keys for the queue and qmgr names
	//

	// build the path for the new key
	strcpy(strBuf, KEYPREFIX);
	strcat(strBuf, serviceName);

	// cout << "Service key " << strBuf << endl;

	// add a key for the new source
	printf("Creating registry keys for service at %s.\n", strBuf);
	ret = RegCreateKeyEx(HKEY_LOCAL_MACHINE, strBuf, 0, NULL,
	REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &keyHandle, &disposition);
	if (ret != ERROR_SUCCESS)
		ErrorHandler(
				"Unable to create key: Check and make sure you are a member of Administrators.",
				GetLastError());

	if (disposition == REG_OPENED_EXISTING_KEY)
		printf("Updating existing StartUp information.\n");
	else
		printf("Creating new StartUp information.\n");

	// add the TriggerQueueName value to key
	ret = RegSetValueEx(keyHandle, TRIGGERQUEUENAME, 0, REG_MULTI_SZ,
			(LPBYTE) &QueueName, qindx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for Trigger Queue Name",
				GetLastError());
	} else {
		if (0 != &QueueName )
		if (debug) {
			printf("Trigger Queue Name added to Registry.\n");
		}
	}
	ret = RegSetValueEx(keyHandle, COMMENT, 0, REG_MULTI_SZ, (LPBYTE) &Comment,
			strlen(Comment));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key Comment",
				GetLastError());
	} else {
		if (0 != &Comment )
		if (debug) {
			printf("Comment field added to Registry.\n");
		}
	}

	ret = RegSetValueEx(keyHandle, SERVICENAME, 0, REG_MULTI_SZ,
			(LPBYTE) &ServiceName, sindx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for Service Name",
				GetLastError());
	} else {
		if (0 != &ServiceName )
		if (debug) {
			printf("Service  Name added to Registry.\n");
		}
	}

	// add the TriggerQueueQmrName value to key
	ret = RegSetValueEx(keyHandle, TRIGGERQUEUEMGRNAME, 0, REG_MULTI_SZ,
			(LPBYTE) &QueueMgrName, qmgrindx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler(
				"Unable to add value to key for Trigger Queue Manager Name",
				GetLastError());
	} else {
		if (0 != &QueueMgrName )
			if (debug) {
				printf("Trigger Queue Manager Name added to Registry.\n");
			}
		}

	// add the NotesIni value to key
	ret = RegSetValueEx(keyHandle, NOTESINI, 0, REG_MULTI_SZ,
			(LPBYTE) &NotesIni, niindx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for Notes INI",
				GetLastError());
	} else {
		if (0 != &NotesIni )
			if (debug) {
				printf("Notes INI added to Registry.\n");
			}
		}

	// add the conName value to key
	ret = RegSetValueEx(keyHandle, CONNAME, 0, REG_MULTI_SZ, (LPBYTE) &Conname,
			con_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for CONNAME", GetLastError());

	} else {
		if (0 != &Conname )
			if (debug) {
				printf("Connection Name added to Registry.\n");
			}
		}
	// add the Channel value to key
	ret = RegSetValueEx(keyHandle, CHANNEL, 0, REG_MULTI_SZ, (LPBYTE) &Channel,
			chan_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for CHANNEL", GetLastError());
	} else {
		if (0 != &Channel )
			if (debug) {
				printf("Channel added to Registry.\n");
			}
		}

	// add the LoclAddr value to key
	ret = RegSetValueEx(keyHandle, LOCLADDR, 0, REG_MULTI_SZ,
			(LPBYTE) &Locladdr, locl_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for Local Address",
				GetLastError());
	} else {
		if (0 != &Locladdr )
			if (debug) {
				printf("Local Address added to Registry.\n");
			}
		}

	// add the RcvData value to key
	ret = RegSetValueEx(keyHandle, RCVDATA, 0, REG_MULTI_SZ, (LPBYTE) &RcvData,
			rcvd_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for RCVDATA", GetLastError());
	} else {
		if (0 != &RcvData )
			if (debug) {
				printf("RCVDATA added to Registry.\n");
			}
		}

	// add the ScyData value to key
	ret = RegSetValueEx(keyHandle, SCYDATA, 0, REG_MULTI_SZ, (LPBYTE) &ScyData,
			scyd_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SCYDATA", GetLastError());
	} else {
		if (0 != &ScyData )
			if (debug) {
				printf("SCYDATA added to Registry.\n");
			}
		}

	// add the SendData value to key
	ret = RegSetValueEx(keyHandle, SENDDATA, 0, REG_MULTI_SZ,
			(LPBYTE) &SendData, sndd_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SENDDATA", GetLastError());
	} else {
		if (0 != &SendData )
			if (debug) {
				printf("SENDDATA added to Registry.\n");
			}
		}

	// add the RcvExit value to key
	ret = RegSetValueEx(keyHandle, RCVEXIT, 0, REG_MULTI_SZ, (LPBYTE) &RcvExit,
			rcve_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for RCVEXIT", GetLastError());
	} else {
		if (0 != &RcvExit )
			if (debug) {
				printf("RCVEXIT added to Registry.\n");
			}
		}

	// add the ScyExit value to key
	ret = RegSetValueEx(keyHandle, SCYEXIT, 0, REG_MULTI_SZ, (LPBYTE) &ScyExit,
			scye_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SCYEXIT", GetLastError());
	} else {
		if (0 != &ScyExit )
			if (debug) {
				printf("SCYEXIT added to Registry.\n");
			}
		}

	// add the SendExit value to key
	ret = RegSetValueEx(keyHandle, SENDEXIT, 0, REG_MULTI_SZ,
			(LPBYTE) &SendExit, snde_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SENDEXIT", GetLastError());
	} else {
		if (0 != &SendExit )
			if (debug) {
				printf("SENDEXIT added to Registry.\n");
			}
		}

	// add the Userid value to key
	ret = RegSetValueEx(keyHandle, USERID, 0, REG_MULTI_SZ, (LPBYTE) &Userid,
			user_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for USERID", GetLastError());
	} else {
		if (0 != &Userid )
			if (debug) {
				printf("USERID added to Registry.\n");
			}
		}

	// add the Sslpeer value to key
	ret = RegSetValueEx(keyHandle, SSLPEER, 0, REG_MULTI_SZ, (LPBYTE) &Sslpeer,
			sslp_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SSLPEER", GetLastError());
	} else {
		if (0 != &Sslpeer )
			if (debug) {
				printf("SSLPEER added to Registry.\n");
			}
		}

	// add the Sslciph value to key
	ret = RegSetValueEx(keyHandle, SSLCIPH, 0, REG_MULTI_SZ, (LPBYTE) &Sslciph,
			sslc_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SSLCIPH", GetLastError());
	} else {
		if (0 != &Sslciph )
			if (debug) {
				printf("Trigger Queue Name added to Registry.\n");
			}
		}

	// add the Trptype value to key
	// ret=RegSetValueEx(keyHandle, TRPTYPE, 0, REG_MULTI_SZ, (LPBYTE) &Trptype, trpt_indx);
	//  if (ret != ERROR_SUCCESS) ErrorHandler("Unable to add value to key", GetLastError());

	// add the Hbint value to key
	ret = RegSetValueEx(keyHandle, HBINT, 0, REG_MULTI_SZ, (LPBYTE) &Hbint,
			hbin_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for HBINT", GetLastError());
	} else {
		if (0 != &Hbint )
			if (debug) {
				printf("HBINT added to Registry.\n");
			}
		}

	// add the Kaint value to key
	ret = RegSetValueEx(keyHandle, KAINT, 0, REG_MULTI_SZ, (LPBYTE) &Kaint,
			kain_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for KAINT", GetLastError());
	} else {
		if (0 != &Kaint )
			if (debug) {
				printf("KAINT added to Registry.\n");
			}
		}
	// add the Channel Username value to key
	ret = RegSetValueEx(keyHandle, CHANNELUID, 0, REG_MULTI_SZ,
			(LPBYTE) &ChannelUserId, chlui_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for CHANNELUID",
				GetLastError());
	} else {
		if (0 != &ChannelUserId )
			if (debug) {
				printf("Channel User ID added to Registry.\n");
			}
		}

	ret = RegSetValueEx(keyHandle, CHANNELPW, 0, REG_MULTI_SZ,
			(LPBYTE) &ChannelPassword, chlpw_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for CHANNELPW",
				GetLastError());
	} else {
		if (0 != &ChannelPassword )
			if (debug) {
				printf("Channel Password added to Registry.\n");
			}
		}

	// add the mqcdVersion value to key
	ret = RegSetValueEx(keyHandle, MA7K_MQCD_VERSION, 0, REG_MULTI_SZ,
			(LPBYTE) &mqcdVersion, mqcd_indx);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for MA7K_MQCD_VERSION",
				GetLastError());
	} else {
		if (0 != &mqcdVersion )
			if (debug) {
				printf("MA7K MQCD Version added to Registry.\n");
			}
		}

	// add the WaitInterval value to key
	ret = RegSetValueEx(keyHandle, WAITINTERVAL, 0, REG_DWORD,
			(LPBYTE) &WaitIntervalDefault, sizeof(WaitIntervalDefault));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for Wait Interval",
				GetLastError());
	} else {
		if (0 != &WaitIntervalDefault )
			if (debug) {
				printf("Wait Interval added to Registry.\n");
			}
		}

	// add the LongRty value to key
	ret = RegSetValueEx(keyHandle, LONGRTY, 0, REG_DWORD,
			(LPBYTE) &LongRtyDefault, sizeof(LongRtyDefault));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key LONGRTY", GetLastError());
	} else {
		if (0 != &LongRtyDefault )
			if (debug) {
				printf("LONGRTY added to Registry.\n");
			}
		}

	// add the LongTmr value to key
	ret = RegSetValueEx(keyHandle, LONGTMR, 0, REG_DWORD,
			(LPBYTE) &LongTmrDefault, sizeof(LongTmrDefault));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key LONGTMR", GetLastError());
	} else {
		if (0 != &LongTmrDefault )
			if (debug) {
				printf("LONGTMR added to Registry.\n");
			}
		}

	// add the ShortRty value to key
	ret = RegSetValueEx(keyHandle, SHORTRTY, 0, REG_DWORD,
			(LPBYTE) &ShortRtyDefault, sizeof(ShortRtyDefault));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SHORTRTY", GetLastError());
	} else {
		if (0 != &ShortRtyDefault )
			if (debug) {
				printf("SHORTRTY added to Registry.\n");
			}
		}

	// add the ShortTmr value to key
	ret = RegSetValueEx(keyHandle, SHORTTMR, 0, REG_DWORD,
			(LPBYTE) &ShortTmrDefault, sizeof(ShortTmrDefault));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for SHORTTMR", GetLastError());
	} else {
		if (0 != &ShortTmrDefault )
			if (debug) {
				printf("SHORTTMR added to Registry.\n");
			}
		}

	// add the EventLevel value to key
	ret = RegSetValueEx(keyHandle, EVENTLEVEL, 0, REG_DWORD,
			(LPBYTE) &EventLevelDefault, sizeof(EventLevelDefault));
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for EVENTLEVEL",
				GetLastError());
	} else {
		if (0 != &EventLevelDefault )
			if (debug) {
				printf("Event Level added to Registry.\n");
			}
		}

	// add the mqseries dll name to key
	ret = RegSetValueEx(keyHandle, MQDLL, 0, REG_SZ, (LPBYTE) &MQdllDefault,
			strlen(MQdllDefault) + 1);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for MQDLL", GetLastError());
	} else {
		if (0 != &MQdllDefault )
			if (debug) {
				printf("MQ DLL path %s added to Registry.\n",MQdllDefault);
			}
		}

	// add the Notes agent redir output key
	ret = RegSetValueEx(keyHandle, AGENTREDIR, 0, REG_SZ,
			(LPBYTE) &AgentRedirDefault, strlen(AgentRedirDefault) + 1);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for AGENTREDIR",
				GetLastError());
	} else {
		if (0 != &AgentRedirDefault )
			if (debug) {
				printf("AGENTREDIR added to Registry.\n");
			}
		}

	// add the KeyRepository output key
	ret = RegSetValueEx(keyHandle, KEYREPOS, 0, REG_SZ, (LPBYTE) &keyRepos,
			strlen(keyRepos) + 1);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for KEYREPOS", GetLastError());
	} else {
		if (0 != &keyRepos )
			if (debug) {
				printf("KEYREPOS added to Registry.\n");
			}
		}

	// add our path key
	ret = RegSetValueEx(keyHandle, MYEXEPATH, 0, REG_SZ, (LPBYTE) &Path,
			strlen(Path) + 1);
	if (ret != ERROR_SUCCESS) {
		ErrorHandler("Unable to add value to key for MYEXEPATH",
				GetLastError());
	} else {
		if (0 != &Path )
			if (debug) {
				printf("MYEXEPATH added to Registry.\n");
			}
		}

	// close the key
	RegCloseKey(keyHandle);

	return 0;
}

// ************************************** process ini file if specified **************************
int processini(char * configFile) {

	struct {
		char q[MQ_Q_NAME_LENGTH + 1];
		char qm[MQ_Q_MGR_NAME_LENGTH + 1];
		char service[MQ_SERVICE_NAME_LENGTH];
		char ni[MAX_PATH + 1];
		char conname[MQ_CONN_NAME_LENGTH + 1];
		CHAR channel[MQ_CHANNEL_NAME_LENGTH + 1];
		CHAR locladdr[MQ_LOCAL_ADDRESS_LENGTH + 1];
		CHAR rcvData[MQ_EXIT_DATA_LENGTH + 1];
		CHAR scyData[MQ_EXIT_DATA_LENGTH + 1];
		CHAR sendData[MQ_EXIT_DATA_LENGTH + 1];
		CHAR rcvExit[MQ_EXIT_NAME_LENGTH + 1];
		CHAR scyExit[MQ_EXIT_NAME_LENGTH + 1];
		CHAR sendExit[MQ_EXIT_NAME_LENGTH + 1];
		CHAR userid[MQ_USER_ID_LENGTH + 1];
		CHAR sslciph[MQ_SSL_CIPHER_SPEC_LENGTH + 1];
		CHAR sslpeer[MQ_SSL_PEER_NAME_LENGTH + 1];
		// CHAR  trptype[(10)+1];
		CHAR hbint[(10) + 1];
		CHAR kaint[(10) + 1];
		CHAR channelUserId[(1024) + 1];
		CHAR channelPassword[(MQ_CSP_PASSWORD_LENGTH * 4) + 1];
		CHAR mqcdversion[(3) + 1];

	} t[MAXTHREADS];

	FILE *fp; // file ptr to config file
	CHAR buf[MAX_PATH];
	int i = 0;
	int rlen = 0;
	int qlen = 0;
	int qmlen = 0;
	int conlen = 0;
	int nilen = 0;
	char * pEndBuff = NULL;
	char * pStrBuff = NULL;
	char * tempPw = NULL;
	int inGlobal = 0; // flag to tell us we are in global section
	int inThread = 0; // flag to tell use we are in thread section
	int inService = 0; // flag to tell use we are in thread section
	int rc = 0;
	int recordsread = 0; // number of "thread" stanza's
	int servicesread = 0; // number of "thread" stanza's
	int servUseridSpecified = 0;
	int servPasswordSpecified = 0;
	int ln = 0;

	recordsread = 0; // number of thread stanza's
	memset(&t, '\0', sizeof(t));

	tempPw = (char*) malloc(
			MQ_CSP_PASSWORD_LENGTH + MQ_CSP_PASSWORD_LENGTH + 1);
	memset(tempPw, '\0', sizeof(tempPw));
	if (!(fp = fopen(configFile, "r"))) {
		printf("!!!! Failed to open configuration file %s\n\n", configFile);
		return (1);
	} // end if
	printf("Opened configuration file %s\n", configFile);

	while (fgets(buf, sizeof(buf), fp)) {
		++ln;
		if (debug) {
			printf("Reading line #%d\n", ln);
		}
		rlen = strlen(buf); // get the record length
		if (*(buf + rlen - 1) == '\n')
			rlen--; // strip the newline

		// check to see if its ALL blanks .... or a comment
		for (i = 0; i < rlen - 1 && *(buf + i) == ' '; ++i)
			;

		if (debug) {
			printf("Buff is currently %c and rlen is %d\n",buf[0],rlen);
		}

		if (*buf != '*' && i < rlen - 1) {

			// we allow extra blank space (or tabs) in the front and end of the string, so now we must strip it off
			pStrBuff = buf;
			pEndBuff = buf + rlen - 1;
			while ((*pStrBuff == ' ' || *pStrBuff == '\t')
					&& pStrBuff < pEndBuff)
				pStrBuff++; // scan to first non-blank
			while ((*pEndBuff == ' ' || *pEndBuff == '\t')
					&& pStrBuff < pEndBuff)
				pEndBuff--; // scan to first non-blank

			rlen = pEndBuff - pStrBuff + 1;
			memcpy(buf, pStrBuff, rlen);

			if (!memcmp(buf, "Global:", 7)) {
				printf("Found Global parameters section file\n");
				inGlobal = 1;
				inThread = 0;
				inService = 0;
				continue;
			} else if (!memcmp(buf, "Thread:", 7)) {
				printf("Found a Thread stanza :%s.\n", buf);
				inGlobal = 0;
				inThread = 1;
				inService = 0;
				if (recordsread++ == MAXTHREADS) {
					printf("Too many stanzas encountered, maximum is %d\n",
					MAXTHREADS);
					rc = 4;
					break;
				}
				continue;
			} else if (!memcmp(buf, "Service:", 8)) {
				printf("Found a Service stanza :%s.\n", buf);
				inGlobal = 0;
				inThread = 0;
				inService = 1;
				if (recordsread++ == MAXTHREADS) {
					printf("Too many stanzas encountered, maximum is %d\n",
					MAXTHREADS);
					rc = 4;
					break;
				}
				continue;
			} else if (!inGlobal && !inThread && !inService) {
				printf(
						"Invalid statement: %s\n (note that keywords are case sensitive)\n",
						buf);
				rc = 3;
				break;
			}
			//
			// in the Global stanza ....
			//
			if (inGlobal) {
				printf("Processing Global keyword :%s.\n", buf);
				if (!memcmp(buf, "ShortTmr=", 9))
					ShortTmrDefault = atoi(buf + 9);
				else if (!memcmp(buf, "ShortRty=", 9))
					ShortRtyDefault = atoi(buf + 9);
				else if (!memcmp(buf, "LongTmr=", 8))
					LongTmrDefault = atoi(buf + 8);
				else if (!memcmp(buf, "LongRty=", 8))
					LongRtyDefault = atoi(buf + 8);
				else if (!memcmp(buf, "EventLevel=", 11))
					EventLevelDefault = atoi(buf + 11);
				else if (!memcmp(buf, "MQSeriesDLL=", 12)) {
					memset(MQdllDefault, '\0', sizeof(MQdllDefault));
					memcpy(MQdllDefault, buf + 12, rlen - 12);
				} else if (!memcmp(buf, "AgentRedirStdout=", 17)) {
					memset(AgentRedirDefault, '\0', sizeof(AgentRedirDefault));
					memcpy(AgentRedirDefault, buf + 17, rlen - 17);
				} else if (!memcmp(buf, "WaitInterval=", 13))
					WaitIntervalDefault = atoi(buf + 13);
				else if (!memcmp(buf, "KeyRepository=", 14)) {
					memset(keyRepos, '\0', sizeof(keyRepos));
					memcpy(keyRepos, buf + 14, rlen - 14);
				} else if (!memcmp(buf, "ServiceUserid=", 14)) {
					memset(serviceUserid, '\0', sizeof(serviceUserid));
					memcpy(serviceUserid, buf + 14, rlen - 14);
					servUseridSpecified = 1;
				} else if (!memcmp(buf, "ServicePassword=", 16)) {
					memset(servicePassword, '\0', sizeof(servicePassword));
					memcpy(servicePassword, buf + 16, rlen - 16);
					servPasswordSpecified = 1;
				} else {
					printf(
							"Invalid statement: %s\n (note that keywords are case sensitive)\n",
							buf);
					rc = 3;
					break;
				}
				if (debug) {
					printf("Successfully read Global keyword :%s.\n", buf);
				}
			}
			//
			// In the Thread stanza ...
			//
			if (inThread) {

				printf("Processing Thread keyword :%s.\n", buf);
				if (!memcmp(buf, TRIGGERQUEUENAME, strlen(TRIGGERQUEUENAME))
						&& *(buf + strlen(TRIGGERQUEUENAME)) == '=') {
					strncpy(t[recordsread - 1].q,
							buf + (strlen(TRIGGERQUEUENAME) + 1),
							rlen - (strlen(TRIGGERQUEUENAME) + 1));
					printf("found queue name = %s\n", t[recordsread - 1].q);
					memset(t[recordsread - 1].service, '\0',
							MQ_SERVICE_NAME_LENGTH);
					t[recordsread - 1].service[0] = PLACEHOLDER;
				} else if (!memcmp(buf, "TriggerQueueMgrName=", 20)) {
					strncpy(t[recordsread - 1].qm, buf + 20, rlen - 20);
					printf("found qmgr name = %s\n", t[recordsread - 1].qm);
				} else if (!memcmp(buf, "NotesIni=", 9)) {
					strncpy(t[recordsread - 1].ni, buf + 9, rlen - 9);
				}

				/**** v140 stuff here .... */
				else if (!memcmp(buf, CONNAME, strlen(CONNAME))
						&& *(buf + strlen(CONNAME)) == '=') {
					strncpy(t[recordsread - 1].conname,
							buf + (strlen(CONNAME) + 1),
							rlen - (strlen(CONNAME) + 1));
				} else if (!memcmp(buf, CHANNEL, strlen(CHANNEL))
						&& *(buf + strlen(CHANNEL)) == '=') {
					strncpy(t[recordsread - 1].channel,
							buf + (strlen(CHANNEL) + 1),
							rlen - (strlen(CHANNEL) + 1));
				} else if (!memcmp(buf, LOCLADDR, strlen(LOCLADDR))
						&& *(buf + strlen(LOCLADDR)) == '=') {
					strncpy(t[recordsread - 1].locladdr,
							buf + (strlen(LOCLADDR) + 1),
							rlen - (strlen(LOCLADDR) + 1));
				} else if (!memcmp(buf, RCVDATA, strlen(RCVDATA))
						&& *(buf + strlen(RCVDATA)) == '=') {
					strncpy(t[recordsread - 1].rcvData,
							buf + (strlen(RCVDATA) + 1),
							rlen - (strlen(RCVDATA) + 1));
				} else if (!memcmp(buf, SCYDATA, strlen(SCYDATA))
						&& *(buf + strlen(SCYDATA)) == '=') {
					strncpy(t[recordsread - 1].scyData,
							buf + (strlen(SCYDATA) + 1),
							rlen - (strlen(SCYDATA) + 1));
				} else if (!memcmp(buf, SENDDATA, strlen(SENDDATA))
						&& *(buf + strlen(SENDDATA)) == '=') {
					strncpy(t[recordsread - 1].sendData,
							buf + (strlen(SENDDATA) + 1),
							rlen - (strlen(SENDDATA) + 1));
				} else if (!memcmp(buf, RCVEXIT, strlen(RCVEXIT))
						&& *(buf + strlen(RCVEXIT)) == '=') {
					strncpy(t[recordsread - 1].rcvExit,
							buf + (strlen(RCVEXIT) + 1),
							rlen - (strlen(RCVEXIT) + 1));
				} else if (!memcmp(buf, SCYEXIT, strlen(SCYEXIT))
						&& *(buf + strlen(SCYEXIT)) == '=') {
					strncpy(t[recordsread - 1].scyExit,
							buf + (strlen(SCYEXIT) + 1),
							rlen - (strlen(SCYEXIT) + 1));
				} else if (!memcmp(buf, SENDEXIT, strlen(SENDEXIT))
						&& *(buf + strlen(SENDEXIT)) == '=') {
					strncpy(t[recordsread - 1].sendExit,
							buf + (strlen(SENDEXIT) + 1),
							rlen - (strlen(SENDEXIT) + 1));
				} else if (!memcmp(buf, USERID, strlen(USERID))
						&& *(buf + strlen(USERID)) == '=') {
					strncpy(t[recordsread - 1].userid,
							buf + (strlen(USERID) + 1),
							rlen - (strlen(USERID) + 1));
				} else if (!memcmp(buf, SSLCIPH, strlen(SSLCIPH))
						&& *(buf + strlen(SSLCIPH)) == '=') {
					strncpy(t[recordsread - 1].sslciph,
							buf + (strlen(SSLCIPH) + 1),
							rlen - (strlen(SSLCIPH) + 1));
				} else if (!memcmp(buf, SSLPEER, strlen(SSLPEER))
						&& *(buf + strlen(SSLPEER)) == '=') {
					strncpy(t[recordsread - 1].sslpeer,
							buf + (strlen(SSLPEER) + 1),
							rlen - (strlen(SSLPEER) + 1));
				}

				else if (!memcmp(buf, HBINT, strlen(HBINT))
						&& *(buf + strlen(HBINT)) == '=') {
					strncpy(t[recordsread - 1].hbint, buf + (strlen(HBINT) + 1),
							rlen - (strlen(HBINT) + 1));
				} else if (!memcmp(buf, KAINT, strlen(KAINT))
						&& *(buf + strlen(KAINT)) == '=') {
					strncpy(t[recordsread - 1].kaint, buf + (strlen(KAINT) + 1),
							rlen - (strlen(KAINT) + 1));
				} else if (!memcmp(buf, CHANNELUID, strlen(CHANNELUID))
						&& *(buf + strlen(CHANNELUID)) == '=') {
					strncpy(t[recordsread - 1].channelUserId,
							buf + (strlen(CHANNELUID) + 1),
							rlen - (strlen(CHANNELUID) + 1));
					//	prompt for password!
					memset(tempPw, '\0', sizeof(tempPw));
					printf("Enter the password for Channel User ID %s:\n",
							t[recordsread - 1].channelUserId);
					HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
					DWORD mode = 0;
					GetConsoleMode(hStdin, &mode);
					SetConsoleMode(hStdin, mode & (~ENABLE_ECHO_INPUT));
					gets(tempPw);
					SetConsoleMode(hStdin, mode & (ENABLE_ECHO_INPUT));
					char* tempVal;
					tempVal = encryptData(tempPw);
					strcpy(t[recordsread - 1].channelPassword, tempVal);
				} else if (!memcmp(buf, MA7K_MQCD_VERSION,
						strlen(MA7K_MQCD_VERSION))
						&& *(buf + strlen(MA7K_MQCD_VERSION)) == '=') {
					strncpy(t[recordsread - 1].mqcdversion,
							buf + (strlen(MA7K_MQCD_VERSION) + 1),
							rlen - (strlen(MA7K_MQCD_VERSION) + 1));
				}

				else {
					printf(
							"Invalid statement: %s\n (note that keywords are case sensitive)",
							buf);
					rc = 3;
					break;
				}
				if (debug) {
					printf("Successfully read Thread keyword :%s.\n", buf);
				}
			}
			if (inService) {
				printf("Processing Service keyword :%s.\n", buf);
				if (!memcmp(buf, SERVICENAME, strlen(SERVICENAME))
						&& *(buf + strlen(SERVICENAME)) == '=') {

					strncpy(t[recordsread - 1].service,
							buf + (strlen(SERVICENAME) + 1),
							rlen - (strlen(SERVICENAME) + 1));
					printf("found service name = %s\n",
							t[recordsread - 1].service);
					memset(t[recordsread - 1].q, '\0', MQ_Q_NAME_LENGTH);
					t[recordsread - 1].q[0] = PLACEHOLDER;

				} else if (!memcmp(buf, SERVICEQUEUEMGRNAME,
						strlen(SERVICEQUEUEMGRNAME))
						&& *(buf + strlen(SERVICEQUEUEMGRNAME)) == '=') {
					strncpy(t[recordsread - 1].qm,
							buf + (strlen(SERVICEQUEUEMGRNAME) + 1),
							rlen - (strlen(SERVICEQUEUEMGRNAME) + 1));
				} else if (!memcmp(buf, CONNAME, strlen(CONNAME))
						&& *(buf + strlen(CONNAME)) == '=') {
					strncpy(t[recordsread - 1].conname,
							buf + (strlen(CONNAME) + 1),
							rlen - (strlen(CONNAME) + 1));
				} else if (!memcmp(buf, CHANNEL, strlen(CHANNEL))
						&& *(buf + strlen(CHANNEL)) == '=') {
					strncpy(t[recordsread - 1].channel,
							buf + (strlen(CHANNEL) + 1),
							rlen - (strlen(CHANNEL) + 1));
				} else if (!memcmp(buf, LOCLADDR, strlen(LOCLADDR))
						&& *(buf + strlen(LOCLADDR)) == '=') {
					strncpy(t[recordsread - 1].locladdr,
							buf + (strlen(LOCLADDR) + 1),
							rlen - (strlen(LOCLADDR) + 1));
				} else if (!memcmp(buf, RCVDATA, strlen(RCVDATA))
						&& *(buf + strlen(RCVDATA)) == '=') {
					strncpy(t[recordsread - 1].rcvData,
							buf + (strlen(RCVDATA) + 1),
							rlen - (strlen(RCVDATA) + 1));
				} else if (!memcmp(buf, SCYDATA, strlen(SCYDATA))
						&& *(buf + strlen(SCYDATA)) == '=') {
					strncpy(t[recordsread - 1].scyData,
							buf + (strlen(SCYDATA) + 1),
							rlen - (strlen(SCYDATA) + 1));
				} else if (!memcmp(buf, SENDDATA, strlen(SENDDATA))
						&& *(buf + strlen(SENDDATA)) == '=') {
					strncpy(t[recordsread - 1].sendData,
							buf + (strlen(SENDDATA) + 1),
							rlen - (strlen(SENDDATA) + 1));
				} else if (!memcmp(buf, RCVEXIT, strlen(RCVEXIT))
						&& *(buf + strlen(RCVEXIT)) == '=') {
					strncpy(t[recordsread - 1].rcvExit,
							buf + (strlen(RCVEXIT) + 1),
							rlen - (strlen(RCVEXIT) + 1));
				} else if (!memcmp(buf, SCYEXIT, strlen(SCYEXIT))
						&& *(buf + strlen(SCYEXIT)) == '=') {
					strncpy(t[recordsread - 1].scyExit,
							buf + (strlen(SCYEXIT) + 1),
							rlen - (strlen(SCYEXIT) + 1));
				} else if (!memcmp(buf, SENDEXIT, strlen(SENDEXIT))
						&& *(buf + strlen(SENDEXIT)) == '=') {
					strncpy(t[recordsread - 1].sendExit,
							buf + (strlen(SENDEXIT) + 1),
							rlen - (strlen(SENDEXIT) + 1));
				} else if (!memcmp(buf, USERID, strlen(USERID))
						&& *(buf + strlen(USERID)) == '=') {
					strncpy(t[recordsread - 1].userid,
							buf + (strlen(USERID) + 1),
							rlen - (strlen(USERID) + 1));
				} else if (!memcmp(buf, SSLCIPH, strlen(SSLCIPH))
						&& *(buf + strlen(SSLCIPH)) == '=') {
					strncpy(t[recordsread - 1].sslciph,
							buf + (strlen(SSLCIPH) + 1),
							rlen - (strlen(SSLCIPH) + 1));
				} else if (!memcmp(buf, SSLPEER, strlen(SSLPEER))
						&& *(buf + strlen(SSLPEER)) == '=') {
					strncpy(t[recordsread - 1].sslpeer,
							buf + (strlen(SSLPEER) + 1),
							rlen - (strlen(SSLPEER) + 1));
				}
				else if (!memcmp(buf, HBINT, strlen(HBINT))
						&& *(buf + strlen(HBINT)) == '=') {
					strncpy(t[recordsread - 1].hbint, buf + (strlen(HBINT) + 1),
							rlen - (strlen(HBINT) + 1));
				} else if (!memcmp(buf, KAINT, strlen(KAINT))
						&& *(buf + strlen(KAINT)) == '=') {
					strncpy(t[recordsread - 1].kaint, buf + (strlen(KAINT) + 1),
							rlen - (strlen(KAINT) + 1));
				} else if (!memcmp(buf, MA7K_MQCD_VERSION,
						strlen(MA7K_MQCD_VERSION))
						&& *(buf + strlen(MA7K_MQCD_VERSION)) == '=') {
					strncpy(t[recordsread - 1].mqcdversion,
							buf + (strlen(MA7K_MQCD_VERSION) + 1),
							rlen - (strlen(MA7K_MQCD_VERSION) + 1));
				} else if (!memcmp(buf, CHANNELUID, strlen(CHANNELUID))
						&& *(buf + strlen(CHANNELUID)) == '=') {
					strncpy(t[recordsread - 1].channelUserId,
							buf + (strlen(CHANNELUID) + 1),
							rlen - (strlen(CHANNELUID) + 1));
					//	prompt for password!
					memset(tempPw, '\0', sizeof(tempPw));
					printf("Enter the password for Channel User ID %s:\n",
							t[recordsread - 1].channelUserId);
					HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
					DWORD mode = 0;
					GetConsoleMode(hStdin, &mode);
					SetConsoleMode(hStdin, mode & (~ENABLE_ECHO_INPUT));
					gets(tempPw);
					SetConsoleMode(hStdin, mode & (ENABLE_ECHO_INPUT));
					char* tempVal;
					tempVal = encryptData(tempPw);
					strcpy(t[recordsread - 1].channelPassword, tempVal);
				} else {
					printf(
							"Invalid statement: %s\n (note that keywords are case sensitive)",
							buf);
					rc = 3;
					break;
				}
				if (debug) {
					printf("Successfully read Service keyword :%s.\n", buf);
				}
			}
		}/* End if its not a comment */
	} // end while

	fclose(fp);
	if (rc)
		return (rc);
	printf("Processing all keywords, now validating values");
	// Now, lets check that no-zero vaules have been specified, could indicate an atoi failure ..

	if (!ShortTmrDefault) {
		printf("Invalid ShortTmr value, cannot be zero or non-numeric\n\n");
		rc = 2;
	}
	if (!ShortRtyDefault) {
		printf("Invalid ShortRty value, cannot be zero or non-numeric\n\n");
		rc = 2;
	}
	if (!LongTmrDefault) {
		printf("Invalid LongTmr value, cannot be zero or non-numeric\n\n");
		rc = 2;
	}
	if (!LongRtyDefault) {
		printf("Invalid LongRty value, cannot be zero or non-numeric\n\n");
		rc = 2;
	}
	if (!EventLevelDefault) {
		printf("Invalid EventLevel value, cannot be zero or non-numeric\n\n");
		rc = 2;
	}
	if (!WaitIntervalDefault) {
		printf("Invalid WaitInterval value, cannot be zero or non-numeric\n\n");
		rc = 2;
	}
	if (rc)
		return (rc);

	// There has to be at least one thread stanza ....
	if (!recordsread) {
		printf(
				"No Thread or Service stanzas found, there must be at least one Thread stanza\n");
		return (5);
	}

	printf("ShortTmr %d, ShortRty %d, LongTmr %d, LongRty %d\n",
			ShortTmrDefault, ShortRtyDefault, LongTmrDefault, LongRtyDefault);
	printf("EventLevel %d, WaitInterval %d, MQSeriesDLL \"%s\"\n",
			EventLevelDefault, WaitIntervalDefault, MQdllDefault);

	printf(
			"Checking if we need to ask for Windows Service password - userid flag %d password flag %d\n",
			servUseridSpecified, servPasswordSpecified);

	if (servUseridSpecified && !servPasswordSpecified) {
		printf(
				"Please enter the service password (the password will NOT be displayed as you type!!!)\n");
		HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
		DWORD mode = 0;
		GetConsoleMode(hStdin, &mode);
		SetConsoleMode(hStdin, mode & (~ENABLE_ECHO_INPUT));

		fgets(servicePassword, sizeof(servicePassword), stdin);
		SetConsoleMode(hStdin, mode & (ENABLE_ECHO_INPUT));
		// change the \n to \0
		printf("Service password successfully entered!!!)\n");
		servicePassword[strlen(servicePassword) - 1] = '\0';
	}

	printf(
			"Validated keywords, about to build arrays for multi value registry entries\n");

	qindx = 0; // length of q-buffer
	sindx = 0; // length of service-buffer
	qmgrindx = 0; // length of qmgr-ubffer
	niindx = 0; // length of notes ini file name buffer

	//
	// In this section, we build up the multi-sz strings that will be set into the registry ...
	//
	for (i = 0; i < recordsread; i++) {

		if (!(t[i].q[0]) && !(t[i].q[0] == PLACEHOLDER)) {
			printf(
					"Error, a Thread stanza was found which did not name an initiation queue\n");
			return (2);
		} else {
			printf("\nThread %d, TriggerQueueName=\"%s\"\n", i, t[i].q);
		}
		if (!(t[i].service[0]) && !(t[i].service[0] == PLACEHOLDER)) {
			printf(
					"Error, a Service stanza was found which did not name the service\n");
			return (2);
		} else {
			if (!(t[i].service[0] == PLACEHOLDER)) {
				printf("\nService %d, ServiceName=\"%s\"\n", i, t[i].service);
			}
		}
		if (t[i].qm[0]) {
			printf("          TriggerQueueMgrName=\"%s\"\n", t[i].qm);
		}
		if (t[i].ni[0]) {
			printf("          NotesIni=\"%s\"\n", t[i].ni);
		}
		if (t[i].channel[0] && !t[i].conname[0]) {
			printf("\n CHANNEL specfied but no CONNAME specified\n");
			return (2);
		}
		if (t[i].conname[0]) {
			if (t[i].channel[0]) {
				printf("          CONNAME=\"%.56s\"\n", t[i].conname);
				printf("          CHANNEL=\"%s\"\n", t[i].channel);
				printf("          LOCLADDR=\"%s\"\n", t[i].locladdr);
				printf("          RCVDATA=\"%s\"\n", t[i].rcvData);
				printf("          SCYDATA=\"%s\"\n", t[i].scyData);
				printf("          SENDDATA=\"%s\"\n", t[i].sendData);
				printf("          RCVEXIT=\"%s\"\n", t[i].rcvExit);
				printf("          SCYEXIT=\"%s\"\n", t[i].scyExit);
				printf("          SENDEXIT=\"%s\"\n", t[i].sendExit);
				printf("          USERID=\"%s\"\n", t[i].userid);
				printf("          SSLCIPH=\"%s\"\n", t[i].sslciph);
				printf("          SSLPEER=\"%s\"\n", t[i].sslpeer);
				// printf(  "          TRPTYPE=\"%s\"\n", t[i].trptype);
				printf("          HBINT=\"%s\"\n", t[i].hbint);
				printf("          KAINT=\"%s\"\n", t[i].kaint);
				printf("          CHANNELUSERNAME=\"%s\"\n",
						t[i].channelUserId);
				printf("          ENCRYPTED CHANNELPASSWORD=\"%s\"\n",
						t[i].channelPassword);
				printf("          MQCD_VERSION=\"%s\"\n", t[i].mqcdversion);

			} else {
				printf("\n CONNAME specfied but no CHANNEL specified\n");
				return (2);
			}

		}

		//  note... we should only copy ouf of the iTH element if a conname is present, else treat it
		// like a non-specified value

		makeGoodForRegistry(QueueMgrName, t[i].qm, sizeof(t[0].qm), &qmgrindx);
		makeGoodForRegistry(QueueName, t[i].q, sizeof(t[0].q), &qindx);
		makeGoodForRegistry(ServiceName, t[i].service, sizeof(t[0].service),
				&sindx);
		makeGoodForRegistry(NotesIni, t[i].ni, sizeof(t[0].ni), &niindx);
		makeGoodForRegistry(Conname, t[i].conname, sizeof(t[0].conname),
				&con_indx);
		makeGoodForRegistry(Channel, t[i].channel, sizeof(t[0].channel),
				&chan_indx);
		makeGoodForRegistry(Locladdr, t[i].locladdr, sizeof(t[0].locladdr),
				&locl_indx);
		makeGoodForRegistry(RcvData, t[i].rcvData, sizeof(t[0].rcvData),
				&rcvd_indx);
		makeGoodForRegistry(ScyData, t[i].scyData, sizeof(t[0].scyData),
				&scyd_indx);
		makeGoodForRegistry(SendData, t[i].sendData, sizeof(t[0].sendData),
				&sndd_indx);
		makeGoodForRegistry(RcvExit, t[i].rcvExit, sizeof(t[0].rcvExit),
				&rcve_indx);
		makeGoodForRegistry(ScyExit, t[i].scyExit, sizeof(t[0].scyExit),
				&scye_indx);
		makeGoodForRegistry(SendExit, t[i].sendExit, sizeof(t[0].sendExit),
				&snde_indx);
		makeGoodForRegistry(Userid, t[i].userid, sizeof(t[0].userid),
				&user_indx);
		makeGoodForRegistry(Sslciph, t[i].sslciph, sizeof(t[0].sslciph),
				&sslc_indx);
		makeGoodForRegistry(Sslpeer, t[i].sslpeer, sizeof(t[0].sslpeer),
				&sslp_indx);
		makeGoodForRegistry(Hbint, t[i].hbint, sizeof(t[0].hbint), &hbin_indx);
		makeGoodForRegistry(Kaint, t[i].kaint, sizeof(t[0].kaint), &kain_indx);
		makeGoodForRegistry(ChannelUserId, t[i].channelUserId,
				sizeof(t[0].channelUserId), &chlui_indx);
		makeGoodForRegistry(ChannelPassword, t[i].channelPassword,
				sizeof(t[0].channelPassword), &chlpw_indx);
		makeGoodForRegistry(mqcdVersion, t[i].mqcdversion,
				sizeof(t[0].mqcdversion), &mqcd_indx);
		printf("Added entries from stanza %d to registry arrays\n", i);

	}

	// now, skip past the second \0 ...
	qindx++;
	sindx++;
	qmgrindx++;
	niindx++;
	con_indx++;
	chan_indx++;
	locl_indx++;
	rcvd_indx++;
	scyd_indx++;
	sndd_indx++;
	rcve_indx++;
	scye_indx++;
	snde_indx++;
	user_indx++;
	sslc_indx++;
	sslp_indx++;
	// trpt_indx++; 
	hbin_indx++;
	kain_indx++;
	chlui_indx++;
	chlpw_indx++;
	mqcd_indx++;

	printf("Finished parsing the ini file with RC %d\n", rc);
	return (rc);

}

void makeGoodForRegistry(char * pTarget, char * pSource, int lSource,
		int * piTarget) {

	int slen = lSource - 1;
	while (slen >= 0 && *(pSource + slen) == '\0')
		slen--;
	slen++;
	if (slen > 0) {
		memcpy(pTarget + *piTarget, pSource, slen);
		*piTarget += slen;
	} else {
		memset(pTarget + *piTarget, ' ', 1);
		(*piTarget)++;
	}
	strcpy(pTarget + *piTarget, "\0");
	(*piTarget)++;
	if (debug) {
		printf("Converted %s to %s for registry use",pSource,pTarget);
	}

}

char* encryptData(char* inStr) {
	DATA_BLOB DataIn;
	DATA_BLOB DataOut;
	BYTE *pbDataInput;
	DWORD cbDataInput;

	pbDataInput = (BYTE*) LocalAlloc( LMEM_FIXED, strlen(inStr));
	memset(pbDataInput, '\0', strlen(inStr) + 1);
	memcpy(pbDataInput, inStr, strlen(inStr));
	cbDataInput = strlen((char *) pbDataInput) + 1;

	DataIn.pbData = pbDataInput;
	DataIn.cbData = cbDataInput;
	BYTE *tempVal;
	char *returnVal;

	//-------------------------------------------------------------------
	//  Begin protect phase.

	if (!CryptProtectData(&DataIn,
	NULL, // A description string.
			NULL,                               // Optional entropy
												// not used.
			NULL,                               // Reserved.
			NULL,                               // Pass a PromptStruct.
			0, &DataOut)) {
		ErrorHandler("Encryption error!", GetLastError());
	}
	tempVal = (BYTE*) malloc(DataOut.cbData);
	memset(inStr, '\0', sizeof(inStr));
	memcpy(tempVal, DataOut.pbData, DataOut.cbData);
	hexArrayToStr(tempVal, DataOut.cbData, &returnVal);
	LocalFree(DataOut.pbData);
	LocalFree(DataIn.pbData);
	printf("Finished encrypting the password\n");
	return returnVal;
}

void hexArrayToStr(unsigned char* info, unsigned int infoLength,
		char **buffer) {
	const char* pszNibbleToHex = { "0123456789ABCDEF" };
	int nNibble, i;
	if (infoLength > 0) {
		if (info != NULL) {
			*buffer = (char *) malloc((infoLength * 2) + 1);
			buffer[0][(infoLength * 2)] = 0;
			for (i = 0; i < infoLength; i++) {
				nNibble = info[i] >> 4;
				buffer[0][2 * i] = pszNibbleToHex[nNibble];
				nNibble = info[i] & 0x0F;
				buffer[0][2 * i + 1] = pszNibbleToHex[nNibble];
			}
		} else {
			*buffer = NULL;
		}
	} else {
		*buffer = NULL;
	}
}
