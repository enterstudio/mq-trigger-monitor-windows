/* ******************************************************************/
/* TrigSvc.cpp                                                      */
/* (C) Copyright IBM Corp.1997, 2006                                */
/********************************************************************/
//
// Current Maintainer: Jeff Lowrey
// Original Author: Wayne M. Schutz
//
// version 1.00 -- 04 May 1998 -- initial version.
//              A version of the trigger monitor that can run as a NT Service.
//              This is so we can run it as a client trigger monitor.
//
// version 1.01 - 30 June 1998 - never released as a supportpac -- wms
// version 1.01:(1) made StrucId = TMC instead of TM since we are passing
//                  a character version of the trigger msg,
//              (2) fixed up version number and don't pad appltype number.
//              (3) Now passes EnvData after Trigmsg.
//              (4) Do an MQINQ to get the real qmgr name for the TM.
//              (5) message in event log looks more like runmqtrm generated msg
//              (6) changed CREATE_PROCESS call to SYSTEM() to make specification of .exe optional
//              (7) checking for appltype = WINDOWS_NT (11)
// version 1.1 -- 15 Aug  1998 -- wms
// version 1.1: (1) This version support running Notes agents.  The
//              code that runs notes agents is packaged into a seperate
//              dll that is only loaded if a trigmsg with appltype of
//              22 is encountered.  This way, it is not necessary to
//              have to Notes DLLs unless they are needed.  For symmetry
//              purposes, we also package the appltype 11 call into a dll.
//
//          (2) A new key has been added to the registry to name the MQSeries
//              DLL that we should use.  This allows the same executable
//              to run as either a client or server and insulates us
//              from any .LIB changes.	
//
//
// version 1.20 -- 5 Oct 1998 - wms
//          (1) Provides for the ability to monitor more than one initiation
//              queue.  Each monitored queue is spawned by a seperate thread.
//              The NOTES triggering facility has been changed from a dll
//              to an executable (.exe) to isolate one Notes script program
//              from the next.  The NT registry variables TRIGGERQUEUENAME
//              and TRIGGERQMGRNAME have been changed from a simple string
//               (REG_SZ) to an array of strings (REG_MULTI_SZ)..
//              To suport this, we support a -f parameter which names a
//              file.  The file is a list of queue names and optional
//              queue manager names, one pair per line, up to MAXTHREADS (16)
//              lines.
//
// version 1.21 -- 15 Dec1998 - wms
//          (1) -- added "gmo_convert" to MQGET.
// version 1.22 -- 8 Feb 2000 - wms
//          (1) -- fixed "array overflow" error in writeLog rtn.
//
// version 1.30 ---  07 May   2000 - wms
//          (1) -- added ability to specify a NotesIni file.
//
// version 1.31 --- 29 Jan 2001 - wms
//          (1) -- a number of customers have complained that when they install
//                 this supportpac into a directory that has blanks in the
//                 directory name, and then trigger a Notes agent, the "trige22"
//                 executable cannot be found.  This seems to be a problem with
//                 the system() call, so, for this executable, we use the
//                 CreateProcess windoze call instead.
// version 1.32 -- 
//          (1) linked with Notes API 6.0.1
//          (2) Clean up error message regarding opening QMGR
//          (3) Added "MQRC_Q_MGR_NAME_ERROR" retry condition
//          (4) read service name and service label from file in execution directory called "service.dat".
//              This allows multiple instances / versions of the supportpac to be run on the same machine
//
// version 1.4.0 -- 17 March 2004
//          (1) allow Specification of mqconnx parms in the setup.ini file
// version 1.4.1 -- 28 July 2006
//          see readme.txt
// version 1.5.0 -- Jeff Lowrey now maintainer.
//          see readme.txt for all future change log entries.
#include <windows.h>
#include <winbase.h>
#include <Wincrypt.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <cmqc.h>
#include <cmqcfc.h>
#include <cmqxc.h>                 /* For MQCD definition           */

#include "trigsvc.h"
#include "msgs.h"

typedef struct {
	MQCHAR ServiceName[MQ_OBJECT_NAME_LENGTH + 1];
	MQCHAR ServiceDesc[MQ_SERVICE_DESC_LENGTH + 1];
	MQCHAR AlterationDate[MQ_CREATION_DATE_LENGTH + 1];
	MQCHAR AlterationTime[MQ_CREATION_TIME_LENGTH + 1];
	MQCHAR StartCommand[MQ_SERVICE_COMMAND_LENGTH + 1]; /*  MQCA_SERVICE_START_COMMAND */
	MQCHAR StartArgs[MQ_SERVICE_ARGS_LENGTH + 1]; /*  MQCA_SERVICE_START_ARGS */
	MQCHAR StopCommand[MQ_SERVICE_COMMAND_LENGTH + 1]; /*  MQCA_SERVICE_STOP_COMMAND */
	MQCHAR StopArgs[MQ_SERVICE_ARGS_LENGTH + 1]; /*  MQCA_SERVICE_STOP_ARGS */
	MQCHAR StdoutDestination[MQ_SERVICE_PATH_LENGTH + 1]; /*  MQCA_STDOUT_DESTINATION */
	MQCHAR StderrDestination[MQ_SERVICE_PATH_LENGTH + 1]; /*  MQCA_STDERR_DESTINATION */
	MQLONG Control; /* MQIA_SERVICE_CONTROL */
	MQLONG ServiceType; /* MQIA_SERVICE_TYPE */
	MQHOBJ hAdminQ; // Command Server queue handle
	MQHOBJ hReplyQ; // dynamic reply queue
	HANDLE hProcess; //handle to service process

} SERVINFO, *PSERVINFO;

typedef struct {
	HANDLE threadHandle; // handle of spawned thread
	DWORD threadStatus; // current state of this Thread
	DWORD ThreadID; // ID number used to identify this thread
	MQLONG hConn; // MQ Connection Handle
	MQCHAR Queue[MQ_Q_NAME_LENGTH + 1];
	MQCHAR ServiceName[MQ_OBJECT_NAME_LENGTH + 1];
	MQCHAR QMgr[MQ_Q_MGR_NAME_LENGTH + 1];
	MQCHAR CmdSvrQ[MQ_Q_NAME_LENGTH + 1];
	MQCHAR NotesIni[MAX_PATH];
	MQCHAR conName[MQ_CONN_NAME_LENGTH + 1];
	MQCHAR channel[MQ_CHANNEL_NAME_LENGTH + 1];
	MQCHAR locladdr[MQ_LOCAL_ADDRESS_LENGTH + 1];
	MQCHAR rcvData[MQ_EXIT_DATA_LENGTH + 1];
	MQCHAR scyData[MQ_EXIT_DATA_LENGTH + 1];
	MQCHAR sendData[MQ_EXIT_DATA_LENGTH + 1];
	MQCHAR rcvExit[MQ_EXIT_NAME_LENGTH + 1];
	MQCHAR scyExit[MQ_EXIT_NAME_LENGTH + 1];
	MQCHAR sendExit[MQ_EXIT_NAME_LENGTH + 1];
	MQCHAR userid[MQ_USER_ID_LENGTH + 1];
	MQCHAR sslciph[MQ_SSL_CIPHER_SPEC_LENGTH + 1];
	MQCHAR sslpeer[MQ_SSL_PEER_NAME_LENGTH + 1];
	// MQCHAR  trptype[(10)+1];
	MQCHAR hbint[(10) + 1];
	MQCHAR kaint[(10) + 1];
	MQCHAR mqcdversion[(3) + 1];
	MQCHAR channelUserId[(1024) + 1];
	MQCHAR channelPassword[(MQ_CSP_PASSWORD_LENGTH * 4) + 1];
	PSERVINFO servInfo;
} THRDCTL, *PTHRDCTL;

char tempPw[MQ_CSP_PASSWORD_LENGTH + 1];

// Vales for CurrentState
#define THREAD_STARTING 0
#define THREAD_RUNNING  1
#define THREAD_PAUSED   2
#define THREAD_EXITING  3
#define THREAD_STOPPED  4

// Global variables

// Event used to hold ServiceMain from completing
HANDLE terminateEvent = NULL;
// Handle used to communicate status info with
// the SCM. Created by RegisterServiceCtrlHandler
SERVICE_STATUS_HANDLE serviceStatusHandle;
// Flags holding current state of service
BOOL pauseService = FALSE;
BOOL runningService = FALSE;
BOOL allthreadsPaused = FALSE;
// Thread for the actual work
DWORD ServiceThread(PTHRDCTL);
// hANDLE FOR REPORT EVENT MESSAGES
HANDLE logHandle;
// Thread control pointers
PTHRDCTL pThrdCtl[MAXTHREADS];
// Object to sync threads
CRITICAL_SECTION ThreadCritSect;

// Internal function declarations
void StopService();
BOOL SendStatusToSCM(DWORD, DWORD, DWORD, DWORD, DWORD);
void ServiceCtrlHandler(DWORD);
void ServiceMain(DWORD, LPTSTR *);
MQLONG getRealQmgrName(MQHCONN, MQCHAR48, int tid);
void launchApplication(MQTM, char *, char *);
MQLONG getServiceInfo(MQHCONN, PTHRDCTL);
void parseServiceInfo(PTHRDCTL thdCntl, MQBYTE * pPCFMsg);
void launchMQService(PTHRDCTL thdCntl, bool start);
void StripTrailingBlanks(MQCHAR* MQParm, MQCHAR * Insert, int Length);
void decryptData(char* inStr, char* returnVal);
int myHexToData(char *data, char *hexstring, unsigned int len);
void printMQCNO(char* output, MQCNO *connOpts);

// globals that we get from the NT registry
DWORD valueWaitInterval;
DWORD valueLongTmr;
DWORD valueLongRty;
DWORD valueShortTmr;
DWORD valueShortRty;
DWORD valueEventLevel;
CHAR MyPath[MAX_PATH]; /* Program path */

// v141 changes
MQCHAR keyRepos[MAX_PATH];

// end v141 changes

// pointers to the MQ function calls loaded from the mqic or mqm.dll
void (*tsCONN)(...);
void (*tsCONNX)(...);
void (*tsOPEN)(...);
void (*tsGET)(...);
void (*tsPUT)(...);
void (*tsINQ)(...);
void (*tsCMIT)(...);
void (*tsCLOSE)(...);
void (*tsDISC)(...);

void getServiceName(char *);

/***************************************************************************/
/* getKeyValue --                                                          */
/* This is a utililty function to read a value from the NT registry        */
/***************************************************************************/

BOOL getKeyValue(HKEY keyhandle, // handle to registry keys
		char * keyName, // name of key to get
		char * keyValue, // place to put the value
		DWORD * pBufsz) // size of value buffer
		{

	DWORD valueType = 0;
	DWORD ret = 0;
	char buffer[80];
	LPCSTR substr[2];
	char printstr[256];
	BOOL success = TRUE;

	ret = RegQueryValueEx(keyhandle, // handle of key to query
			keyName, // address of name of value to query
			0, // reserved
			&valueType, // address of buffer for value type
			(LPBYTE) keyValue, // address of data buffer
			pBufsz); // address of data buffer size

	if (ret != ERROR_SUCCESS) {
		substr[0] = buffer;
		sprintf(buffer, "RegQueryValueEx: key(%s), buflen=%ld, buffer_addr=%ld",
				keyName, *pBufsz, keyValue);
		sprintf(printstr, "%4d", GetLastError());
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR, NULL, 2,
				0, substr, NULL);
		success = FALSE; // indicate time to end
	} // end if ret != error_success
	return success;

} // end of getKeyValue function

/***********************************************************************/
/* StopService --                                                      */
/* Stops the service by allowing ServiceMain to complete               */
/* Called when the user wants to stop the service or                   */
/* we have retried MQCONN (or some other retryable condition) too many */
/* times.                                                              */
/***********************************************************************/

void StopService() {
	// Tell the SCM what's happening
	SendStatusToSCM(SERVICE_STOP_PENDING, NO_ERROR, 0, 1, 5000);
	runningService = FALSE;

	// Set the event that is holding ServiceMain
	// so that ServiceMain can return
	if (terminateEvent) {
		SetEvent(terminateEvent);
	}/* End if*/
}

/***********************************************************************/
/* UpdateStatusFromThreads --                                          */
/* This function is called whenever a thread changes its status in     */
/* its control block.  If all the threads are PAUSED, we tell then     */
/* SCM that we're paused.  If a single thread is running, then we tell */
/* the SCM that we're running.                                         */
/***********************************************************************/
void UpdateStatusFromThreads() {

	int i = 0; // loop control
	BOOL paused = TRUE; // assume paused until we find out otherwise
	BOOL stop = TRUE; // assume everyone is exiting

	for (i = 0; i < MAXTHREADS; ++i) {
		if (pThrdCtl[i]) {
			if (pThrdCtl[i]->threadStatus != THREAD_PAUSED)
				paused = FALSE;
			if (pThrdCtl[i]->threadStatus != THREAD_EXITING)
				stop = FALSE;
		}/* End if*/
	}

	// has everyone exited?
	if (stop) {
		StopService();
	} else {
		// now, we need to figure out if there is a state change to tell
		// the SCM about ....
		if (allthreadsPaused) { // does SCM think we're paused?
			if (!paused) { // yes... is there a running thread?
				allthreadsPaused = FALSE;
				SendStatusToSCM(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);
			}
		} else { // SCM thinks we're running ...
			if (paused) { // are all threads paused?
				allthreadsPaused = TRUE;
				SendStatusToSCM(SERVICE_PAUSED, NO_ERROR, 0, 0, 0);
			}
		}
	}

} // end UpdateStatusFromThreads

/***********************************************************************/
/* SendStatusToSCM --                                                  */
/* This function consolidates the activities of updating               */
/* the service status with SetServiceStatus                            */
/* It is called whenever we want to inform the service control manager */
/* of a state change in the service.  For example when going from      */
/* "starting" to "started".                                            */
/***********************************************************************/

BOOL SendStatusToSCM(DWORD dwCurrentState, DWORD dwWin32ExitCode,
		DWORD dwServiceSpecificExitCode, DWORD dwCheckPoint, DWORD dwWaitHint) {
	BOOL success;
	SERVICE_STATUS serviceStatus;
	LPCSTR substr[2];
	char printstr[256];

	// Fill in all of the SERVICE_STATUS fields
	serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	serviceStatus.dwCurrentState = dwCurrentState;

	// If in the process of something, then accept
	// no control events, else accept anything
	if (dwCurrentState == SERVICE_START_PENDING)
		serviceStatus.dwControlsAccepted = 0;
	else
		serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP
				| SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_SHUTDOWN;

	// if a specific exit code is defined, set up
	// the win32 exit code properly
	if (dwServiceSpecificExitCode == 0)
		serviceStatus.dwWin32ExitCode = dwWin32ExitCode;
	else
		serviceStatus.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;

	serviceStatus.dwServiceSpecificExitCode = dwServiceSpecificExitCode;

	serviceStatus.dwCheckPoint = dwCheckPoint;
	serviceStatus.dwWaitHint = dwWaitHint;

	if (serviceStatusHandle) {
		// Pass the status record to the SCM

		if (valueEventLevel > 10) {
			sprintf(printstr,
					"Handle = %ld, State = %ld, Controls= %ld, ExitCode = %ld, Specific = %ld, CheckPt = %ld, WaitHint = %ld",
					serviceStatusHandle, serviceStatus.dwCurrentState,
					serviceStatus.dwControlsAccepted,
					serviceStatus.dwWin32ExitCode,
					serviceStatus.dwServiceSpecificExitCode,
					serviceStatus.dwCheckPoint, serviceStatus.dwWaitHint);

			substr[0] = printstr;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
			NULL, 1, 0, substr, NULL);
		}

		success = SetServiceStatus(serviceStatusHandle, &serviceStatus);
		if (!success) {
			substr[0] = "SetServiceStatus";
			sprintf(printstr, "%4d", GetLastError());
			substr[1] = printstr;
			ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR,
			NULL, 2, 0, substr, NULL);
		}
	} /* End if servicestatusHandle*/

	return success;
} // end of SendStatusToSCM function

/********************************************************************************/
/* ServiceCtrlHandler --                                                        */
/* Dispatches events received from the service control manager                  */
/* It is called by the Service Control Manager whenever the SCM wants to change */
/* our status (for example .. to Pause )                                        */
/* The "RegisterServiceCtrlHandler" call in ServiceMain registes this function  */
/********************************************************************************/
void ServiceCtrlHandler(DWORD controlCode) {
	DWORD currentState = 0;
	int i = 0; // loop control

	if (runningService)
		if (pauseService)
			currentState = SERVICE_PAUSED;
		else
			currentState = SERVICE_RUNNING;
	else
		currentState = SERVICE_STOP_PENDING;

	switch (controlCode) {
	// There is no START option because
	// ServiceMain gets called on a start

	// Stop the service
	case SERVICE_CONTROL_STOP:
		currentState = SERVICE_STOP_PENDING;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, STOP_REQ, NULL,
				0, 0, NULL, NULL);

		// If we do decide to MQPUT1 or MQSET queue
		// put that code here.

		// Stop the service
		StopService();
		return;

		// Pause the service
	case SERVICE_CONTROL_PAUSE:
		if (runningService && !pauseService) {
			// Tell the SCM what's happening
			SendStatusToSCM(SERVICE_PAUSE_PENDING, NO_ERROR, 0, 1, 1000);
			pauseService = TRUE;
			for (i = 0; i < MAXTHREADS; i++) {
				if (pThrdCtl[i]) {
					if (pThrdCtl[i]->threadHandle)
						SuspendThread(pThrdCtl[i]->threadHandle);
					pThrdCtl[i]->threadStatus = THREAD_PAUSED;
				}
			} // end for ...
			currentState = SERVICE_PAUSED;
		}
		break;

		// Resume from a pause
	case SERVICE_CONTROL_CONTINUE:
		if (runningService && pauseService) {
			// Tell the SCM what's happening
			SendStatusToSCM(SERVICE_CONTINUE_PENDING, NO_ERROR, 0, 1, 1000);
			pauseService = FALSE;
			for (i = 0; i < MAXTHREADS; i++) {
				if (pThrdCtl[i]) {
					if (pThrdCtl[i]->threadHandle)
						ResumeThread(pThrdCtl[i]->threadHandle);
					pThrdCtl[i]->threadStatus = THREAD_RUNNING;
				}
			} // end for ...
			currentState = SERVICE_RUNNING;
		}
		break;

		// Update current status
	case SERVICE_CONTROL_INTERROGATE:
		// it will fall to bottom and send status
		break;
	case SERVICE_CONTROL_SHUTDOWN:
		// Do nothing on shutdown
		// if we do something, it has to be quick
		currentState = SERVICE_STOP_PENDING;
		return;
	default:
		break;
	}
	SendStatusToSCM(currentState, NO_ERROR, 0, 0, 0);
}

/*****************************************************************************************/
/* ServiceMain  --                                                                       */
/* This routine is called when the SCM wants to start the service.                       */
/* When it returns, the service has stopped. It therefore waits on an event              */
/* just before the end of the function, and that event gets set when it is time to stop. */
/* It also returns on any error because the service cannot start if there is an eror.    */
/*****************************************************************************************/

void ServiceMain(DWORD argc, LPTSTR *argv) {

	LPCSTR substr[2];

	BOOL success;

	char printstr[1024];
	char printstr2[1024];
	DWORD id;
	HKEY keyhandle;
	char defaultQMgrName[MQ_Q_MGR_NAME_LENGTH + 1];
	char defaultQueueName[MQ_Q_NAME_LENGTH + 1];

	DWORD bufsz;
	CHAR strBuf[80];
	DWORD ret;
	CHAR valueMQdll[MQ_CONN_NAME_LENGTH + 1];

	HINSTANCE mqHandle;
	char *pQNames = NULL, *pwQNames = NULL;
	char *pSNames = NULL, *pwSNames = NULL;
	char *pQMgrNames = NULL, *pwQMgrNames = NULL;
	char *pNotesIni = NULL, *pwNotesIni = NULL;

	char *pconName = NULL, *pwconName = NULL;
	char *pchannel = NULL, *pwchannel = NULL;
	char *plocladdr = NULL, *pwlocladdr = NULL;
	char *prcvData = NULL, *pwrcvData = NULL;
	char *pscyData = NULL, *pwscyData = NULL;
	char *psendData = NULL, *pwsendData = NULL;
	char *prcvExit = NULL, *pwrcvExit = NULL;
	char *pscyExit = NULL, *pwscyExit = NULL;
	char *psendExit = NULL, *pwsendExit = NULL;
	char *puserid = NULL, *pwuserid = NULL;
	char *psslciph = NULL, *pwsslciph = NULL;
	char *psslpeer = NULL, *pwsslpeer = NULL;
	// char *ptrptype=NULL, *pwtrptype=NULL;
	char *phbint = NULL, *pwhbint = NULL;
	char *pkaint = NULL, *pwkaint = NULL;
	char *pchlusername = NULL, *pchannelpw = NULL;
	char *pwchluid = NULL, *pwchlpw = NULL;
	char *pmqcdversion = NULL, *pwmqcdversion = NULL;

	DWORD lQNames = 0, lSNames = 0, lQMgrNames = 0, lNotesIni = 0;
	DWORD l_conn = 0, l_chan = 0, l_locl = 0, l_rcvd = 0, l_scyd = 0,
			l_sndd = 0, l_rcve = 0, l_scye = 0, l_snde = 0, l_user = 0, l_sslc =
					0, l_sslp = 0, l_trpt = 0, l_hbin = 0, l_kain = 0,
			l_chluid = 0, l_chlpw = 0, l_mqcd = 0;

	char serviceName[128]; // name of the service

	success = TRUE;
	int i = 0;

	strcpy(serviceName, argv[0]); // copy the name of the service

	// register a handle for events
	logHandle = RegisterEventSource(NULL, serviceName);

	ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, STARTING, NULL, 0,
			0, NULL, NULL);

	// immediately call Registration function
	serviceStatusHandle = RegisterServiceCtrlHandler(serviceName,
			(LPHANDLER_FUNCTION) ServiceCtrlHandler);
	if (!serviceStatusHandle) {
		substr[0] = "RegisterServiceCtrlHandler";
		sprintf(printstr, "%4d", GetLastError());
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR, NULL, 2,
				0, substr, NULL);
		success = FALSE; // indicate time to end

	}

	runningService = TRUE;

	if (success) {
		// Notify SCM of progress
		SendStatusToSCM(SERVICE_START_PENDING, NO_ERROR, 0, 1, 5000);

		// create the termination event
		terminateEvent = CreateEvent(0, TRUE, FALSE, 0);
		if (!terminateEvent) {
			substr[0] = "CreateEvent";
			sprintf(printstr, "%4d", GetLastError());
			substr[1] = printstr;
			ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR,
			NULL, 2, 0, substr, NULL);
			success = FALSE; // indicate time to end
		}
	} // end of if success

	// build the path for the key
	strcpy(strBuf, KEYPREFIX);
	strcat(strBuf, serviceName);

	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, strBuf, 0, KEY_QUERY_VALUE,
			&keyhandle);

	if (ret != ERROR_SUCCESS) {
		substr[0] = "RegOpenKeyEx";
		sprintf(printstr, "%4d", ret);
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR, NULL, 2,
				0, substr, NULL);
		success = FALSE; // indicate time to end
	} /* end of error */

	// now malloc a big area to receive q names and qmgr names
	lQNames = MAXTHREADS * (MQ_Q_MGR_NAME_LENGTH + 1) + 1;
	pQNames = (char*) malloc(lQNames);
	lSNames = MAXTHREADS * (MQ_SERVICE_NAME_LENGTH + 1) + 1;
	pSNames = (char*) malloc(lSNames);
	lQMgrNames = MAXTHREADS * (MQ_Q_NAME_LENGTH + 1) + 1;
	pQMgrNames = (char*) malloc(lQMgrNames);
	lNotesIni = MAXTHREADS * (MAX_PATH + 1) + 1;
	pNotesIni = (char*) malloc(lNotesIni);
	l_conn = MAXTHREADS * (MQ_CONN_NAME_LENGTH + 1) + 1;
	pconName = (char*) malloc(l_conn);
	l_chan = MAXTHREADS * (MQ_CHANNEL_NAME_LENGTH + 1) + 1;
	pchannel = (char*) malloc(l_chan);
	l_locl = MAXTHREADS * (MQ_LOCAL_ADDRESS_LENGTH + 1) + 1;
	plocladdr = (char*) malloc(l_locl);
	l_rcvd = MAXTHREADS * (MQ_EXIT_DATA_LENGTH + 1) + 1;
	prcvData = (char*) malloc(l_rcvd);
	l_scyd = MAXTHREADS * (MQ_EXIT_DATA_LENGTH + 1) + 1;
	pscyData = (char*) malloc(l_scyd);
	l_sndd = MAXTHREADS * (MQ_EXIT_DATA_LENGTH + 1) + 1;
	psendData = (char*) malloc(l_sndd);
	l_rcve = MAXTHREADS * (MQ_EXIT_NAME_LENGTH + 1) + 1;
	prcvExit = (char*) malloc(l_rcve);
	l_scye = MAXTHREADS * (MQ_EXIT_NAME_LENGTH + 1) + 1;
	pscyExit = (char*) malloc(l_scye);
	l_snde = MAXTHREADS * (MQ_EXIT_NAME_LENGTH + 1) + 1;
	psendExit = (char*) malloc(l_snde);
	l_user = MAXTHREADS * (MQ_USER_ID_LENGTH + 1) + 1;
	puserid = (char*) malloc(l_user);
	l_sslc = MAXTHREADS * (MQ_SSL_CIPHER_SPEC_LENGTH + 1) + 1;
	psslciph = (char*) malloc(l_sslc);
	l_sslp = MAXTHREADS * (MQ_SSL_PEER_NAME_LENGTH + 1) + 1;
	psslpeer = (char*) malloc(l_sslp);
	// l_trpt = MAXTHREADS*(10+1)+1;                                     ptrptype =     (char*)malloc(l_trpt);
	l_hbin = MAXTHREADS * (10 + 1) + 1;
	phbint = (char*) malloc(l_hbin);
	l_kain = MAXTHREADS * (10 + 1) + 1;
	pkaint = (char*) malloc(l_kain);
	l_chluid = MAXTHREADS * (1024 + 1) + 1;
	pchlusername = (char*) malloc(l_chluid);
	l_chlpw = MAXTHREADS * ((MQ_CSP_PASSWORD_LENGTH * 4) + 1) + 1;
	//right now we are guessing that 4x will hold the encrypted password data.
	pchannelpw = (char*) malloc(l_chlpw);
	l_mqcd = MAXTHREADS * (10 + 1) + 1;
	pmqcdversion = (char*) malloc(l_mqcd);

	if (!pQNames || !pSNames || !pQMgrNames || !pNotesIni || !pconName
			|| !pchannel || !plocladdr || !prcvData || !pscyData || !psendData
			|| !prcvExit || !pscyExit || !psendExit || !puserid || !psslciph
			|| !psslpeer ||
			// !ptrptype ||
			!phbint || !pkaint || !pmqcdversion) { // big trouble
		substr[0] = "malloc";
		substr[1] = "-1";
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR, NULL, 2,
				0, substr, NULL);
		success = FALSE; // indicate time to end
	}/* End if malloc failed */

	if (success)
		success = getKeyValue(keyhandle, TRIGGERQUEUEMGRNAME, pQMgrNames,
				&lQMgrNames);
	if (success)
		success = getKeyValue(keyhandle, TRIGGERQUEUENAME, pQNames, &lQNames);
	if (success)
		success = getKeyValue(keyhandle, SERVICENAME, pSNames, &lSNames);
	if (success)
		success = getKeyValue(keyhandle, NOTESINI, pNotesIni, &lNotesIni);
	if (success)
		success = getKeyValue(keyhandle, CONNAME, pconName, &l_conn);
	if (success)
		success = getKeyValue(keyhandle, CHANNEL, pchannel, &l_chan);
	if (success)
		success = getKeyValue(keyhandle, LOCLADDR, plocladdr, &l_locl);
	if (success)
		success = getKeyValue(keyhandle, RCVDATA, prcvData, &l_rcvd);
	if (success)
		success = getKeyValue(keyhandle, SCYDATA, pscyData, &l_scyd);
	if (success)
		success = getKeyValue(keyhandle, SENDDATA, psendData, &l_sndd);
	if (success)
		success = getKeyValue(keyhandle, SENDEXIT, psendExit, &l_snde);
	if (success)
		success = getKeyValue(keyhandle, RCVEXIT, prcvExit, &l_rcve);
	if (success)
		success = getKeyValue(keyhandle, SCYEXIT, pscyExit, &l_scye);
	if (success)
		success = getKeyValue(keyhandle, USERID, puserid, &l_user);
	if (success)
		success = getKeyValue(keyhandle, SSLCIPH, psslciph, &l_sslc);
	if (success)
		success = getKeyValue(keyhandle, SSLPEER, psslpeer, &l_sslp);
	// if (success) success = getKeyValue(keyhandle, TRPTYPE, ptrptype, &l_trpt);
	if (success)
		success = getKeyValue(keyhandle, HBINT, phbint, &l_hbin);
	if (success)
		success = getKeyValue(keyhandle, KAINT, pkaint, &l_kain);
	if (success)
		success = getKeyValue(keyhandle, CHANNELUID, pchlusername, &l_chluid);
	if (success)
		success = getKeyValue(keyhandle, CHANNELPW, pchannelpw, &l_chlpw);
	if (success)
		success = getKeyValue(keyhandle, MA7K_MQCD_VERSION, pmqcdversion,
				&l_mqcd);

	// end of v140 changes

	// v141 get key repository

	if (success) {
		bufsz = sizeof(keyRepos);
		success = getKeyValue(keyhandle, KEYREPOS, (char *) &keyRepos, &bufsz);
	}

	// end v141

	if (success) {
		bufsz = sizeof(valueWaitInterval);
		success = getKeyValue(keyhandle, WAITINTERVAL,
				(char*) &valueWaitInterval, &bufsz);
	}
	if (success) {
		bufsz = sizeof(valueLongTmr);
		success = getKeyValue(keyhandle, LONGTMR, (char*) &valueLongTmr,
				&bufsz);
	}
	if (success) {
		bufsz = sizeof(valueLongRty);
		success = getKeyValue(keyhandle, LONGRTY, (char*) &valueLongRty,
				&bufsz);
	}
	if (success) {
		bufsz = sizeof(valueShortTmr);
		success = getKeyValue(keyhandle, SHORTTMR, (char*) &valueShortTmr,
				&bufsz);
	}
	if (success) {
		bufsz = sizeof(valueShortRty);
		success = getKeyValue(keyhandle, SHORTRTY, (char*) &valueShortRty,
				&bufsz);
	}
	if (success) {
		bufsz = sizeof(valueEventLevel);
		success = getKeyValue(keyhandle, EVENTLEVEL, (char*) &valueEventLevel,
				&bufsz);
	}
	if (success) {
		bufsz = sizeof(valueMQdll);
		success = getKeyValue(keyhandle, MQDLL, (char*) &valueMQdll, &bufsz);
	}
	if (success) {
		bufsz = sizeof(MyPath);
		success = getKeyValue(keyhandle, MYEXEPATH, (char*) &MyPath, &bufsz);
	}

	RegCloseKey(keyhandle); // close the key handle .. done for now ...

	if (success) {
		mqHandle = LoadLibraryEx(valueMQdll, NULL, 0);
		if (!mqHandle) {
			substr[0] = valueMQdll;
			sprintf(printstr, "%4d", GetLastError());
			substr[1] = printstr;
			ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, LOAD_DLL_ERR,
			NULL, 2, 0, substr, NULL);
			success = FALSE; // indicate time to end
		} else {

			tsCONN = (void (*)(...)) GetProcAddress(mqHandle, "MQCONN");
			tsCONNX = (void (*)(...)) GetProcAddress(mqHandle, "MQCONNX");
			tsOPEN = (void (*)(...)) GetProcAddress(mqHandle, "MQOPEN");
			tsGET = (void (*)(...)) GetProcAddress(mqHandle, "MQGET");
			tsPUT = (void (*)(...)) GetProcAddress(mqHandle, "MQPUT");
			tsINQ = (void (*)(...)) GetProcAddress(mqHandle, "MQINQ");
			tsCMIT = (void (*)(...)) GetProcAddress(mqHandle, "MQCMIT");
			tsCLOSE = (void (*)(...)) GetProcAddress(mqHandle, "MQCLOSE");
			tsDISC = (void (*)(...)) GetProcAddress(mqHandle, "MQDISC");
			sprintf(printstr, "tsCONN is %d", tsCONN);
			substr[0] = printstr;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
			NULL, 1, 0, substr, NULL);
			sprintf(printstr, "tsOPEN is %d", tsOPEN);
			substr[0] = printstr;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
			NULL, 1, 0, substr, NULL);
			if (!tsCONN || !tsCONNX || !tsOPEN || !tsGET || !tsINQ || !tsCMIT
					|| !tsCLOSE || !tsDISC) {

				substr[0] = valueMQdll;
				sprintf(printstr, "%4d", GetLastError());
				substr[1] = printstr;
				ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, LOAD_DLL_ERR,
				NULL, 2, 0, substr, NULL);
				success = FALSE; // indicate time to end
			}/* End if GET PROC ADDR FAILED */
		}/* End if !mqHandle */

	}/* End if success */

	if (success) {
		if (valueEventLevel > 1) {
			sprintf(printstr,
					"WaitInterval=%ld LongRty=%ld LongTmr=%ld ShortRty=%ld "
							"ShortTmr=%ld EventLevel=%ld", valueWaitInterval,
					valueLongRty, valueLongTmr, valueShortRty, valueShortTmr,
					valueEventLevel);
			substr[0] = printstr;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, STARTED,
			NULL, 1, 0, substr, NULL);
		} // end event level test
		  // Check for startup params, if they have been specified,
		  // then overlay anything we got from the registry ....
		if (argc > 1) {
			strcpy(pQNames, argv[1]);
			lQNames = strlen(argv[1]) + 2;
			*(pQNames + lQNames - 1) = '\0';
		}
		if (argc > 2) {
			strcpy(pQMgrNames, argv[2]);
			lQMgrNames = strlen(argv[2]) + 2;
			*(pQMgrNames + lQMgrNames - 1) = '\0';
		}

		// Now we'll build all the required thread ctl structures.
		// first, init all the pointers to NULL so we know how many
		// we have ...
		for (i = 0; i < MAXTHREADS; i++)
			pThrdCtl[i] = NULL;

		// read queue names and qmgr name array into the thread control
		// structures ... we base this on the number of init queues specified
		pwQNames = pQNames; // variable to loop through array
		pwSNames = pSNames; // variable to loop through array
		pwQMgrNames = pQMgrNames; // variable to loop through array
		pwNotesIni = pNotesIni; // variable to loop through array
		pwconName = pconName; // variable to loop through array
		pwchannel = pchannel;
		pwlocladdr = plocladdr;
		pwrcvData = prcvData;
		pwscyData = pscyData;
		pwsendData = psendData;
		pwrcvExit = prcvExit;
		pwscyExit = pscyExit;
		pwsendExit = psendExit;
		pwuserid = puserid;
		pwsslciph = psslciph;
		pwsslpeer = psslpeer;
		// pwtrptype = ptrptype;
		pwhbint = phbint;
		pwkaint = pkaint;
		pwchluid = pchlusername;
		pwchlpw = pchannelpw;
		pwmqcdversion = pmqcdversion;

		i = 0;
		while (*pwQNames && i < MAXTHREADS && (pwQNames < pQNames + lQNames)
				&& (pwSNames < pSNames + lSNames)
				&& (pwQMgrNames < pQMgrNames + lQMgrNames)
				&& (pwconName < pconName + l_conn)
				&& (pwchannel < pchannel + l_chan)
				&& (pwlocladdr < plocladdr + l_locl)
				&& (pwrcvData < prcvData + l_rcvd)
				&& (pwscyData < pscyData + l_scyd)
				&& (pwsendData < psendData + l_sndd)
				&& (pwrcvExit < prcvExit + l_rcve)
				&& (pwscyExit < pscyExit + l_scye)
				&& (pwsendExit < psendExit + l_snde)
				&& (pwuserid < puserid + l_user)
				&& (pwsslciph < psslciph + l_sslc)
				&& (pwsslpeer < psslpeer + l_sslp)
				// && (pwtrptype < ptrptype + l_trpt )
				&& (pwhbint < phbint + l_hbin) && (pwkaint < pkaint + l_kain)
				&& (pwchluid < pchlusername + l_chluid)
				&& (pwchlpw < pchannelpw + l_chlpw)
				&& (pwmqcdversion < pmqcdversion + l_mqcd)
				&& (pwNotesIni < pNotesIni + lNotesIni)) { // while we are pointing to something

			pThrdCtl[i] = (PTHRDCTL) malloc(sizeof(THRDCTL));
			memset(pThrdCtl[i], '\0', sizeof(THRDCTL));

			pThrdCtl[i]->ThreadID = i; // save the index
			pThrdCtl[i]->threadStatus = THREAD_STARTING;
			pThrdCtl[i]->hConn = MQHC_UNUSABLE_HCONN;
			strcpy(pThrdCtl[i]->Queue, pwQNames); // save the queue name
			if (valueEventLevel > 10) {
				sprintf(printstr, "Read queue name of %s at position %d",
						pwQNames, i);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				STARTED, NULL, 1, 0, substr, NULL);
			} // end event level test
			strcpy(pThrdCtl[i]->ServiceName, pwSNames); // save the service name
			if (valueEventLevel > 10) {
				sprintf(printstr, "Read service name of %s at position %d",
						pwSNames, i);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				STARTED, NULL, 1, 0, substr, NULL);
			} // end event level test
			strcpy(pThrdCtl[i]->QMgr, pwQMgrNames); // save the queue mgr name
			strcpy(pThrdCtl[i]->CmdSvrQ, DEFAULTCMDSVRQ); // save the queue mgr name
			strcpy(pThrdCtl[i]->NotesIni, pwNotesIni); // save the ini file name
			strcpy(pThrdCtl[i]->conName, pwconName); // save the connection name
			strcpy(pThrdCtl[i]->channel, pwchannel);
			strcpy(pThrdCtl[i]->locladdr, pwlocladdr);
			strcpy(pThrdCtl[i]->rcvData, pwrcvData);
			strcpy(pThrdCtl[i]->scyData, pwscyData);
			strcpy(pThrdCtl[i]->sendData, pwsendData);
			strcpy(pThrdCtl[i]->rcvExit, pwrcvExit);
			strcpy(pThrdCtl[i]->scyExit, pwscyExit);
			strcpy(pThrdCtl[i]->sendExit, pwsendExit);
			strcpy(pThrdCtl[i]->userid, pwuserid);
			strcpy(pThrdCtl[i]->sslciph, pwsslciph);
			strcpy(pThrdCtl[i]->sslpeer, pwsslpeer);
			// strcpy(pThrdCtl[i]->trptype, pwtrptype);
			strcpy(pThrdCtl[i]->hbint, pwhbint);
			strcpy(pThrdCtl[i]->kaint, pwkaint);
			strcpy(pThrdCtl[i]->channelUserId, pchlusername);
			strcpy(pThrdCtl[i]->channelPassword, pchannelpw);
			strcpy(pThrdCtl[i]->mqcdversion, pwmqcdversion);
			pThrdCtl[i]->servInfo = (PSERVINFO) malloc(sizeof(SERVINFO));
			memset(pThrdCtl[i]->servInfo, '\0', sizeof(SERVINFO));
			pThrdCtl[i]->servInfo->hAdminQ = MQHO_UNUSABLE_HOBJ;
			pThrdCtl[i]->servInfo->hReplyQ = MQHO_UNUSABLE_HOBJ;
			pThrdCtl[i]->servInfo->hProcess = 0;
			pwQNames += strlen(pwQNames) + 1;
			pwSNames += strlen(pwSNames) + 1;
			if (valueEventLevel > 10) {
				sprintf(printstr,
						"allocated servInfo and initialized pieces for thread %d.",
						i);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(pThrdCtl[i]->ServiceName), substr,
						&(pThrdCtl[i]->ServiceName));
			}
			if (*pwQMgrNames)
				pwQMgrNames += strlen(pwQMgrNames) + 1;
			if (*pwNotesIni)
				pwNotesIni += strlen(pwNotesIni) + 1;
			if (*pwconName)
				pwconName += strlen(pwconName) + 1;
			if (*pwchannel)
				pwchannel += strlen(pwchannel) + 1;
			if (*pwlocladdr)
				pwlocladdr += strlen(pwlocladdr) + 1;
			if (*pwrcvData)
				pwrcvData += strlen(pwrcvData) + 1;
			if (*pwsendData)
				pwsendData += strlen(pwsendData) + 1;
			if (*pwscyData)
				pwscyData += strlen(pwscyData) + 1;
			if (*pwrcvExit)
				pwrcvExit += strlen(pwrcvExit) + 1;
			if (*pwsendExit)
				pwsendExit += strlen(pwsendExit) + 1;
			if (*pwscyExit)
				pwscyExit += strlen(pwscyExit) + 1;
			if (*pwuserid)
				pwuserid += strlen(pwuserid) + 1;
			if (*pwsslciph)
				pwsslciph += strlen(pwsslciph) + 1;
			if (*pwsslpeer)
				pwsslpeer += strlen(pwsslpeer) + 1;
			// if (*pwtrptype)   pwtrptype += strlen(pwtrptype)+1;
			if (*pwhbint)
				pwhbint += strlen(pwhbint) + 1;
			if (*pwkaint)
				pwkaint += strlen(pwkaint) + 1;
			if (*pwchluid)
				pwchluid += strlen(pwchluid) + 1;
			if (*pwchlpw)
				pwchlpw += strlen(pwchlpw) + 1;
			if (*pwmqcdversion)
				pwmqcdversion += strlen(pwmqcdversion) + 1;

			i++;
		} // end of the while

		// free the area we got to hold the names originally

		if (pQNames)
			free(pQNames);
		if (pQMgrNames)
			free(pQMgrNames);
		if (pNotesIni)
			free(pNotesIni);
		if (pconName)
			free(pconName);

		if (pchannel)
			free(pchannel);
		if (plocladdr)
			free(plocladdr);
		if (prcvData)
			free(prcvData);
		if (pscyData)
			free(pscyData);
		if (psendData)
			free(psendData);
		if (prcvExit)
			free(prcvExit);
		if (pscyExit)
			free(pscyExit);
		if (psendExit)
			free(psendExit);
		if (puserid)
			free(puserid);
		if (psslciph)
			free(psslciph);
		if (psslpeer)
			free(psslpeer);
		// if(ptrptype) free(ptrptype);
		if (phbint)
			free(phbint);
		if (pkaint)
			free(pkaint);
		if (pmqcdversion)
			free(pmqcdversion);

		// Start the threads themselves ...
		InitializeCriticalSection(&ThreadCritSect);
		i = 0;
		while (i < MAXTHREADS && pThrdCtl[i]) {

			// Start the service's thread
			pThrdCtl[i]->threadHandle = CreateThread(0, 0,
					(LPTHREAD_START_ROUTINE) ServiceThread,
					(LPVOID) pThrdCtl[i], // parm being passed to thread
					0, &id);

			if (pThrdCtl[i]->threadHandle == 0) {
				substr[0] = "CreateThread";
				sprintf(printstr, "%4d", GetLastError());
				substr[1] = printstr;
				ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR,
				NULL, 2, 0, substr, NULL);
				success = FALSE;
				break;
			}
			i++;

		}/* End while */

	} // end if success

	// Tell the SCM we're up and running ...
	SendStatusToSCM(SERVICE_RUNNING, NO_ERROR, 0, 0, 0);

	// Wait for stop signal, and then terminate
	if (success)
		WaitForSingleObject(terminateEvent, INFINITE);

	// If the thread has started kill it off and free the control block

	for (i = 0; i < MAXTHREADS; i++) {
		if (pThrdCtl[i]) {
			if (pThrdCtl[i]->threadHandle) {
				pThrdCtl[i]->threadStatus = THREAD_EXITING;
				if (!WaitForSingleObject(pThrdCtl[i]->threadHandle, 300000)) {
					CloseHandle(pThrdCtl[i]->threadHandle);
				}
			}
			free(pThrdCtl[i]);
		}

	} // end for ...

	// if terminateEvent has been created, close it.
	if (terminateEvent)
		CloseHandle(terminateEvent);

	ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, STOPPED, NULL, 0, 0,
	NULL, NULL);

	// sleep a little while to make sure last msg is written
	SleepEx(5000, FALSE);

	// free the event handle

	DeregisterEventSource(logHandle);

	// Free the critical section object
	InitializeCriticalSection(&ThreadCritSect);

	// Send a message to the scm to tell about
	// stopage

	SendStatusToSCM(SERVICE_STOPPED, 0, 0, 0, 0);

} // end ServiceMain

/*****************************************************************************************/
/* WinMain --                                                                            */
/* This routine gets loaded by the SCM when the service is initially installed           */
/* or NT is restarted.                                                                   */
/*****************************************************************************************/
int WINAPI WinMain(HINSTANCE ghInstance, HINSTANCE hPrevInstance,
		LPSTR lpCmdLine, int nCmdShow) {

	char serviceName[128]; // name of the service

	SERVICE_TABLE_ENTRY serviceTable[] = { { "foof",
			(LPSERVICE_MAIN_FUNCTION) ServiceMain }, { NULL, NULL } };

	BOOL success;
	LPCSTR substr[2];
	char printstr[256];

	// Get the name of the service we are running as and set it into the table
	getServiceName(serviceName);
	serviceTable[0].lpServiceName = &serviceName[0];

	// Register with the SCM
	success = StartServiceCtrlDispatcher(serviceTable);
	if (!success) {
		// register a handle for events
		logHandle = RegisterEventSource(NULL, "TrigSvc");

		substr[0] = "StartServiceCtrlDispatcher";
		sprintf(printstr, "%4d", GetLastError());
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR, NULL, 2,
				0, substr, NULL);
	}

	return 0;
}

/*****************************************************************************/
/* ServiceThread ---                                                         */
/* This thread gets started by ServiceMain when the service is started by NT */
/* Service main has established the environment (reading the environment     */
/* variables &c).  This routine connects to MQ and loop on the trigger msg.  */
/* When a message arrives, the character version of the trigger message is   */
/* built (mqtmc2) and the appropriate DLL is loaded to run the program       */
/* TrigS11.dll just calls the system() function to execute an NT program     */
/* (usually an .exe or .bat).  Trigs22.dll sets up and calls the Notes       */
/* agent.
 /*****************************************************************************/

DWORD ServiceThread(PTHRDCTL pThrdCtl) {
	/*   Declare MQI structures needed                                */
	MQCNO Connect_options = { MQCNO_DEFAULT };
	/* MQCONNX options               */
	MQCD ClientConn = { MQCD_CLIENT_CONN_DEFAULT };
	/* Client connection channel     */
	/* definition                    */

	MQSCO ssl_Connection_Options = { MQSCO_DEFAULT };

	MQOD od = { MQOD_DEFAULT }; /* Object Descriptor */
	MQMD md = { MQMD_DEFAULT }; /* Message Descriptor */
	MQGMO gmo = { MQGMO_DEFAULT }; /* get message options */
	MQCSP csp = { MQCSP_DEFAULT }; /*security descriptor */
	MQTM trigger; /* trigger message buffer */
	MQHCONN Hcon = 0; /* connection handle */
	MQHOBJ Hobj = 0; /* object handle */
	MQLONG CompCode = 0; /* completion code */
	MQLONG OpenCode = 0; /* MQOPEN completion code */
	MQLONG DReason = 0; /* reason code */
	MQLONG CReason = 0; /* reason code for MQCONN */
	MQLONG buflen = 0; /* buffer length */
	MQLONG triglen = 0; /* message length received */
	LPCSTR substr[4];
	char printstr[256];
	DWORD numberOfRetries = 0;
	BOOL retry = TRUE;
	DWORD sleepInterval = 0;
	char verb[12];
	MQCHAR48 realqmgrname; /* the real name of the queue manager */

	/** executable code ****************************************************/

	if (valueEventLevel > 1) {

		sprintf(printstr, "Q=\"%s\" Qmgr=\"%s\" NotesIni=\"%s\" (Th# %d)",
				pThrdCtl->Queue, pThrdCtl->QMgr, pThrdCtl->NotesIni,
				pThrdCtl->ThreadID);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, STARTED_THREAD,
		NULL, 1, 0, substr, NULL);
	}
	/* end event level test */
	/* loop as long as we have a retry condition */

	while (retry) {

		EnterCriticalSection(&ThreadCritSect);
		pThrdCtl->threadStatus = THREAD_RUNNING;
		UpdateStatusFromThreads();
		LeaveCriticalSection(&ThreadCritSect);

		Hcon = MQHC_UNUSABLE_HCONN;
		Hobj = MQHO_UNUSABLE_HOBJ;
		pThrdCtl->hConn = MQHC_UNUSABLE_HCONN;

		if (pThrdCtl->conName[0] != ' ') {
			char temp[10];
			memset(temp, '\0', sizeof(temp));
			char debugstr[1024];
			memset(debugstr, '\0', sizeof(debugstr));

			strcpy(verb, "MQCONNX");

			strncpy(ClientConn.ConnectionName, pThrdCtl->conName,
			MQ_CONN_NAME_LENGTH);
			strcat(debugstr, "ConnectionName=");
			strncat(debugstr, ClientConn.ConnectionName, MQ_CONN_NAME_LENGTH);

			strncpy(ClientConn.ChannelName, pThrdCtl->channel,
			MQ_CHANNEL_NAME_LENGTH);
			strcat(debugstr, "; ChannelName=");
			strncat(debugstr, ClientConn.ChannelName, MQ_CHANNEL_NAME_LENGTH);

			if (pThrdCtl->mqcdversion[0] != ' ') {
				ClientConn.Version = atoi(pThrdCtl->mqcdversion);
				strcat(debugstr, "; Version=");
				sprintf(temp, "%ld", ClientConn.Version);
				strncat(debugstr, temp, 10);
			} else {
				ClientConn.Version = MQCD_CURRENT_VERSION;
			}

			// if (pThrdCtl->trptype[0]!=' ') ClientConn.Version=atoi(pThrdCtl->trptype);

			if (pThrdCtl->scyExit[0] != ' ') {
				strncpy(ClientConn.SecurityExit, pThrdCtl->scyExit,
				MQ_EXIT_NAME_LENGTH);
				strcat(debugstr, ";  SecurityExit=");
				strncat(debugstr, ClientConn.SecurityExit, MQ_EXIT_NAME_LENGTH);
			}

			if (pThrdCtl->sendExit[0] != ' ') {
				strncpy(ClientConn.SendExit, pThrdCtl->sendExit,
				MQ_EXIT_NAME_LENGTH);
				strcat(debugstr, ";  SendExit=");
				strncat(debugstr, ClientConn.SendExit, MQ_EXIT_NAME_LENGTH);
			}

			if (pThrdCtl->rcvExit[0] != ' ') {
				strncpy(ClientConn.ReceiveExit, pThrdCtl->rcvExit,
				MQ_EXIT_NAME_LENGTH);
				strcat(debugstr, ";  ReceiveExit=");
				strncat(debugstr, ClientConn.ReceiveExit, MQ_EXIT_NAME_LENGTH);
			}

			if (pThrdCtl->scyData[0] != ' ') {
				strncpy(ClientConn.SecurityUserData, pThrdCtl->scyData,
				MQ_EXIT_DATA_LENGTH);
				strcat(debugstr, ";  SecurityUserData=");
				strncat(debugstr, ClientConn.SecurityUserData,
				MQ_EXIT_DATA_LENGTH);
			}

			if (pThrdCtl->sendData[0] != ' ') {
				strncpy(ClientConn.SendUserData, pThrdCtl->sendData,
				MQ_EXIT_DATA_LENGTH);
				strcat(debugstr, ";  SendUserData=");
				strncat(debugstr, ClientConn.SendUserData, MQ_EXIT_DATA_LENGTH);
			}

			if (pThrdCtl->rcvData[0] != ' ') {
				strncpy(ClientConn.ReceiveUserData, pThrdCtl->rcvData,
				MQ_EXIT_DATA_LENGTH);
				strcat(debugstr, ";  ReceiveUserData=");
				strncat(debugstr, ClientConn.ReceiveUserData,
				MQ_EXIT_DATA_LENGTH);
			}

			if (pThrdCtl->userid[0] != ' ') {
				strncpy(ClientConn.UserIdentifier, pThrdCtl->userid,
				MQ_USER_ID_LENGTH);
				strcat(debugstr, ";  UserIdentifier=");
				strncat(debugstr, ClientConn.UserIdentifier, MQ_USER_ID_LENGTH);
			}

			if (pThrdCtl->hbint[0] != ' ') {
				ClientConn.HeartbeatInterval = atoi(pThrdCtl->hbint);
				strcat(debugstr, ";  HeartBeatInterval=");
				sprintf(temp, "%ld", ClientConn.HeartbeatInterval);
				strncat(debugstr, temp, 10);
			}

			if (pThrdCtl->kaint[0] != ' ') {
				ClientConn.KeepAliveInterval = atoi(pThrdCtl->kaint);
				strcat(debugstr, ";  KeepAliveInterval=");
				sprintf(temp, "%ld", ClientConn.KeepAliveInterval);
				strncat(debugstr, temp, 10);
			}

			if (pThrdCtl->sslciph[0] != ' ') {
				strncpy(ClientConn.SSLCipherSpec, pThrdCtl->sslciph,
				MQ_SSL_CIPHER_SPEC_LENGTH);
				strcat(debugstr, ";  SSLCipherSpec=");
				strncat(debugstr, ClientConn.SSLCipherSpec,
				MQ_SSL_CIPHER_SPEC_LENGTH);
			}

			if (pThrdCtl->sslpeer[0] != ' ') {
				ClientConn.SSLPeerNamePtr = pThrdCtl->sslpeer;
				ClientConn.SSLPeerNameLength = strlen(pThrdCtl->sslpeer);
				strcat(debugstr, ";  SSLPeerName=");
				strncat(debugstr, (char*) ClientConn.SSLPeerNamePtr,
						ClientConn.SSLPeerNameLength);
			}

			if (pThrdCtl->locladdr[0] != ' ') {
				strncpy(ClientConn.LocalAddress, pThrdCtl->locladdr,
				MQ_LOCAL_ADDRESS_LENGTH);
				strcat(debugstr, ";  LocalAddress=");
				strncat(debugstr, ClientConn.LocalAddress,
				MQ_LOCAL_ADDRESS_LENGTH);
			}

			Connect_options.Options = MQCNO_CLIENT_BINDING;

			/* Point the MQCNO to the client connection definition */
			Connect_options.ClientConnOffset = 0;
			Connect_options.ClientConnPtr = &ClientConn;

			/* Client connection fields are in the version 2 part of the
			 MQCNO so we must set the version number to 2 or they will be ignored */
			Connect_options.Version = MQCNO_VERSION_2;

			/* If a keyRepos was specified, then we need to setup the SCO control block as well */

			if (keyRepos[0]) {
				// point to sco
				Connect_options.SSLConfigPtr = &ssl_Connection_Options;
				// set version
				Connect_options.Version = MQCNO_VERSION_4;
				// copy in keyRepos
				strncpy(ssl_Connection_Options.KeyRepository, keyRepos,
						sizeof(ssl_Connection_Options.KeyRepository));
				strcat(debugstr, "; KeyRepository=");
				strncat(debugstr, ssl_Connection_Options.KeyRepository,
						sizeof(ssl_Connection_Options.KeyRepository));
			}
			if (pThrdCtl->channelUserId[0] != ' '
					&& pThrdCtl->channelPassword[0] != ' ') {
				Connect_options.Version = MQCNO_CURRENT_VERSION;
				csp.AuthenticationType = MQCSP_AUTH_USER_ID_AND_PWD;
				csp.CSPUserIdPtr = pThrdCtl->channelUserId;
				csp.CSPUserIdLength = strlen(pThrdCtl->channelUserId);

				decryptData(pThrdCtl->channelPassword, tempPw);
				if (strlen(tempPw) > 0 && tempPw[strlen(tempPw) - 1] == '\n')
					tempPw[strlen(tempPw) - 1] = 0;
				csp.CSPPasswordPtr = tempPw;
				csp.CSPPasswordLength = strlen(tempPw);
				Connect_options.SecurityParmsPtr = &csp;

			} else {
				sprintf(printstr,
						"Found a channel user name or channel password, but not both.\n");
				sprintf(printstr,
						"%s Channel UserID = %s Encrypted Channel Password = %s",
						printstr, pThrdCtl->channelUserId,
						pThrdCtl->channelPassword);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL,
				DEBUG_ERR, NULL, 4, 0, substr, NULL);
			}

			strcat(debugstr, " (Data is thread control block)");

			if (valueEventLevel > 10) {
				substr[0] = debugstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(THRDCTL), substr, pThrdCtl);
			}

			if (valueEventLevel > 2) {
				char cnoDump[5000];
				sprintf(printstr, "Connecting with %s to QMGR (Thread #%d)",
						verb, pThrdCtl->ThreadID);
				strcat(printstr, " (Data is MQCD control block)");
				substr[0] = printstr;
				printMQCNO(cnoDump, &Connect_options);
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(ClientConn), substr, &ClientConn);
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(cnoDump), substr, NULL);
			}

			tsCONNX(pThrdCtl->QMgr, /* queue manager*/
			&Connect_options, /* options for connection         */
			&Hcon, /* connection handle*/
			&CompCode, /* completion code*/
			&CReason); /* reason code*/
			if (0 != csp.CSPPasswordPtr) {
				memset(csp.CSPPasswordPtr, '\0', sizeof(csp.CSPPasswordPtr));
				csp.CSPPasswordPtr = 0;
			}
			// if we get this, we ignore it
			if (CReason == MQRC_SSL_ALREADY_INITIALIZED) {
				CompCode = MQCC_OK;
				CReason = MQRC_NONE;
			}

		} else { // Just normal MQCONN connection
			strcpy(verb, "MQCONN");
			if (valueEventLevel > 2) {
				sprintf(printstr, "Connecting with %s to QMGR (Thread #%d)",
						verb, pThrdCtl->ThreadID);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(pThrdCtl->QMgr), substr,
						&(pThrdCtl->QMgr));
			}
			tsCONN(pThrdCtl->QMgr, /* queue manager*/
			&Hcon, /* connection handle*/
			&CompCode, /* completion code*/
			&CReason); /* reason code*/
		} // end of if client connection (connx)

		if (CompCode == MQCC_OK) {
			/* save the connection handle */
			pThrdCtl->hConn = Hcon;
			if (valueEventLevel > 10) {
				sprintf(printstr,
						"Saved connection, now getting real qmgr name.(Thread #%d)",
						pThrdCtl->ThreadID);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(pThrdCtl->QMgr), substr,
						&(pThrdCtl->QMgr));
			}

			strcpy(verb, "MQOPEN/INQ");
			CReason = getRealQmgrName(Hcon, realqmgrname, pThrdCtl->ThreadID);
			if (0 != CReason) {
				CompCode = MQCC_FAILED;/* get the real qmgr name*/
			}
			if (valueEventLevel > 10) {
				sprintf(printstr,
						"getRealQmgr returned %s with %d(Thread #%d).",
						realqmgrname, CReason, pThrdCtl->ThreadID);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(pThrdCtl->QMgr), substr,
						&(pThrdCtl->QMgr));
			}

			if (pThrdCtl->Queue[0] != PLACEHOLDER) {
				if (valueEventLevel > 2) {
					sprintf(printstr,
							"This thread (Thread #%d) is a running a trigger monitor.",
							pThrdCtl->ThreadID);
					substr[0] = printstr;
					ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
					DEBUG_ERR, NULL, 1, sizeof(pThrdCtl->QMgr), substr,
							&(pThrdCtl->QMgr));
				}
				/* This entry is a trigger monitor entry */
				if (CompCode == MQCC_OK) {
					strcpy(od.ObjectName, pThrdCtl->Queue);
					strcpy(verb, "MQOPEN");
					tsOPEN(Hcon, /* connection handle*/
					&od, /* object descriptor for queue*/
					MQOO_INPUT_AS_Q_DEF /* open queue for input*/
					+ MQOO_FAIL_IF_QUIESCING, /* but not if MQM stopping*/
					&Hobj, /* object handle*/
					&CompCode, /* completion code*/
					&CReason); /* reason code*/

				} /* end of if compcode == ok */

				if (CompCode == MQCC_OK) {

					OpenCode = CompCode; /* keep for conditional close*/
					buflen = sizeof(trigger); /* size of all trigger messages*/
					while (CompCode != MQCC_FAILED
							&& pThrdCtl->threadStatus != THREAD_EXITING) {
						if (valueEventLevel > 10) {
							sprintf(printstr, "CompCode = %d ", CompCode);
							substr[0] = printstr;
							ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE,
							NULL,
							DEBUG_ERR, NULL, 1, sizeof(md), substr, &md);
						}
						gmo.Options = MQGMO_WAIT /* wait for new messages ...*/
						+ MQGMO_FAIL_IF_QUIESCING /* or until MQM stopping*/
						+ MQGMO_ACCEPT_TRUNCATED_MSG /* remove all long messages*/
						+ MQGMO_CONVERT /* convert TM */
						+ MQGMO_SYNCPOINT;
						gmo.WaitInterval = valueWaitInterval; /* time limit as spec'd by user */

						memcpy(md.MsgId, MQMI_NONE, sizeof(md.MsgId));
						memcpy(md.CorrelId, MQCI_NONE, sizeof(md.CorrelId));
						strcpy(verb, "MQGET");
						tsGET(Hcon, /* connection handle*/
						Hobj, /* object handle*/
						&md, /* message descriptor*/
						&gmo, /* get message options*/
						buflen, /* buffer length*/
						&trigger, /* trigger message buffer*/
						&triglen, /* message length*/
						&CompCode, /* completion code*/
						&CReason); /* reason code*/

						if (valueEventLevel > 3) {
							sprintf(printstr, "Rc = %d from MQGET", CReason);
							substr[0] = printstr;
							ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE,
							NULL,
							DEBUG_ERR, NULL, 1, sizeof(md), substr, &md);
						}

						if (CReason == MQRC_NONE
								|| CReason == MQRC_NO_MSG_AVAILABLE) {
							numberOfRetries = 0; /* reset number of retrys, since we succeded */
						}

						/* We do a GET with syncpoint to ensure that we don't loose
						 ** a trigger message if the connection was broken, so we
						 ** now need to commit the removal of the message
						 ** from the initiation queue.
						 */
						if (CReason == MQRC_NONE) {
							tsCMIT(Hcon, &CompCode, &DReason);
							if (DReason || valueEventLevel > 2) {
								substr[0] = "MQCMIT";
								sprintf(printstr, "%4d", DReason);
								substr[1] = printstr;
								substr[2] = pThrdCtl->Queue;
								substr[3] = pThrdCtl->QMgr;
								ReportEvent(logHandle,
								EVENTLOG_INFORMATION_TYPE, NULL,
								MQAPI_ERR, NULL, 4, 0, substr, NULL);
							}
						}

						if (CReason != MQRC_NO_MSG_AVAILABLE) {
							if (CReason == MQRC_NONE) {
								if (triglen != buflen || memcmp(trigger.StrucId,
								MQTM_STRUC_ID, sizeof(trigger.StrucId))) {
									substr[0] = pThrdCtl->Queue;
									sprintf(printstr,
											"User = \"%.12s\", Appl = \"%.28s\", Msg Length = %ld",
											md.UserIdentifier, md.PutApplName,
											triglen);
									substr[1] = printstr;
									ReportEvent(logHandle,
									EVENTLOG_WARNING_TYPE, NULL,
									BAD_TRIG_LEN, NULL, 2, triglen, substr,
											&trigger);
								} else {

									launchApplication(trigger, realqmgrname,
											pThrdCtl->NotesIni);

								} // end not success
							} /* end process for successful GET*/

						} else {
							CReason = MQRC_NONE; // treat no mesg available as an okay condition
							CompCode = MQCC_OK;
						} // end if ... else no msg available
					} // end while rc = 0 loop

				} /* end of compcode = ok from MQCONN and MQOPEN */
			} else if (pThrdCtl->ServiceName[0] != PLACEHOLDER) {
				/* this entry is an MQ Service entry */
				if (valueEventLevel > 2) {
					sprintf(printstr,
							"This thread (Thread #%d) is a running a service.",
							pThrdCtl->ThreadID);
					substr[0] = printstr;
					ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
					DEBUG_ERR, NULL, 1, sizeof(pThrdCtl->QMgr), substr,
							&(pThrdCtl->QMgr));
				}
				if (valueEventLevel > 10) {
					sprintf(printstr, "Get Service Info for Thread #%d",
							pThrdCtl->ThreadID);
					substr[0] = printstr;
					//				sprintf((char*)substr[1],"tsOPEN = &d",tsOPEN);
					//substr[1] = strcat("",printstr);
					ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
					DEBUG_ERR, NULL, 1, sizeof(pThrdCtl->ServiceName), substr,
							&(pThrdCtl->ServiceName));
				}
				CReason = getServiceInfo(Hcon, pThrdCtl);
				bool startService = true;
				DWORD serviceStatus;
				bool rc;
				//open a temporary queue, use it to decide when to issue the stop command.
				if (CompCode == MQCC_OK) {
					strcpy(od.ObjectName, "SYSTEM.DEFAULT.MODEL.QUEUE");
					strcpy(od.DynamicQName, "MA7K.SVC.*");
					strcpy(verb, "MQOPEN");
					tsOPEN(Hcon, /* connection handle*/
					&od, /* object descriptor for queue*/
					MQOO_INPUT_AS_Q_DEF /* open queue for input*/
					+ MQOO_FAIL_IF_QUIESCING, /* but not if MQM stopping*/
					&Hobj, /* object handle*/
					&CompCode, /* completion code*/
					&CReason); /* reason code*/

				} /* end of if compcode == ok */

				if (CompCode == MQCC_OK) {
					while ((startService || CompCode != MQCC_FAILED)
							&& pThrdCtl->threadStatus != THREAD_EXITING) {
						if (startService) {
							startService = false;
							launchMQService(pThrdCtl, true);
						}
						if (valueEventLevel > 10) {
							sprintf(printstr, "CompCode = %d startService= %d",
									CompCode, startService);
							substr[0] = printstr;
							ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE,
							NULL,
							DEBUG_ERR, NULL, 1, sizeof(md), substr, &md);
						}

						gmo.Options = MQGMO_WAIT /* wait for new messages ...*/
						+ MQGMO_FAIL_IF_QUIESCING /* or until MQM stopping*/
						+ MQGMO_ACCEPT_TRUNCATED_MSG /* remove all long messages*/
						+ MQGMO_CONVERT /* convert TM */
						+ MQGMO_NO_SYNCPOINT;
						gmo.WaitInterval = valueWaitInterval; /* time limit as spec'd by user */

						memcpy(md.MsgId, MQMI_NONE, sizeof(md.MsgId));
						memcpy(md.CorrelId, MQCI_NONE, sizeof(md.CorrelId));
						strcpy(verb, "MQGET");
						tsGET(Hcon, /* connection handle*/
						Hobj, /* object handle*/
						&md, /* message descriptor*/
						&gmo, /* get message options*/
						buflen, /* buffer length*/
						&trigger, /* trigger message buffer*/
						&triglen, /* message length*/
						&CompCode, /* completion code*/
						&CReason); /* reason code*/

						if (valueEventLevel > 3) {
							sprintf(printstr, "Rc = %d from MQGET", CReason);
							substr[0] = printstr;
							ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE,
							NULL,
							DEBUG_ERR, NULL, 1, sizeof(md), substr, &md);
						}

						if (CReason == MQRC_NO_MSG_AVAILABLE) {
							numberOfRetries = 0; /* reset number of retrys, since we succeded */
							CReason = MQRC_NONE; // treat no mesg available as an okay condition
							CompCode = MQCC_OK;
						} else if (CReason == MQRC_NONE) {
							numberOfRetries = 0; /* reset number of retrys, since we succeded */
							if (md.Feedback == MQFB_QUIT)
								CompCode = MQCC_FAILED; // exit the processing loop
						}
						//					rc = GetExitCodeProcess(pThrdCtl->servInfo->hProcess,&serviceStatus);
						//					if (rc&&serviceStatus == STILL_ACTIVE) {
						//						Sleep(sleepInterval*1000);
						//					} else {
						//						CloseHandle(pThrdCtl->servInfo->hProcess);
						//						pThrdCtl->servInfo->hProcess = 0;
						//					}
					}
					// while not done
					launchMQService(pThrdCtl, false);
				}
			} else {
				if (valueEventLevel > 10) {
					sprintf(printstr,
							"This thread (Thread #%d) has failed because neither thread nor service matched. Queue: %s Service: %s",
							pThrdCtl->ThreadID, pThrdCtl->Queue,
							pThrdCtl->ServiceName);
					substr[0] = printstr;
					ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
					DEBUG_ERR, NULL, 1, sizeof(pThrdCtl->QMgr), substr,
							&(pThrdCtl->QMgr));
				}
			}
		}

		if (CReason != MQRC_CONNECTION_BROKEN && Hobj != MQHO_UNUSABLE_HOBJ) {
			tsCLOSE(Hcon, /* connection handle*/
			&Hobj, /* object handle*/
			0, &CompCode, /* completion code*/
			&DReason); /* reason code*/

		}
		if (CReason != MQRC_CONNECTION_BROKEN && Hcon != MQHC_UNUSABLE_HCONN) {
			tsDISC(&Hcon, /* connection handle*/
			&CompCode, /* completion code*/
			&DReason); /* reason code*/
			pThrdCtl->hConn = MQHC_UNUSABLE_HCONN;

		}

		// make the decision here whether or not to retry the connect
		if (CReason == MQRC_Q_MGR_NOT_AVAILABLE
				|| CReason == MQRC_CONNECTION_QUIESCING
				|| CReason == MQRC_CONNECTION_BROKEN
				|| CReason == MQRC_RECONNECT_FAILED //v150 additions
				|| CReason == MQRC_GET_INHIBITED
				|| CReason == MQRC_OBJECT_CHANGED
				|| CReason == MQRC_OBJECT_IN_USE
				|| CReason == MQRC_Q_MGR_QUIESCING
				|| CReason == MQRC_Q_MGR_NAME_ERROR // v141 addition
				|| CReason == MQRC_HOST_NOT_AVAILABLE //v150 additions
				|| CReason == MQRC_CHANNEL_NOT_AVAILABLE
				|| CReason == MQRC_Q_MGR_STOPPING) {
			if (numberOfRetries < valueLongRty + valueShortRty) {
				if (valueEventLevel > 1 && numberOfRetries == 0) { // put event msg first time
					substr[0] = verb;
					sprintf(printstr, "%4d", CReason);
					substr[1] = printstr;
					substr[2] = pThrdCtl->Queue;
					substr[3] = pThrdCtl->QMgr;
					ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL,
					MQAPI_ERR_RETRY, NULL, 4, 0, substr, NULL);
				}

				EnterCriticalSection(&ThreadCritSect);
				pThrdCtl->threadStatus = THREAD_PAUSED;
				UpdateStatusFromThreads();
				LeaveCriticalSection(&ThreadCritSect);

				retry = TRUE;
				if (numberOfRetries < valueShortRty)
					sleepInterval = valueShortTmr;
				else
					sleepInterval = valueLongTmr;
				numberOfRetries++;
				if (valueEventLevel > 2) {
					sprintf(printstr,
							"Rc = %d Sleeping for %d, numberOfRetries %d",
							CReason, sleepInterval, numberOfRetries);
					substr[0] = printstr;
					ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
					DEBUG_ERR, NULL, 1, 0, substr, NULL);
				}
				Sleep(sleepInterval * 1000);
			} else { // too many retries
				retry = FALSE;
				substr[0] = verb;
				sprintf(printstr, "%4d", CReason);
				substr[1] = printstr;
				substr[2] = pThrdCtl->Queue;
				substr[3] = pThrdCtl->QMgr;
				ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR,
				NULL, 4, 0, substr, NULL);
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				TOO_MANY_RETRIES, NULL, 0, 0, NULL, NULL);
			} // end of if .. else retry decision
		} else { // not a qualifying condition for retrying
			retry = FALSE;
			if (CReason != MQRC_NONE) {
				substr[0] = verb;
				sprintf(printstr, "%4d", CReason);
				substr[1] = printstr;
				substr[2] = pThrdCtl->Queue;
				substr[3] = pThrdCtl->QMgr;
				ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR,
						NULL, 4, 0, substr, NULL);
			}
		} // end of else not a qualifying condition
	}/* End while number of retries */

	if (pThrdCtl->Queue[0] != PLACEHOLDER) {
		sprintf(printstr, "queue %s", pThrdCtl->Queue);
		substr[0] = printstr;
		sprintf(printstr, "%d", pThrdCtl->ThreadID);
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, EXITING_THREAD,
				NULL, 2, 0, substr, NULL);
	} else if (pThrdCtl->ServiceName[0] != PLACEHOLDER) {
		sprintf(printstr, "service %s", pThrdCtl->ServiceName);
		substr[0] = printstr;
		sprintf(printstr, "%d", pThrdCtl->ThreadID);
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, EXITING_THREAD,
				NULL, 2, 0, substr, NULL);
	} else {

	}
	substr[0] = pThrdCtl->Queue;
	sprintf(printstr, "%d", pThrdCtl->ThreadID);
	substr[1] = printstr;
	ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, EXITING_THREAD,
			NULL, 2, 0, substr, NULL);

	EnterCriticalSection(&ThreadCritSect);
	pThrdCtl->threadStatus = THREAD_EXITING;
	UpdateStatusFromThreads();
	LeaveCriticalSection(&ThreadCritSect);

	return 0;

} // end of service thread

//*******************************************************************************
//
//  This routine gets the name of the qmgr that we are connected to
//  This name is used in building the character version of the trigger message
//
//*******************************************************************************

MQLONG getRealQmgrName(MQHCONN Hconn, MQCHAR48 qmname, int tid) {
	MQLONG cc;
	MQLONG rc;
	MQLONG Hobj;
	MQOD od = { MQOD_DEFAULT }; /* Object  Descriptor */
	MQLONG sel1 = MQCA_Q_MGR_NAME; /* selector for MQINQ call*/
	LPCSTR substr[4];
	char printstr[256];

	// do an MQINQ to get the real name of the queue manager
	if (valueEventLevel > 10) {
		sprintf(printstr, "Entered getRealQmgrName in Thread #%d.", tid);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(qmname), substr, &(qmname));
	}
	od.ObjectType = MQOT_Q_MGR;
	tsOPEN(Hconn, &od, MQOO_INQUIRE, &Hobj, &cc, &rc);
	if (cc != MQCC_OK) {
		substr[0] = "QMGR Object: MQOPEN for INQ";
		sprintf(printstr, "%4d in Thread #%d.", rc, tid);
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR, NULL, 2, 0,
				substr, NULL);
		return rc;
	}
	tsINQ(Hconn, Hobj, 1, &sel1, 0, NULL, MQ_Q_MGR_NAME_LENGTH, qmname, &cc,
			&rc);
	if (cc != MQCC_OK) {
		substr[0] = "QMGR Object: MQINQ";
		sprintf(printstr, "%4d in Thread #%d.", rc, tid);
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR, NULL, 2, 0,
				substr, NULL);

		return rc;
	}
	tsCLOSE(Hconn, &Hobj, MQCO_NONE, &cc, &rc);
	if (valueEventLevel > 10) {
		sprintf(printstr, "Exiting getRealQmgrName, found: %s in Thread #%d.",
				qmname, tid);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(qmname), substr, &(qmname));
	}
	return (MQCC_OK);
}

//**************************************************************************************
//
// This routine builds a character version of the trigmsg and then launches
// the application.
//
//**************************************************************************************

void launchApplication(MQTM trigger, char * realqmgrname, char * NotesIni) {

	struct {
		char program[sizeof(trigger.ApplId) + 2];
		char fill1[2]; /* blank, the double quote */
		MQTMC2 trig; /* character version of TM */
		char fill2[2]; /* double quote, then blank*/
		char EnvData[sizeof(trigger.EnvData)];
		char fill3; /* null character */
	} SystemParm;

	char printstr[256];
	char printstr1[256];
	LPCSTR substr[3];
	int sysRc = 0;
	STARTUPINFO startUpInfo;
	PROCESS_INFORMATION procInfo;
	char command[sizeof(trigger.ApplId)];
	int success = 0;
	int lastError = 0;
	char *p;

	memset(&SystemParm, ' ', sizeof(SystemParm));
	memcpy(SystemParm.trig.StrucId, MQTMC_STRUC_ID,
			sizeof(SystemParm.trig.StrucId));
	memcpy(SystemParm.trig.Version, MQTMC_CURRENT_VERSION,
			sizeof(SystemParm.trig.Version));
	memcpy(SystemParm.trig.QName, trigger.QName, sizeof(SystemParm.trig.QName));
	memcpy(SystemParm.trig.ProcessName, trigger.ProcessName,
			sizeof(SystemParm.trig.ProcessName));
	memcpy(SystemParm.trig.TriggerData, trigger.TriggerData,
			sizeof(SystemParm.trig.TriggerData));
	// I don't agree with it, but there seems to be a feeling
	// that the application type field should be a blank
	// at least that's what the APR says .....
	// sprintf(c5, "%4d", trigger.ApplType);
	// memcpy(SystemParm.trig.ApplType, c5, sizeof(SystemParm.trig.ApplType));
	memcpy(SystemParm.trig.ApplId, trigger.ApplId,
			sizeof(SystemParm.trig.ApplId));
	memcpy(SystemParm.trig.EnvData, trigger.EnvData,
			sizeof(SystemParm.trig.EnvData));
	memcpy(SystemParm.trig.UserData, trigger.UserData,
			sizeof(SystemParm.trig.UserData));
	memcpy(SystemParm.trig.QMgrName, realqmgrname, MQ_Q_MGR_NAME_LENGTH);

	memcpy(SystemParm.fill1, " \"", sizeof(SystemParm.fill1));
	memcpy(SystemParm.fill2, "\" ", sizeof(SystemParm.fill2));
	SystemParm.fill3 = '\0';

	switch (trigger.ApplType) {

	case (MQAT_WINDOWS_NT):
	case (MQAT_WINDOWS):
	case (MQAT_DOS):

		memcpy(SystemParm.program, trigger.ApplId, sizeof(trigger.ApplId));

		/* Pass Environment data after trigger message */
		memcpy(SystemParm.EnvData, trigger.EnvData, sizeof(trigger.EnvData));

		if (valueEventLevel > 1) {
			substr[0] = (char*) &SystemParm;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, CMD_MSG,
			NULL, 1, sizeof(SystemParm), substr, &SystemParm);
		}

		// Issue the command to start the program
		sysRc = system((char*) &SystemParm);
		if (0 != sysRc) {
			if (sysRc == 1) { // could not find executable
				ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL,
				PROC_NOEXEC, NULL, 0, sizeof(SystemParm), substr, &SystemParm);
			} else {
				//				lastError = GetLastError();
				lastError = errno;
				sprintf(printstr, "%4d", sysRc);
				sprintf(printstr1, "%4d", lastError);
				substr[0] = printstr;
				substr[1] = printstr1;
				ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL, PROC_WARN,
				NULL, 2, sizeof(SystemParm), substr, &SystemParm);
			}
		}

		break;

	case (MQAT_NOTES_AGENT):

		strcpy(SystemParm.program, "\"");
		strcat(SystemParm.program, MyPath);
		strcat(SystemParm.program, "\\");
		strcat(SystemParm.program, NOTESEXE);
		strcat(SystemParm.program, "\"");
		SystemParm.program[strlen(SystemParm.program)] = ' ';

		sprintf(SystemParm.EnvData, "%.4d", valueEventLevel);
		strcat(SystemParm.EnvData, " \""); // v1.3 add notesini to parm list
		strcat(SystemParm.EnvData, NotesIni); // v1.3 add notesini to parm list
		strcat(SystemParm.EnvData, "\""); // v1.3 add notesini to parm list

		if (valueEventLevel > 1) {
			substr[0] = (char*) &SystemParm;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, CMD_MSG,
			NULL, 1, sizeof(SystemParm), substr, &SystemParm);
		}

		strcpy(command, MyPath);
		strcat(command, "\\");
		strcat(command, NOTESEXE);

		GetStartupInfo(&startUpInfo);
		strcpy(startUpInfo.lpTitle, "Running Lotus Notes Agent ...");

		// See prolog comments for v131 for explanation of why CreateProcess is used ...

		success = CreateProcess(command, (char *) &SystemParm, 0, 0, FALSE,
		CREATE_NEW_CONSOLE, 0, 0, &startUpInfo, &procInfo);
		if (!success) {
			if (GetLastError() == 2) { // counldn't find executable
				substr[0] = command;
				ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL,
				PROC_NOEXEC, NULL, 1, 0, substr, NULL);
			} else {
				sprintf(printstr, "%.4d", GetLastError());
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL, PROC_WARN,
				NULL, 1, 0, substr, NULL);
			}
		} else {
			// Close process and thread handles.
			CloseHandle(procInfo.hProcess);
			CloseHandle(procInfo.hThread);
		}

		break;

	default:

		sprintf(printstr, "%d", trigger.ApplType);
		substr[0] = printstr;

		ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL, BAD_APPL_TYPE,
		NULL, 1, sizeof(trigger), substr, &trigger);
		return;
	} // end switch

} // end launch application

void launchMQService(PTHRDCTL thdCntl, bool start) {

	char printstr[1024];
	char printstr1[1024];
	LPCSTR substr[3];
	int sysRc = 0;
	char command[MQ_SERVICE_COMMAND_LENGTH + MQ_SERVICE_ARGS_LENGTH + 1];
	int success = 0;
	int lastError = 0;
	char *p;

	if (valueEventLevel > 10) {
		sprintf(printstr, "Entered launchMQService with start = %s\n",
				(start ? "true" : "false"));
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
		NULL, 1, sizeof(thdCntl->ServiceName), substr, &(thdCntl->ServiceName));
	}

	memset(command, ' ', sizeof(command));
	if (start) {
		if (strncmp(thdCntl->servInfo->StartCommand, "MA7K", 4) == 0) {
			strcpy(command, (thdCntl->servInfo->StartCommand + 4));
		} else {
			strcpy(command, thdCntl->servInfo->StartCommand);
		}
		strcat(command, " ");
		strcat(command, (char*) thdCntl->servInfo->StartArgs);
	} else {
		if (strncmp(thdCntl->servInfo->StopCommand, "MA7K", 4) == 0) {
			strcpy(command, (thdCntl->servInfo->StopCommand + 4));
		} else {
			strcpy(command, thdCntl->servInfo->StopCommand);
		}
		strcat(command, " ");
		strcat(command, (char*) thdCntl->servInfo->StopArgs);

	}
	if (valueEventLevel > 10) {
		sprintf(printstr, "Created command line: '%s'\n ", command);
		substr[1] = printstr1;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
		NULL, 1, sizeof(thdCntl->ServiceName), substr, &(thdCntl->ServiceName));
	}
	sysRc = system(command);
	if (0 != sysRc) {
		if (sysRc == 1) { // could not find executable
			ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL, PROC_NOEXEC,
			NULL, 0, sizeof(command), substr, &command);
		} else {
			//				lastError = GetLastError();
			lastError = errno;
			sprintf(printstr, "%4d", sysRc);
			sprintf(printstr1, "%4d", lastError);
			substr[0] = printstr;
			substr[1] = printstr1;
			ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL, PROC_WARN,
			NULL, 2, sizeof(command), substr, &command);
		}
	} else {
		if (valueEventLevel > 10) {
			sprintf(printstr, "successfully ran command line: '%s'\n ",
					command);
			substr[1] = printstr1;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
			NULL, 1, sizeof(thdCntl->ServiceName), substr,
					&(thdCntl->ServiceName));
		}

	}
	/*	success = CreateProcess(command, args, 0, 0, FALSE, CREATE_NO_WINDOW, 0,
	 0, &startUpInfo, &procInfo);
	 if (!success) {
	 if (GetLastError()==2) { // counldn't find executable
	 substr[0] = command;
	 ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL,
	 PROC_NOEXEC, NULL, 1, 0, substr, NULL);
	 } else {
	 sprintf(printstr, "%.4d", GetLastError());
	 substr[0] = printstr;
	 ReportEvent(logHandle, EVENTLOG_WARNING_TYPE, NULL, PROC_WARN,
	 NULL, 1, 0, substr, NULL);
	 }
	 }
	 CloseHandle(procInfo.hThread);
	 thdCntl->servInfo->hProcess = procInfo.hProcess;
	 */
	if (valueEventLevel > 10) {
		sprintf(printstr, "Exited launchMQService");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
		NULL, 1, sizeof(thdCntl->ServiceName), substr, &(thdCntl->ServiceName));
	}
}

//*******************************************************************************
//
//  This routine gets the contents of the service that's been requested.
//
//*******************************************************************************

MQLONG getServiceInfo(MQHCONN Hconn, PTHRDCTL thdCntl) {
	MQCHAR48 QName;
	MQCHAR48 *DynamicQName;
	MQLONG OpenOpts;
	MQLONG CompCode = 0; /* MQ API completion code          */
	MQLONG Reason = 0; /* Reason qualifying above         */
	MQLONG AdminMsgLen; /* Length of user message buffer   */
	MQLONG UserMsgLen; /* Actual length received        */
	MQBYTE *pAdminMsg; /* Ptr to outbound data buffer     */
	MQBYTE pAdminReply[4096]; /* Ptr to inbound data buffer     */
	MQCFH *pPCFHeader; /* Ptr to PCF header structure     */
	MQCFST *pPCFString; /* Ptr to PCF string parm block    */
	MQCFSL *pPCFStringList;/* Ptr to PCF string parm block    */
	MQCFIN *pPCFInteger; /* v32 ptr to chltype param for clntconn */
	MQCFIL *pPCFIntegerList; /* v32 ptr to chltype param for clntconn */
	MQCFBS *pPCFByte; /* Ptr to PCF byte string parm block */
	MQLONG *pPCFType = NULL; /* Type field of PCF message parm  */
	MQMD MsgDesc = { MQMD_DEFAULT }; /* Message description        */
	MQMD MsgDesc_V = { MQMD_DEFAULT }; /* Message descriptor (def values) */
	MQGMO GetMsgOpts = { MQGMO_DEFAULT }; /* Options to control MQGET      */
	MQPMO PutMsgOpts = { MQPMO_DEFAULT }; /* Controls action of MQPUT   */
	MQHOBJ hAdminQ = MQHO_UNUSABLE_HOBJ; /* handle to output queue          */
	MQHOBJ hReplyQ = MQHO_UNUSABLE_HOBJ; /* handle to input queue           */
	MQOD ObjDesc = { MQOD_DEFAULT };
	PSERVINFO myService;
	LPCSTR substr[4];
	char printstr[256];

	if (valueEventLevel > 10) {
		sprintf(printstr, "entered getServiceInfo");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
		sprintf(printstr, "tsOPEN is %d", tsOPEN);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, 0, substr,
		NULL);

	}
	memcpy(ObjDesc.ObjectName, thdCntl->CmdSvrQ, MQ_Q_NAME_LENGTH);
	memcpy(ObjDesc.ObjectQMgrName, thdCntl->QMgr, MQ_Q_MGR_NAME_LENGTH);
	if (valueEventLevel > 10) {
		sprintf(printstr, "Constructed MQOD objectName:%s objectQmgrName:%s ",
				ObjDesc.ObjectName, ObjDesc.ObjectQMgrName);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}
	thdCntl->servInfo->hProcess = 0;
	if (valueEventLevel > 10) {
		sprintf(printstr, "Cleared HProcess");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
		sprintf(printstr,
				"checking hadminq = %d is unusable hobj = %d equality is  %d",
				thdCntl->servInfo->hAdminQ, MQHO_UNUSABLE_HOBJ,
				(thdCntl->servInfo->hAdminQ == MQHO_UNUSABLE_HOBJ));
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}

	if (thdCntl->servInfo->hAdminQ == MQHO_UNUSABLE_HOBJ) {
		if (valueEventLevel > 10) {
			sprintf(printstr, "About to open adminq\n");
			substr[0] = printstr;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
			DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
					&(thdCntl->ServiceName));
		}
		tsOPEN(Hconn /* I  : Queue manager handle   */
		, &ObjDesc /* IO : queue attributes       */
		, MQOO_OUTPUT + MQOO_FAIL_IF_QUIESCING /* I  :                        */
		, &hAdminQ/*  O : handle to open queue   */
		, &CompCode /*  O : MQCC_OK/Warning/Error       */
		, &Reason /*  O : Reason for above       */
		);

		if (valueEventLevel > 10) {
			sprintf(printstr, "Opened adminq, checking CC=%4d RC=%4d", CompCode,
					Reason);
			substr[0] = printstr;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
			DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
					&(thdCntl->ServiceName));
		}
		if (CompCode != MQCC_OK) {
			sprintf(printstr, "MQOPEN failed for %s", ObjDesc.ObjectName);
			substr[0] = printstr;
			sprintf(printstr, "CC=%4d RC=%4d", CompCode, Reason);
			substr[1] = printstr;
			ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR, NULL,
					2, 0, substr, NULL);
			thdCntl->servInfo->hAdminQ = MQHO_UNUSABLE_HOBJ;
			return Reason;
		} /* endif */
		thdCntl->servInfo->hAdminQ = hAdminQ;
	} else {
		if (valueEventLevel > 10) {
			sprintf(printstr, "About to restore adminq");
			substr[0] = printstr;
			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
			DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
					&(thdCntl->ServiceName));
		}
		hAdminQ = thdCntl->servInfo->hAdminQ;
	}

	if (valueEventLevel > 10) {
		sprintf(printstr, "opened adminq or reacquired handle to same.");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}
	memcpy(ObjDesc.ObjectName, "SYSTEM.DEFAULT.MODEL.QUEUE\0",
	MQ_Q_NAME_LENGTH);
	memcpy(ObjDesc.ObjectQMgrName, thdCntl->QMgr, MQ_Q_MGR_NAME_LENGTH);
	DynamicQName = (MQCHAR48 *) malloc(MQ_Q_NAME_LENGTH + 1);
	memset(DynamicQName, '\0', MQ_Q_NAME_LENGTH + 1);
	memcpy(DynamicQName, "MA7K.ADM.*", 11);
	memcpy(ObjDesc.DynamicQName, *DynamicQName, MQ_Q_NAME_LENGTH);
	if (MQHO_UNUSABLE_HOBJ == thdCntl->servInfo->hReplyQ) {
		tsOPEN(Hconn /* I  : Queue manager handle   */
		, &ObjDesc /* IO : queue attributes       */
		, MQOO_INPUT_EXCLUSIVE/* I  :                        */
		, &hReplyQ/*  O : handle to open queue   */
		, &CompCode /*  O : MQCC_OK/Warning/Error       */
		, &Reason /*  O : Reason for above       */
		);
		if (CompCode != MQCC_OK) {
			sprintf(printstr, "MQOPEN failed for %s", ObjDesc.DynamicQName);
			substr[0] = strcat("", printstr);
			sprintf(printstr, "CC=%4d RC=%4d", CompCode, Reason);
			substr[1] = strcat("", printstr);
			ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR, NULL,
					2, 0, substr, NULL);
			thdCntl->servInfo->hReplyQ = MQHO_UNUSABLE_HOBJ;
			return Reason;
		} /* endif */
	} else {
		hReplyQ = thdCntl->servInfo->hReplyQ;
	}
	if (valueEventLevel > 10) {
		sprintf(printstr, "opened replyq or reacquired handle to same.");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}

	/* --------------------------------------------------------------- */
	/* If we have opened a dynamic queue then the actual name supplied */
	/* has been returned in the object descriptor in place of the      */
	/* supplied model queue name                                       */
	/* --------------------------------------------------------------- */
	if (DynamicQName != NULL) {
		memcpy(DynamicQName, ObjDesc.ObjectName, MQ_Q_NAME_LENGTH);
		sprintf(printstr, "Reply queue is %s.\n", DynamicQName);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	} /* endif */

	/* Set the length for the message buffer */
	AdminMsgLen = MQCFH_STRUC_LENGTH + MQCFST_STRUC_LENGTH_FIXED
			+ MQ_SERVICE_NAME_LENGTH;

	/* Allocate storage for the message buffer and set a pointer to */
	/* its start address.                                           */
	pAdminMsg = (MQBYTE *) malloc(AdminMsgLen);

	memset(pAdminMsg, '\0', AdminMsgLen);

	/* pPCFHeader is set equal to pAdminMsg in order to provide a */
	/* structure to the newly allocated block of storage. We can  */
	/* then create a request header of the correct format in the  */
	/* message buffer.                                            */
	pPCFHeader = (MQCFH *) pAdminMsg;

	/* pPCFString is set to point into the message buffer immediately */
	/* after the request header. This allows us to specify a context  */
	/* for the request in the required format.                        */
	pPCFString = (MQCFST *) (pAdminMsg + MQCFH_STRUC_LENGTH);

	/* Setup request header */
	pPCFHeader->Type = MQCFT_COMMAND;
	pPCFHeader->StrucLength = MQCFH_STRUC_LENGTH;
	pPCFHeader->Version = MQCFH_VERSION_1;
	pPCFHeader->Command = MQCMD_INQUIRE_SERVICE;
	pPCFHeader->MsgSeqNumber = MQCFC_LAST;
	pPCFHeader->Control = MQCFC_LAST;
	pPCFHeader->ParameterCount = 1;

	/* Setup parameter block */
	pPCFString->Type = MQCFT_STRING;
	pPCFString->StrucLength =
	MQCFST_STRUC_LENGTH_FIXED + MQ_SERVICE_NAME_LENGTH;
	pPCFString->Parameter = MQCA_SERVICE_NAME;
	pPCFString->CodedCharSetId = MQCCSI_DEFAULT;
	pPCFString->StringLength = strlen(thdCntl->ServiceName);
	pPCFString->StringLength = MQ_SERVICE_NAME_LENGTH;
	memset(pPCFString->String, '\0', MQ_SERVICE_NAME_LENGTH);
	strcpy(pPCFString->String, thdCntl->ServiceName);

	if (valueEventLevel > 10) {
		sprintf(printstr, "populated service name with %s.",
				pPCFString->String);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, AdminMsgLen, substr, pAdminMsg);
	}

	MsgDesc.Persistence = MQPER_NOT_PERSISTENT;
	memset(MsgDesc.ReplyToQMgr, '\0', MQ_Q_MGR_NAME_LENGTH);
	MsgDesc.MsgType = MQMT_REQUEST;
	memcpy(MsgDesc.ReplyToQ, DynamicQName, MQ_Q_NAME_LENGTH);
	memcpy(MsgDesc.Format, MQFMT_ADMIN, MQ_FORMAT_LENGTH);

	MsgDesc.Expiry = 1800; /* 3 minutes */
	if (valueEventLevel > 10) {
		sprintf(printstr, "Constructed INQUIRE SERVICE message for service %s.",
				thdCntl->ServiceName);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, AdminMsgLen, substr, pAdminMsg);
	}

	PutMsgOpts.Options = MQPMO_NO_SYNCPOINT;
	PutMsgOpts.Options = MQPMO_NO_SYNCPOINT;
	tsPUT(Hconn /* I  : Queue manager handle     */
	, hAdminQ /* I  : Queue handle             */
	, &MsgDesc /* IO : Message attributes       */
	, &PutMsgOpts /* IO : Control action of MQPUT  */
	, AdminMsgLen /* I  : User message length      */
	, (MQBYTE *) pAdminMsg /* I  : User message             */
	, &CompCode /*  O : MQCC_OK/Warning/Error         */
	, &Reason /*  O : Reason for above         */
	);
	if (CompCode != MQCC_OK) {
		sprintf(printstr, "MQPUT failed for %s", thdCntl->CmdSvrQ);
		substr[0] = printstr;
		sprintf(printstr, "CC=%4d RC=%4d", CompCode, Reason);
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR, NULL, 2, 0,
				substr, NULL);
		return Reason;
	} /* endif */
	if (valueEventLevel > 10) {
		sprintf(printstr, "sent INQUIRE SERVICE message.");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}

	/* --------------------------------------------------------------- */
	/* Request sent, let's process replies and dump to screen.         */
	/* --------------------------------------------------------------- */

	free(pAdminMsg);
	AdminMsgLen = 4096;
	//	pAdminMsg = pAdminReply;
	pAdminMsg = (MQBYTE *) malloc(AdminMsgLen);
	if (valueEventLevel > 10) {
		sprintf(printstr, "allocated memory for input buffer.");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}
	if (!(pAdminMsg)) {
		sprintf(printstr, "Failure to allocate %ld bytes for input buffer!",
				AdminMsgLen);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR, NULL, 2, 0,
				substr, NULL);
		return (0);
	}
	GetMsgOpts.Options = MQGMO_WAIT + MQGMO_CONVERT + MQGMO_FAIL_IF_QUIESCING;
	GetMsgOpts.WaitInterval = 60000;
	GetMsgOpts.Options += MQGMO_NO_SYNCPOINT;
	sprintf(printstr, "Set getmsgopts.");
	substr[0] = printstr;
	ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
	DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
			&(thdCntl->ServiceName));

	memcpy(&MsgDesc, &MsgDesc_V, sizeof(MsgDesc));
	if (valueEventLevel > 10) {
		sprintf(printstr, "waiting for reply message.");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}

	tsGET(Hconn /* I  : Queue manager handle              */
	, hReplyQ /* I  : handle to QName to read from      */
	, &MsgDesc /* IO : message attributes                */
	, &GetMsgOpts /* IO : control action of MQGET           */
	, AdminMsgLen /* I  : length of supplied buffer         */
	, (MQBYTE *) pAdminMsg /*  O : User data to be got               */
	, &AdminMsgLen /*  O : Actual length of returned message */
	, &CompCode /*  O : MQCC_OK/Warning/Error                  */
	, &Reason /*  O : Reason for above                  */
	);
	if (CompCode != MQCC_OK) {
		sprintf(printstr, "MQGET failed for %s", DynamicQName);
		substr[0] = printstr;
		sprintf(printstr, "CC=%4d RC=%4d", CompCode, Reason);
		substr[1] = printstr;
		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, MQAPI_ERR, NULL, 2, 0,
				substr, NULL);
		return Reason;
	} /* endif */
	if (valueEventLevel > 10) {
		sprintf(printstr,
				"Received response message of size %d, about to parse service info",
				AdminMsgLen);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, AdminMsgLen, substr, pAdminMsg);
	}

	if (Reason == MQRC_NONE) {
		parseServiceInfo(thdCntl, pAdminMsg);
	}

	if (DynamicQName)
		free(DynamicQName);
	if (valueEventLevel > 10) {
		sprintf(printstr, "Exiting GetServiceInfo");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
				&(thdCntl->ServiceName));
	}
	return MQRC_NONE;
}

void parseServiceInfo(PTHRDCTL thdCntl, MQBYTE * pPCFMsg) {
	MQCFH *pPCFHeader; /* Ptr to PCF header structure     */
	MQCFST *pPCFString; /* Ptr to PCF string parm block    */
	MQCFSL *pPCFStringList;/* Ptr to PCF string parm block    */
	MQCFIN *pPCFInteger; /* v32 ptr to chltype param for clntconn */
	MQCFIL *pPCFIntegerList; /* v32 ptr to chltype param for clntconn */
	MQCFBS *pPCFByte; /* Ptr to PCF byte string parm block */
	MQLONG *pPCFType = NULL; /* Type field of PCF message parm  */
	short Index = 0; /* Loop counter                    */
	LPCSTR substr[4];
	char printstr[1024];

	if (valueEventLevel > 10) {
		sprintf(printstr, "Entered parse service info");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
		NULL, 1, sizeof(thdCntl->ServiceName), substr, &(thdCntl->ServiceName));
	}

	pPCFHeader = (MQCFH *) pPCFMsg;
	if (valueEventLevel > 10) {
		sprintf(printstr, "parsing servce info message with %d parameters",
				pPCFHeader->ParameterCount);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
		NULL, 1, sizeof(thdCntl->ServiceName), substr, &(thdCntl->ServiceName));
	}

	if (pPCFHeader->ParameterCount) {
		pPCFType = (MQLONG *) (pPCFMsg + MQCFH_STRUC_LENGTH);
		Index = 1;
		while (Index <= pPCFHeader->ParameterCount) {
			if (valueEventLevel > 10) {
				sprintf(printstr,
						"Parsing parameter %d of %d with PCF Type of %d", Index,
						pPCFHeader->ParameterCount, *pPCFType);
				substr[0] = printstr;
				ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
				DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
						&(thdCntl->ServiceName));
			}
			switch (*pPCFType) {
			case MQCFT_INTEGER:
				pPCFInteger = (MQCFIN *) pPCFType;
				if (valueEventLevel > 10) {
					sprintf(printstr,
							"Parsing PCF Integer parameter with value %d",
							pPCFInteger->Parameter);
					substr[0] = printstr;
					ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
					DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
							&(thdCntl->ServiceName));
				}
				switch (pPCFInteger->Parameter) {
				case MQIA_SERVICE_CONTROL:
					thdCntl->servInfo->Control = pPCFInteger->Value;
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->Control to %d",
								thdCntl->servInfo->Control);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}

					break;
				case MQIA_SERVICE_TYPE:
					thdCntl->servInfo->ServiceType = pPCFInteger->Value;
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->ServiceType to %d",
								thdCntl->servInfo->Control);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				} /* endswitch */

				pPCFType = (MQLONG *) ((MQBYTE *) pPCFType
						+ pPCFInteger->StrucLength);
				break;

			case MQCFT_STRING:
				pPCFString = (MQCFST *) pPCFType;
				if (valueEventLevel > 10) {
					sprintf(printstr,
							"Parsing PCF String parameter %d with value %d",
							(int) pPCFString, pPCFString->Parameter);
					substr[0] = printstr;
					ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
					DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName), substr,
							&(thdCntl->ServiceName));
				}
				switch (pPCFString->Parameter) {
				case MQCA_SERVICE_NAME:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->ServiceName ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->ServiceName, '\0',
					MQ_OBJECT_NAME_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->ServiceName,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->ServiceName to '%s'",
								thdCntl->servInfo->ServiceName);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_SERVICE_DESC:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->ServiceDesc ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->ServiceDesc, '\0',
					MQ_SERVICE_DESC_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->ServiceDesc,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->ServiceDesc to '%s'",
								thdCntl->servInfo->ServiceDesc);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_ALTERATION_DATE:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->AlterationDate ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->AlterationDate, '\0',
					MQ_CREATION_DATE_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->AlterationDate,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr,
								"set servInfo->AlterationDate to '%s'",
								thdCntl->servInfo->AlterationDate);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_ALTERATION_TIME:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->AlterationTime ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->AlterationTime, '\0',
					MQ_CREATION_TIME_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->AlterationTime,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr,
								"set servInfo->AlterationTime to '%s'",
								thdCntl->servInfo->AlterationTime);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_SERVICE_START_COMMAND:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->StartCommand");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->StartCommand, '\0',
					MQ_SERVICE_COMMAND_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->StartCommand,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->StartCommand to '%s'",
								thdCntl->servInfo->StartCommand);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_SERVICE_START_ARGS:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->StartArgs ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->StartArgs, '\0',
					MQ_SERVICE_ARGS_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->StartArgs,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->StartArgs to '%s'",
								thdCntl->servInfo->StartArgs);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_SERVICE_STOP_COMMAND:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->StopCommand ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->StopCommand, '\0',
					MQ_SERVICE_COMMAND_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->StopCommand,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->StopCommand to '%s'",
								thdCntl->servInfo->StopCommand);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_SERVICE_STOP_ARGS:
					if (valueEventLevel > 10) {
						sprintf(printstr, "setting servInfo->StopArgs ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->StopArgs, '\0',
					MQ_SERVICE_ARGS_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->StopArgs,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr, "set servInfo->StopArgs to '%s'",
								thdCntl->servInfo->StopArgs);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_STDOUT_DESTINATION:
					if (valueEventLevel > 10) {
						sprintf(printstr,
								"setting servInfo->StdoutDestination ");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->StdoutDestination, '\0',
					MQ_SERVICE_PATH_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->StdoutDestination,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr,
								"set servInfo->StdoutDestination to '%s'",
								thdCntl->servInfo->StdoutDestination);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				case MQCA_STDERR_DESTINATION:
					if (valueEventLevel > 10) {
						sprintf(printstr,
								"setting servInfo->StderrDestination");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					memset(thdCntl->servInfo->StderrDestination, '\0',
					MQ_SERVICE_PATH_LENGTH + 1);
					StripTrailingBlanks(thdCntl->servInfo->StderrDestination,
							pPCFString->String, pPCFString->StringLength);
					if (valueEventLevel > 10) {
						sprintf(printstr,
								"set servInfo->StderrDestination to '%s'",
								thdCntl->servInfo->StderrDestination);
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				default:
					if (valueEventLevel > 10) {
						sprintf(printstr, "Didn't match parameter");
						substr[0] = printstr;
						ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
						DEBUG_ERR, NULL, 1, sizeof(thdCntl->ServiceName),
								substr, &(thdCntl->ServiceName));
					}
					break;
				} /* endswitch */
				pPCFType = (MQLONG *) ((MQBYTE *) pPCFType
						+ pPCFString->StrucLength);
				break;

			default:
				break;
			} /* end of switch */
			Index++;
		}
	}
	if (valueEventLevel > 10) {
		sprintf(printstr, "Exiting parse service info");
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, DEBUG_ERR,
		NULL, 1, sizeof(thdCntl->ServiceName), substr, &(thdCntl->ServiceName));
	}

}

void StripTrailingBlanks(MQCHAR* MQParm, MQCHAR* Insert, int Length) {
	int scounter = Length - 1;
	/* Scan backwards until we find a non-blank character */
	while (scounter >= 0 && Insert[scounter] == ' ')
		scounter--;
	/* Copy the original INSERT string into MQParm buffer */
	while (scounter >= 0) {
		MQParm[scounter] = Insert[scounter];
		scounter--;
	} /* endwhile */
}
void decryptData(char* inStr, char* returnVal) {
	DATA_BLOB DataIn;
	DATA_BLOB DataOut;
	BYTE *pbDataInput;
	DWORD cbDataInput;
	char tempVal[(MQ_CSP_PASSWORD_LENGTH * 4) + 1] = "";
	LPCSTR substr[2];
	char printstr[256];
	int rc;

	pbDataInput = (BYTE*) LocalAlloc(LPTR, strlen(inStr) + 1);
	memset(pbDataInput, '\0', strlen(inStr) + 1);
//	tempVal = (char*)malloc();

	rc = myHexToData(tempVal, inStr, strlen(inStr));

	if (0 != rc)
		return;
	memcpy(pbDataInput, tempVal, strlen(inStr) / 2);

	cbDataInput = strlen(inStr) / 2;
	DataIn.pbData = pbDataInput;
	DataIn.cbData = cbDataInput;
//	DataOut.pbData=0;
//	DataOut.cbData=0;
	if (!CryptUnprotectData(&DataIn,
	NULL, // A description string.
			NULL,                               // Optional entropy
												// not used.
			NULL,                               // Reserved.
			NULL,                               // Pass a PromptStruct.
			CRYPTPROTECT_UI_FORBIDDEN, &DataOut)) {

		sprintf(printstr,
				"Decryption error, CryptUnprotectData returned %d for input %s",
				GetLastError(), pbDataInput);
		substr[0] = printstr;
		ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL,
		DEBUG_ERR, NULL, 1, NULL, substr, NULL);
		LocalFree(DataIn.pbData);
		return;
	}
	memcpy(returnVal, (char*) DataOut.pbData, DataOut.cbData);
//	LocalFree(DataOut.pbData);
	LocalFree(DataIn.pbData);
	return;
}

int myHexToData(char *data, char *hexstring, unsigned int len) {
	char printstr[256];
	LPCSTR substr[2];
	char *pos;
	char *endptr;
	size_t count = 0;

	pos = hexstring;
	if ((hexstring[0] == '\0') || (strlen(hexstring) % 2)) {
		//hexstring contains no data
		//or hexstring has an odd length
		return -1;
	}

	for (count = 0; count < len; count++) {
		char buf[3] = { pos[0], pos[1], 0 };
		data[count] = strtol(buf, &endptr, 16);
		pos += 2;		// * sizeof(char);
		if (endptr[0] != '\0') {
			//non-hexadecimal character encountered
			return -2;
		}
	}

	return 0;
}

void printMQCNO(char* output, MQCNO *connOpts) {
	char temp[255];
	MQCD * tempCD;

	tempCD = (MQCD*) connOpts->ClientConnPtr;
	sprintf(temp, "MQCNO Version = %d\n", connOpts->Version);
	strcat(output, temp);
	sprintf(temp, "MQCNO Options = %d\n", connOpts->Options);
	strcat(output, temp);
	sprintf(temp, "MQCNO ClientConnOffset = %d\n", connOpts->ClientConnOffset);
	strcat(output, temp);
	sprintf(temp, "MQCNO ClientConnPtr {\n");
	strcat(output, temp);
	sprintf(temp, "\tBatchHeartbeat =%d\n", tempCD->BatchHeartbeat);
	strcat(output, temp);
	sprintf(temp, "\tBatchDataLimit =%d\n", tempCD->BatchDataLimit);
	strcat(output, temp);
	sprintf(temp, "\tBatchInterval =%d\n", tempCD->BatchInterval);
	strcat(output, temp);
	sprintf(temp, "\tBatchSize =%d\n", tempCD->BatchSize);
	strcat(output, temp);
	sprintf(temp, "\tCLWLChannelPriority =%d\n", tempCD->CLWLChannelPriority);
	strcat(output, temp);
	sprintf(temp, "\tCLWLChannelRank =%d\n", tempCD->CLWLChannelRank);
	strcat(output, temp);
	sprintf(temp, "\tCLWLChannelWeight =%d\n", tempCD->CLWLChannelWeight);
	strcat(output, temp);
	sprintf(temp, "\tCertificateLabel =%s\n", tempCD->CertificateLabel);
	strcat(output, temp);
	sprintf(temp, "\tChannelMonitoring =%d\n", tempCD->ChannelMonitoring);
	strcat(output, temp);
	sprintf(temp, "\tChannelName =%s\n", tempCD->ChannelName);
	strcat(output, temp);
	sprintf(temp, "\tChannelStatistics =%d\n", tempCD->ChannelStatistics);
	strcat(output, temp);
	sprintf(temp, "\tChannelType =%d\n", tempCD->ChannelType);
	strcat(output, temp);
	sprintf(temp, "\tClientChannelWeight =%d\n", tempCD->ClientChannelWeight);
	strcat(output, temp);
	sprintf(temp, "\tClusterPtr =%d\n", tempCD->ClusterPtr);
	strcat(output, temp);
	sprintf(temp, "\tClustersDefined =%d\n", tempCD->ClustersDefined);
	strcat(output, temp);
	sprintf(temp, "\tConnectionAffinity =%d\n", tempCD->ConnectionAffinity);
	strcat(output, temp);
	sprintf(temp, "\tConnectionName =%s\n", tempCD->ConnectionName);
	strcat(output, temp);
	sprintf(temp, "\tDataConversion =%d\n", tempCD->DataConversion);
	strcat(output, temp);
	sprintf(temp, "\tDefReconnect =%d\n", tempCD->DefReconnect);
	strcat(output, temp);
	sprintf(temp, "\tDesc =%s\n", tempCD->Desc);
	strcat(output, temp);
	sprintf(temp, "\tDiscInterval =%d\n", tempCD->DiscInterval);
	strcat(output, temp);
	sprintf(temp, "\tExitDataLength =%d\n", tempCD->ExitDataLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tHdrCompList =%d\n", tempCD->HdrCompList);
	strcat(output, temp);
	sprintf(temp, "\tHeartbeatInterval =%d\n", tempCD->HeartbeatInterval);
	strcat(output, temp);
	sprintf(temp, "\tKeepAliveInterval =%d\n", tempCD->KeepAliveInterval);
	strcat(output, temp);
	sprintf(temp, "\tLocalAddress =%s\n", tempCD->LocalAddress);
	strcat(output, temp);
	sprintf(temp, "\tLongMCAUserIdLength =%d\n", tempCD->LongMCAUserIdLength);
	strcat(output, temp);
	sprintf(temp, "\tLongMCAUserIdPtr =%d\n", tempCD->LongMCAUserIdPtr);
	strcat(output, temp);
	sprintf(temp, "\tLongRemoteUserIdLength =%d\n",
			tempCD->LongRemoteUserIdLength);
	strcat(output, temp);
	sprintf(temp, "\tLongRemoteUserIdPtr =%d\n", tempCD->LongRemoteUserIdPtr);
	strcat(output, temp);
	sprintf(temp, "\tLongRetryCount =%d\n", tempCD->LongRetryCount);
	strcat(output, temp);
	sprintf(temp, "\tLongRetryInterval =%d\n", tempCD->LongRetryInterval);
	strcat(output, temp);
	sprintf(temp, "\tMCAName =%s\n", tempCD->MCAName);
	strcat(output, temp);
	sprintf(temp, "\tMCASecurityId =%s\n", tempCD->MCASecurityId);
	strcat(output, temp);
	sprintf(temp, "\tMCAType =%d\n", tempCD->MCAType);
	strcat(output, temp);
	sprintf(temp, "\tMCAUserIdentifier =%s\n", tempCD->MCAUserIdentifier);
	strcat(output, temp);
	sprintf(temp, "\tMaxInstances =%d\n", tempCD->MaxInstances);
	strcat(output, temp);
	sprintf(temp, "\tMaxInstancesPerClient =%d\n",
			tempCD->MaxInstancesPerClient);
	strcat(output, temp);
	sprintf(temp, "\tMaxMsgLength =%d\n", tempCD->MaxMsgLength);
	strcat(output, temp);
	sprintf(temp, "\tModeName =%s\n", tempCD->ModeName);
	strcat(output, temp);
	sprintf(temp, "\tMsgCompList =%16d\n", tempCD->MsgCompList);
	strcat(output, temp);
	sprintf(temp, "\tMsgExit =%s\n", tempCD->MsgExit);
	strcat(output, temp);
	sprintf(temp, "\tMsgExitPtr =%d\n", tempCD->MsgExitPtr);
	strcat(output, temp);
	sprintf(temp, "\tMsgExitsDefined =%d\n", tempCD->MsgExitsDefined);
	strcat(output, temp);
	sprintf(temp, "\tMsgRetryCount =%d\n", tempCD->MsgRetryCount);
	strcat(output, temp);
	sprintf(temp, "\tMsgRetryExit =%s\n", tempCD->MsgRetryExit);
	strcat(output, temp);
	sprintf(temp, "\tMsgRetryInterval =%d\n", tempCD->MsgRetryInterval);
	strcat(output, temp);
	sprintf(temp, "\tMsgRetryUserData =%s\n", tempCD->MsgRetryUserData);
	strcat(output, temp);
	sprintf(temp, "\tMsgUserData =%s\n", tempCD->MsgUserData);
	strcat(output, temp);
	sprintf(temp, "\tMsgUserDataPtr =%d\n", tempCD->MsgUserDataPtr);
	strcat(output, temp);
	sprintf(temp, "\tNetworkPriority =%d\n", tempCD->NetworkPriority);
	strcat(output, temp);
	sprintf(temp, "\tNonPersistentMsgSpeed =%d\n",
			tempCD->NonPersistentMsgSpeed);
	strcat(output, temp);
	sprintf(temp, "\tPassword =%s\n", tempCD->Password);
	strcat(output, temp);
	sprintf(temp, "\tPropertyControl =%d\n", tempCD->PropertyControl);
	strcat(output, temp);
	sprintf(temp, "\tPutAuthority =%d\n", tempCD->PutAuthority);
	strcat(output, temp);
	sprintf(temp, "\tQMgrName =%s\n", tempCD->QMgrName);
	strcat(output, temp);
	sprintf(temp, "\tReceiveExit =%s\n", tempCD->ReceiveExit);
	strcat(output, temp);
	sprintf(temp, "\tReceiveExitPtr =%d\n", tempCD->ReceiveExitPtr);
	strcat(output, temp);
	sprintf(temp, "\tReceiveExitsDefined =%d\n", tempCD->ReceiveExitsDefined);
	strcat(output, temp);
	sprintf(temp, "\tReceiveUserData =%s\n", tempCD->ReceiveUserData);
	strcat(output, temp);
	sprintf(temp, "\tReceiveUserDataPtr =%d\n", tempCD->ReceiveUserDataPtr);
	strcat(output, temp);
	sprintf(temp, "\tRemotePassword =%s\n", tempCD->RemotePassword);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%s\n", tempCD->RemoteSecurityId);
	strcat(output, temp);
	sprintf(temp, "\tRemoteUserIdentifier =%s\n", tempCD->RemoteUserIdentifier);
	strcat(output, temp);
	sprintf(temp, "\tSSLCipherSpec =%s\n", tempCD->SSLCipherSpec);
	strcat(output, temp);
	sprintf(temp, "\tSSLClientAuth =%d\n", tempCD->SSLClientAuth);
	strcat(output, temp);
	sprintf(temp, "\tSSLPeerNameLength =%d\n", tempCD->SSLPeerNameLength);
	strcat(output, temp);
	sprintf(temp, "\tSSLPeerNamePtr =%d\n", tempCD->SSLPeerNamePtr);
	strcat(output, temp);
	sprintf(temp, "\tSecurityExit =%s\n", tempCD->SecurityExit);
	strcat(output, temp);
	sprintf(temp, "\tSecurityUserData =%s\n", tempCD->SecurityUserData);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%s\n", tempCD->SendExit);
	strcat(output, temp);
	sprintf(temp, "\tSendExitPtr =%d\n", tempCD->SendExitPtr);
	strcat(output, temp);
	sprintf(temp, "\tSendExitsDefined =%d\n", tempCD->SendExitsDefined);
	strcat(output, temp);
	sprintf(temp, "\tSendUserData =%s\n", tempCD->SendUserData);
	strcat(output, temp);
	sprintf(temp, "\tSendUserDataPtr =%d\n", tempCD->SendUserDataPtr);
	strcat(output, temp);
	sprintf(temp, "\tSeqNumberWrap =%d\n", tempCD->SeqNumberWrap);
	strcat(output, temp);
	sprintf(temp, "\tSharingConversations =%d\n", tempCD->SharingConversations);
	strcat(output, temp);
	sprintf(temp, "\tShortConnectionName =%s\n", tempCD->ShortConnectionName);
	strcat(output, temp);
	sprintf(temp, "\tShortRetryCount =%d\n", tempCD->ShortRetryCount);
	strcat(output, temp);
	sprintf(temp, "\tShortRetryInterval =%d\n", tempCD->ShortRetryInterval);
	strcat(output, temp);
	sprintf(temp, "\tStrucLength =%d\n", tempCD->StrucLength);
	strcat(output, temp);
	sprintf(temp, "\tTpName =%s\n", tempCD->TpName);
	strcat(output, temp);
	sprintf(temp, "\tTransportType =%d\n", tempCD->TransportType);
	strcat(output, temp);
	sprintf(temp, "\tUseDLQ =%d\n", tempCD->UseDLQ);
	strcat(output, temp);
	sprintf(temp, "\tUserIdentifier =%S\n", tempCD->UserIdentifier);
	strcat(output, temp);
	sprintf(temp, "\tVersion =%d\n", tempCD->Version);
	strcat(output, temp);
	sprintf(temp, "\tXmitQName =%s\n", tempCD->XmitQName);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "\tExitNameLength =%d\n", tempCD->ExitNameLength);
	strcat(output, temp);
	sprintf(temp, "}\n");
	strcat(output, temp);
	sprintf(temp, "MQCNO ConnTag = %128d\n", connOpts->ConnTag);
	strcat(output, temp);
	sprintf(temp, "MQCNO ConnectionId = %d\n", connOpts->ConnectionId);
	strcat(output, temp);
	sprintf(temp, "MQCNO SSLConfigOffset = %d\n", connOpts->SSLConfigOffset);
	strcat(output, temp);
	sprintf(temp, "MQCNO SSLConfigPtr = %d\n", connOpts->SSLConfigPtr);
	strcat(output, temp);
	sprintf(temp, "MQCNO SecurityParmsOffset = %d\n",
			connOpts->SecurityParmsOffset);
	strcat(output, temp);
	sprintf(temp, "MQCNO SecurityParmsPtr = %d\n", connOpts->SecurityParmsPtr);
	strcat(output, temp);
	sprintf(temp, "MQCNO StrucId = %d\n", connOpts->StrucId);
	strcat(output, temp);
}
