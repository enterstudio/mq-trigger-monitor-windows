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
 
/* ******************************************************************/
/* Update.cpp                                                      */
/* (C) Copyright IBM Corp.1997, 2006                                */
/********************************************************************/
//
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
#include <iostream>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <cmqc.h>
#include <cmqcfc.h>
#include <cmqxc.h>                 /* For MQCD definition           */

#include "trigsvc.h"
#include "msgs.h"

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
} THRDCTL, *PTHRDCTL;

char tempPw[MQ_CSP_PASSWORD_LENGTH + 1];

// Vales for CurrentState
#define THREAD_STARTING 0
#define THREAD_RUNNING  1
#define THREAD_PAUSED   2
#define THREAD_EXITING  3
#define THREAD_STOPPED  4

// Global variables

// Thread control pointers
PTHRDCTL pThrdCtl[MAXTHREADS];

// Internal function declarations
int main(int, char *[]);
void StripTrailingBlanks(MQCHAR*, MQCHAR *, int);
char* encryptData(char*);
void hexArrayToStr(unsigned char*, unsigned int, char **);
void getServiceName(char *);
void ErrorHandler(char*, DWORD);
int updateThread(PTHRDCTL);
int writeRegistry(PTHRDCTL*);
void getServiceName(char *);
void makeGoodForRegistry(char*, char*, int, int*);
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
//		substr[0] = buffer;
//		sprintf(buffer, "RegQueryValueEx: key(%s), buflen=%ld, buffer_addr=%ld",
//				keyName, *pBufsz, keyValue);
//		sprintf(printstr, "%4d", GetLastError());
//		substr[1] = printstr;
//		ReportEvent(logHandle, EVENTLOG_ERROR_TYPE, NULL, FUNCTION_ERR, NULL, 2,
//				0, substr, NULL);
		success = FALSE; // indicate time to end
	} // end if ret != error_success
	return success;

} // end of getKeyValue function

int main(int argc, char *argv[]) {

	BOOL success;

	char printstr[1024];
	char lastReadKey[254];
	DWORD id;
	HKEY keyhandle;
	char defaultQMgrName[MQ_Q_MGR_NAME_LENGTH + 1];
	char defaultQueueName[MQ_Q_NAME_LENGTH + 1];

	DWORD bufsz;
	CHAR strBuf[80];
	DWORD ret;
	CHAR valueMQdll[MQ_CONN_NAME_LENGTH + 1];

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

	char serviceName[128];
	success = TRUE;
	int i = 0;
	getServiceName(serviceName);

	// build the path for the key
	strcpy(strBuf, KEYPREFIX);
	strcat(strBuf, serviceName);

	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, strBuf, 0, KEY_QUERY_VALUE,
			&keyhandle);

	if (ret != ERROR_SUCCESS) {
		sprintf(printstr, "Could not open Registry for Key %s", strBuf);
		ErrorHandler(printstr, ret);
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
		sprintf(printstr,
				"Could not malloc enough space to hold the data from the registry");
		ErrorHandler(printstr, -1);
		success = FALSE; // indicate time to end
	}/* End if malloc failed */

	if (success) {
		success = getKeyValue(keyhandle, TRIGGERQUEUEMGRNAME, pQMgrNames,
				&lQMgrNames);
		strcpy(lastReadKey, TRIGGERQUEUEMGRNAME);
	}
	if (success) {
		success = getKeyValue(keyhandle, TRIGGERQUEUENAME, pQNames, &lQNames);
		strcpy(lastReadKey, TRIGGERQUEUENAME);
	}
	if (success) {
		success = getKeyValue(keyhandle, SERVICENAME, pSNames, &lSNames);
		strcpy(lastReadKey, SERVICENAME);
	}
	if (success) {
		success = getKeyValue(keyhandle, NOTESINI, pNotesIni, &lNotesIni);
		strcpy(lastReadKey, NOTESINI);
	}
	if (success) {
		success = getKeyValue(keyhandle, CONNAME, pconName, &l_conn);
		strcpy(lastReadKey, CONNAME);
	}
	if (success) {
		success = getKeyValue(keyhandle, CHANNEL, pchannel, &l_chan);
		strcpy(lastReadKey, CHANNEL);
	}
	if (success) {
		success = getKeyValue(keyhandle, LOCLADDR, plocladdr, &l_locl);
		strcpy(lastReadKey, LOCLADDR);
	}
	if (success) {
		success = getKeyValue(keyhandle, RCVDATA, prcvData, &l_rcvd);
		strcpy(lastReadKey, RCVDATA);
	}
	if (success) {
		success = getKeyValue(keyhandle, SCYDATA, pscyData, &l_scyd);
		strcpy(lastReadKey, SCYDATA);
	}
	if (success) {
		success = getKeyValue(keyhandle, SENDDATA, psendData, &l_sndd);
		strcpy(lastReadKey, SENDDATA);
	}
	if (success) {
		success = getKeyValue(keyhandle, SENDEXIT, psendExit, &l_snde);
		strcpy(lastReadKey, SENDEXIT);
	}
	if (success) {
		success = getKeyValue(keyhandle, RCVEXIT, prcvExit, &l_rcve);
		strcpy(lastReadKey, RCVEXIT);
	}
	if (success) {
		success = getKeyValue(keyhandle, SCYEXIT, pscyExit, &l_scye);
		strcpy(lastReadKey, SCYEXIT);
	}
	if (success) {
		success = getKeyValue(keyhandle, USERID, puserid, &l_user);
		strcpy(lastReadKey, USERID);
	}
	if (success) {
		success = getKeyValue(keyhandle, SSLCIPH, psslciph, &l_sslc);
		strcpy(lastReadKey, SSLCIPH);
	}
	if (success) {
		success = getKeyValue(keyhandle, SSLPEER, psslpeer, &l_sslp);
		strcpy(lastReadKey, SSLPEER);
	}
	if (success) {
		success = getKeyValue(keyhandle, HBINT, phbint, &l_hbin);
		strcpy(lastReadKey, HBINT);
	}
	if (success) {
		success = getKeyValue(keyhandle, KAINT, pkaint, &l_kain);
		strcpy(lastReadKey, KAINT);
	}
	if (success) {
		success = getKeyValue(keyhandle, CHANNELUID, pchlusername, &l_chluid);
		strcpy(lastReadKey, CHANNELUID);
	}
	if (success) {
		success = getKeyValue(keyhandle, CHANNELPW, pchannelpw, &l_chlpw);
		strcpy(lastReadKey, CHANNELPW);
	}
	if (success) {
		success = getKeyValue(keyhandle, MA7K_MQCD_VERSION, pmqcdversion,
				&l_mqcd);
		strcpy(lastReadKey,
		MA7K_MQCD_VERSION);
	}
	// end of v140 changes

	// v141 get key repository

	if (success) {
		bufsz = sizeof(keyRepos);
		success = getKeyValue(keyhandle, KEYREPOS, (char *) &keyRepos, &bufsz);
		strcpy(lastReadKey, KEYREPOS);
	}

	// end v141

	if (success) {
		bufsz = sizeof(valueWaitInterval);
		success = getKeyValue(keyhandle, WAITINTERVAL,
				(char*) &valueWaitInterval, &bufsz);
		strcpy(lastReadKey, WAITINTERVAL);
	}
	if (success) {
		bufsz = sizeof(valueLongTmr);
		success = getKeyValue(keyhandle, LONGTMR, (char*) &valueLongTmr,
				&bufsz);
		strcpy(lastReadKey, LONGTMR);
	}
	if (success) {
		bufsz = sizeof(valueLongRty);
		success = getKeyValue(keyhandle, LONGRTY, (char*) &valueLongRty,
				&bufsz);
		strcpy(lastReadKey, LONGRTY);
	}
	if (success) {
		bufsz = sizeof(valueShortTmr);
		success = getKeyValue(keyhandle, SHORTTMR, (char*) &valueShortTmr,
				&bufsz);
		strcpy(lastReadKey, SHORTTMR);
	}
	if (success) {
		bufsz = sizeof(valueShortRty);
		success = getKeyValue(keyhandle, SHORTRTY, (char*) &valueShortRty,
				&bufsz);
		strcpy(lastReadKey, SHORTRTY);
	}
	if (success) {
		bufsz = sizeof(valueEventLevel);
		success = getKeyValue(keyhandle, EVENTLEVEL, (char*) &valueEventLevel,
				&bufsz);
		strcpy(lastReadKey, EVENTLEVEL);
	}
	if (success) {
		bufsz = sizeof(valueMQdll);
		success = getKeyValue(keyhandle, MQDLL, (char*) &valueMQdll, &bufsz);
		strcpy(lastReadKey, MQDLL);
	}
	if (success) {
		bufsz = sizeof(MyPath);
		success = getKeyValue(keyhandle, MYEXEPATH, (char*) &MyPath, &bufsz);
		strcpy(lastReadKey, MYEXEPATH);
	}

	RegCloseKey(keyhandle); // close the key handle .. done for now ...

	if (success) {
		if (valueEventLevel > 1) {
//			sprintf(printstr,
//					"WaitInterval=%ld LongRty=%ld LongTmr=%ld ShortRty=%ld "
//							"ShortTmr=%ld EventLevel=%ld", valueWaitInterval,
//					valueLongRty, valueLongTmr, valueShortRty, valueShortTmr,
//					valueEventLevel);
//			substr[0] = printstr;
//			ReportEvent(logHandle, EVENTLOG_INFORMATION_TYPE, NULL, STARTED,
//			NULL, 1, 0, substr, NULL);
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

			strcpy(pThrdCtl[i]->ServiceName, pwSNames); // save the service name

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
			pwQNames += strlen(pwQNames) + 1;
			pwSNames += strlen(pwSNames) + 1;

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

		int j, k = 0;
		for (j = 0; j < i; j++) {
			if (j == 0) {
				printf("Choose which section you want to update :\n");
				printf("\t%d   Global\n", j);
			}
			if (pThrdCtl[j]->Queue[0] != PLACEHOLDER) {
				printf("\t%d   Queue Name = %s\n", j + 1, pThrdCtl[j]->Queue);
			}
			if (pThrdCtl[j]->ServiceName[0] != PLACEHOLDER) {
				printf("\t%d   Service Name = %s\n", j + 1,
						pThrdCtl[j]->ServiceName);
			}
		}
		scanf("%d", &k);
		while (k > j) {
			printf("Choose which section you want to update (0 to %d):\n", j);
			scanf("%d", &k);
		}
		printf("About to update section %d\n", k);
		success = updateThread(pThrdCtl[k - 1]);
		if (0==success) {
			success=writeRegistry(pThrdCtl);
		}

	} // end if success
	else {
		sprintf(printstr,
				"Could not read all necessary registry keys. Last key read was %s",
				lastReadKey);
		ErrorHandler(printstr, -1);
	}

	for (i = 0; i < MAXTHREADS; i++) {
		if (pThrdCtl[i]) {
			free(pThrdCtl[i]);
		}

	} // end for ...

	printf("Successfully updated the service.\n");

} // end ServiceMain

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

char* encryptData(char* inStr) {
	DATA_BLOB DataIn;
	DATA_BLOB DataOut;
	BYTE *pbDataInput;
	DWORD cbDataInput;
	char printstr[254];

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
		sprintf(printstr, "Encryption error!");
		ErrorHandler(printstr, GetLastError());
	}
	tempVal = (BYTE*) malloc(DataOut.cbData);
	memset(inStr, '\0', sizeof(inStr));
	memcpy(tempVal, DataOut.pbData, DataOut.cbData);
	hexArrayToStr(tempVal, DataOut.cbData, &returnVal);
	LocalFree(DataOut.pbData);
	LocalFree(DataIn.pbData);
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

void ErrorHandler(char *s, DWORD err) {
	printf("%s\nError number %d\n", s, err);
	ExitProcess(err);
}

int updateThread(PTHRDCTL curThread) {
	int i = 1;
	int j = 0;
	int rc = 0;
	char tempPw[MQ_CSP_PASSWORD_LENGTH + MQ_CSP_PASSWORD_LENGTH + 1];
	HANDLE hStdin;
	DWORD mode = 0;
	char* tempVal;

	memset(tempPw, '\0', sizeof(tempPw));
	mode = 0;
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(hStdin, &mode);

	while (0 != i) {
		j = 0;
		printf("Choose which field to update:\n");
		if (curThread->Queue[0] != PLACEHOLDER) {
			printf("\t%d   Queue Name (Current Value = '%s')\n", ++j,
					curThread->Queue);
		}
		if (curThread->ServiceName[0] != PLACEHOLDER) {
			printf("\t%d   Service Name (Current Value = '%s')\n", ++j,
					curThread->ServiceName);
		}
		printf("\t%d   Queue Manager (Current Value = '%s')\n", ++j,
				curThread->QMgr);
		printf("\t%d   Command Server Queue (Current Value = '%s')\n", ++j,
				curThread->CmdSvrQ);
		printf("\t%d   Notes INI File (Current Value = '%s')\n", ++j,
				curThread->NotesIni);
		printf("\t%d   Connection Name(Current Value = '%s')\n", ++j,
				curThread->conName);
		printf("\t%d   Channel (Current Value = '%s')\n", ++j,
				curThread->channel);
		printf("\t%d   Local Address (Current Value = '%s')\n", ++j,
				curThread->locladdr);
		printf("\t%d   Receive Data (Current Value = '%s')\n", ++j,
				curThread->rcvData);
		printf("\t%d   Security Data (Current Value = '%s')\n", ++j,
				curThread->scyData);
		printf("\t%d   Send Data (Current Value = '%s')\n", ++j,
				curThread->sendData);
		printf("\t%d   Receive Exit (Current Value = '%s')\n", ++j,
				curThread->rcvExit);
		printf("\t%d   Security Exit (Current Value = '%s')\n", ++j,
				curThread->scyExit);
		printf("\t%d   Send Exit (Current Value = '%s')\n", ++j,
				curThread->sendExit);
		printf("\t%d   Client Connection User ID (Current Value = '%s')\n", ++j,
				curThread->userid);
		printf("\t%d   SSL Cipher (Current Value = '%s')\n", ++j,
				curThread->sslciph);
		printf("\t%d   SSL Peer (Current Value = '%s')\n", ++j,
				curThread->sslpeer);
		printf("\t%d   Heartbeat Interval (Current Value = '%s')\n", ++j,
				curThread->hbint);
		printf("\t%d   Keep Alive Interval (Current Value = '%s')\n", ++j,
				curThread->kaint);
		printf("\t%d   MQCD Version (Current Value = '%s')\n", ++j,
				curThread->mqcdversion);
		printf("\t%d   Channel User ID (Current Value = '%s')\n", ++j,
				curThread->channelUserId);
		printf("\t%d   Channel Password\n", ++j, curThread->channelPassword);
		printf("Enter 0 to finish.\n");
		scanf("%d", &i);
		if (i == 0) {
			rc = 0;
			break;
		}
		switch (i) {
		case 1: //Queue or service
			if (curThread->Queue[0] != PLACEHOLDER) {
				printf("Enter the new Queue name: ");
				scanf("%48s", curThread->Queue);
			}
			if (curThread->ServiceName[0] != PLACEHOLDER) {
				printf("Enter the new Service name: ");
				scanf("%48s", curThread->ServiceName);
			}
			printf("\n");
			break;
		case 2: //Queue Manager
			printf("Enter the new Queue Manager name: ");
			scanf("%48s", curThread->QMgr);
			printf("\n");
			break;
		case 3: //Command Server queue
			printf("Enter the new Command Server Queue name: ");
			scanf("%49s", curThread->CmdSvrQ);
			printf("\n");
			break;
		case 4: //Notes INI
			printf("Enter the new Notes INI File name: ");
			scanf("%260s", curThread->NotesIni);
			printf("\n");
			break;
		case 5: //Connection name
			printf("Enter the new Connection Name: ");
			scanf("%265s", curThread->conName);
			printf("\n");
			break;
		case 6: //channel
			printf("Enter the new Channel name: ");
			scanf("%21s", curThread->channel);
			printf("\n");
			break;
		case 7: //Local Address
			printf("Enter the new Local Address: ");
			scanf("%49s", curThread->locladdr);
			printf("\n");
			break;
		case 8: //Receive Data
			printf("Enter the new Receive Data  alue: ");
			scanf("%33s", curThread->rcvData);
			printf("\n");
			break;
		case 9: //Security Data
			printf("Enter the new Security Data value: ");
			scanf("%33s", curThread->scyData);
			printf("\n");
			break;
		case 10: //Send Data
			printf("Enter the new Send Data value: ");
			scanf("%33s", curThread->sendData);
			printf("\n");
			break;
		case 11: //Receive Exit
			printf("Enter the new Receive Exit value: ");
			scanf("%129s", curThread->rcvExit);
			printf("\n");
			break;
		case 12: //Security Exit
			printf("Enter the new Security Exit value: ");
			scanf("%129s", curThread->scyExit);
			printf("\n");
			break;
		case 13: //Send Exit
			printf("Enter the new Send Exit Value: ");
			scanf("%129s", curThread->sendExit);
			printf("\n");
			break;
		case 14: //Client Connection User ID
			printf("Enter the Client Connection User ID: ");
			scanf("%13s", curThread->userid);
			printf("\n");
			break;
		case 15: //SSL Cipher
			printf("Enter the new SSL Ciperspec value: ");
			scanf("%33s", curThread->sslciph);
			printf("\n");
			break;
		case 16: //SSL Peer
			printf("Enter the SSL Peer name: ");
			scanf("%1025s", curThread->sslpeer);
			printf("\n");
			break;
		case 17: //Heartbeat
			printf("Enter the new Heartbeat Interval: ");
			scanf("%11s", curThread->hbint);
			printf("\n");
			break;
		case 18: //Keep Alive
			printf("Enter the new Keep Alive Interval: ");
			scanf("%11s", curThread->kaint);
			printf("\n");
			break;
		case 19: //MQCD Version
			printf("Enter the new MQCD Version: ");
			scanf("%4s", curThread->mqcdversion);
			printf("\n");
			break;
		case 20: //Channel User ID
			printf(
					"You are changing the channel user id, you will be prompted to change the password as well.\n");
			printf("Enter the new Channel User ID: ");
			scanf("%1024s", curThread->channelUserId);
			printf("\n");
			memset(tempPw, '\0', sizeof(tempPw));
			printf("Enter the password for Channel User ID %s:\n",
					curThread->channelUserId);
			SetConsoleMode(hStdin,
					mode & (~ENABLE_ECHO_INPUT) & ( ENABLE_LINE_INPUT));
			scanf("%s", tempPw);
			SetConsoleMode(hStdin,
					mode & (ENABLE_ECHO_INPUT) & ( ENABLE_LINE_INPUT));
			printf("encrypting password.\n");
			tempVal = encryptData(tempPw);
			strcpy(curThread->channelPassword, tempVal);
			memset(tempPw, '\0', sizeof(tempPw));
			printf("Done encrypting password\n");
			break;
		case 21: //Channel Password
			memset(tempPw, '\0', sizeof(tempPw));
			printf("Enter the password for Channel User ID %s:\n",
					curThread->channelUserId);
			SetConsoleMode(hStdin,
					mode & (~ENABLE_ECHO_INPUT) & ( ENABLE_LINE_INPUT));
			scanf("%s", tempPw);
			SetConsoleMode(hStdin,
					mode & (ENABLE_ECHO_INPUT) & ( ENABLE_LINE_INPUT));
			printf("encrypting password.\n");
			tempVal = encryptData(tempPw);
			strcpy(curThread->channelPassword, tempVal);
			memset(tempPw, '\0', sizeof(tempPw));
			printf("Done encrypting password\n");
			break;
		}
	}
	return rc;
}
int writeRegistry(PTHRDCTL* allThreads) {
	LONG ret;
	HKEY keyHandle;
	DWORD eventTypes;
	CHAR strBuf[80];
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
	int i, j, k;
	// big arrays to hold the registry keys

	CHAR QueueName[MAXTHREADS*(MQ_Q_NAME_LENGTH+1)+1];
	char ServiceName[MAXTHREADS*(MQ_SERVICE_NAME_LENGTH+1)+1];
	CHAR QueueMgrName[MAXTHREADS*(MQ_Q_MGR_NAME_LENGTH+1)+1];
	CHAR NotesIni[MAXTHREADS*(MAX_PATH+1)+1];
	CHAR Conname[MAXTHREADS*(MQ_CONN_NAME_LENGTH+1)+1];
	CHAR Channel[MAXTHREADS*(MQ_CHANNEL_NAME_LENGTH+1)+1];
	CHAR Locladdr[MAXTHREADS*(MQ_LOCAL_ADDRESS_LENGTH+1)+1];
	CHAR RcvData[MAXTHREADS*(MQ_EXIT_DATA_LENGTH+1)+1];
	CHAR ScyData[MAXTHREADS*(MQ_EXIT_DATA_LENGTH+1)+1];
	CHAR SendData[MAXTHREADS*(MQ_EXIT_DATA_LENGTH+1)+1];
	CHAR RcvExit[MAXTHREADS*(MQ_EXIT_NAME_LENGTH+1)+1];
	CHAR ScyExit[MAXTHREADS*(MQ_EXIT_NAME_LENGTH+1)+1];
	CHAR SendExit[MAXTHREADS*(MQ_EXIT_NAME_LENGTH+1)+1];
	CHAR Userid[MAXTHREADS*(MQ_USER_ID_LENGTH+1)+1];
	CHAR Sslciph[MAXTHREADS*(MQ_SSL_CIPHER_SPEC_LENGTH+1)+1];
	CHAR Sslpeer[MAXTHREADS*(MQ_SSL_PEER_NAME_LENGTH+1)+1];
	// CHAR  Trptype[MAXTHREADS*(10)+1];
	CHAR Hbint[MAXTHREADS*(10)+1];
	CHAR Kaint[MAXTHREADS*(10)+1];
	CHAR mqcdVersion[MAXTHREADS*(3)+1];
	CHAR ChannelUserId[MAXTHREADS*(1024)+1];
	char ChannelPassword[MAXTHREADS*(MQ_CSP_PASSWORD_LENGTH*4)+1];
	//I am guessing right now that 4x length is sufficient to hold an encrypted password of normal MQ_CSP_PASSWORD_LENGTH

	DWORD WaitIntervalDefault = 60000; /* default wait interval in milliseconds */
	DWORD LongRtyDefault = 999999999;
	DWORD LongTmrDefault = 1200; //
	DWORD ShortRtyDefault = 10;
	DWORD ShortTmrDefault = 60;
	DWORD EventLevelDefault = 2; // 1 = normal msgs, 2 = detailed (process), 3 = debug
	CHAR AgentRedirDefault[MAX_PATH] = "Yes"; // whether or not to redirect Notes agent output
	CHAR MQdllDefault[MAX_PATH]; // default .dll to use for mqseries
	int qmgrindx = 0; // length of qmgr namelist
	int qindx = 0; // length of queue namelist
	int sindx = 0; // length of service namelist
	int niindx = 0; // length of notesIni namelist
	int con_indx = 0, chan_indx =0, locl_indx=0, rcvd_indx=0, scyd_indx=0,
			sndd_indx=0, rcve_indx=0;
	int scye_indx=0, snde_indx=0, user_indx=0, sslc_indx=0, sslp_indx=0, trpt_indx=
			0, hbin_indx=0;
	int kain_indx=0, chlui_indx=0, chlpw_indx=0, mqcd_indx=0;

	CHAR keyRepos[MAX_PATH]; // location of ssl key repository

	CHAR serviceUserid[128]; // the userid the service should run as
	CHAR servicePassword[128]; // the password the service should run as

	char serviceName[128];

	getServiceName(serviceName);

	printf("Entered writeRegistry\n");

	memset(MQdllDefault, '\0', sizeof(MQdllDefault));
	strcpy(MQdllDefault, "MQIC.DLL");

	// initialize all array members

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

	// Get the path we are running under and append program name.
	len = GetCurrentDirectory(MAX_PATH, Path);
	if (!len)
		ErrorHandler("Unable to get current directory", GetLastError());

	// check that we have the executable in the current directory.

	strcpy(PathProgram, Path);
	strcat(PathProgram, "\\");
	strcat(PathProgram, SERVICEPROGRAM);

	fileHandle = CreateFile(PathProgram, 0, FILE_SHARE_WRITE, 0, OPEN_EXISTING,
			0, 0);
	if (fileHandle == INVALID_HANDLE_VALUE) {
		strcpy(strBuf, "Cound not find service executable \"");
		strcat(strBuf, SERVICEPROGRAM);
		strcat(strBuf, "\" in the current directory");
		ErrorHandler(strBuf, GetLastError());
	} else {
		CloseHandle(fileHandle);
	}
	printf("setup arrays and etc. \n");

	//
	//Here we build the arrays from the modified threads...
	//
	i = 0;
	while (allThreads[i] != NULL) {
		makeGoodForRegistry(QueueMgrName, allThreads[i]->QMgr,
				sizeof(allThreads[0]->QMgr), &qmgrindx);
		makeGoodForRegistry(QueueName, allThreads[i]->Queue,
				sizeof(allThreads[0]->Queue), &qindx);
		makeGoodForRegistry(ServiceName, allThreads[i]->ServiceName,
				sizeof(allThreads[0]->ServiceName), &sindx);
		makeGoodForRegistry(NotesIni, allThreads[i]->NotesIni,
				sizeof(allThreads[0]->NotesIni), &niindx);
		makeGoodForRegistry(Conname, allThreads[i]->conName,
				sizeof(allThreads[0]->conName), &con_indx);
		makeGoodForRegistry(Channel, allThreads[i]->channel,
				sizeof(allThreads[0]->channel), &chan_indx);
		makeGoodForRegistry(Locladdr, allThreads[i]->locladdr,
				sizeof(allThreads[0]->locladdr), &locl_indx);
		makeGoodForRegistry(RcvData, allThreads[i]->rcvData,
				sizeof(allThreads[0]->rcvData), &rcvd_indx);
		makeGoodForRegistry(ScyData, allThreads[i]->scyData,
				sizeof(allThreads[0]->scyData), &scyd_indx);
		makeGoodForRegistry(SendData, allThreads[i]->sendData,
				sizeof(allThreads[0]->sendData), &sndd_indx);
		makeGoodForRegistry(RcvExit, allThreads[i]->rcvExit,
				sizeof(allThreads[0]->rcvExit), &rcve_indx);
		makeGoodForRegistry(ScyExit, allThreads[i]->scyExit,
				sizeof(allThreads[0]->scyExit), &scye_indx);
		makeGoodForRegistry(SendExit, allThreads[i]->sendExit,
				sizeof(allThreads[0]->sendExit), &snde_indx);
		makeGoodForRegistry(Userid, allThreads[i]->userid,
				sizeof(allThreads[0]->userid), &user_indx);
		makeGoodForRegistry(Sslciph, allThreads[i]->sslciph,
				sizeof(allThreads[0]->sslciph), &sslc_indx);
		makeGoodForRegistry(Sslpeer, allThreads[i]->sslpeer,
				sizeof(allThreads[0]->sslpeer), &sslp_indx);
		makeGoodForRegistry(Hbint, allThreads[i]->hbint,
				sizeof(allThreads[0]->hbint), &hbin_indx);
		makeGoodForRegistry(Kaint, allThreads[i]->kaint,
				sizeof(allThreads[0]->kaint), &kain_indx);
		makeGoodForRegistry(ChannelUserId, allThreads[i]->channelUserId,
				sizeof(allThreads[0]->channelUserId), &chlui_indx);
		makeGoodForRegistry(ChannelPassword, allThreads[i]->channelPassword,
				sizeof(allThreads[0]->channelPassword), &chlpw_indx);
		makeGoodForRegistry(mqcdVersion, allThreads[i]->mqcdversion,
				sizeof(allThreads[0]->mqcdversion), &mqcd_indx);
		i++;
	}
	printf("completed making good for registry. \n");

	//
	// Lastly, we create / update keys for the queue and qmgr names
	//

	// build the path for the new key
	strcpy(strBuf, KEYPREFIX);
	strcat(strBuf, serviceName);

	// cout << "Service key " << strBuf << endl;

	// add a key for the new source
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
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	ret = RegSetValueEx(keyHandle, SERVICENAME, 0, REG_MULTI_SZ,
			(LPBYTE) &ServiceName, sindx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the TriggerQueueQmrName value to key
	ret = RegSetValueEx(keyHandle, TRIGGERQUEUEMGRNAME, 0, REG_MULTI_SZ,
			(LPBYTE) &QueueMgrName, qmgrindx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the NotesIni value to key
	ret = RegSetValueEx(keyHandle, NOTESINI, 0, REG_MULTI_SZ,
			(LPBYTE) &NotesIni, niindx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the conName value to key
	ret = RegSetValueEx(keyHandle, CONNAME, 0, REG_MULTI_SZ, (LPBYTE) &Conname,
			con_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the Channel value to key
	ret = RegSetValueEx(keyHandle, CHANNEL, 0, REG_MULTI_SZ, (LPBYTE) &Channel,
			chan_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the LoclAddr value to key
	ret = RegSetValueEx(keyHandle, LOCLADDR, 0, REG_MULTI_SZ,
			(LPBYTE) &Locladdr, locl_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the RcvData value to key
	ret = RegSetValueEx(keyHandle, RCVDATA, 0, REG_MULTI_SZ, (LPBYTE) &RcvData,
			rcvd_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the ScyData value to key
	ret = RegSetValueEx(keyHandle, SCYDATA, 0, REG_MULTI_SZ, (LPBYTE) &ScyData,
			scyd_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the SendData value to key
	ret = RegSetValueEx(keyHandle, SENDDATA, 0, REG_MULTI_SZ,
			(LPBYTE) &SendData, sndd_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the RcvExit value to key
	ret = RegSetValueEx(keyHandle, RCVEXIT, 0, REG_MULTI_SZ, (LPBYTE) &RcvExit,
			rcve_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the ScyExit value to key
	ret = RegSetValueEx(keyHandle, SCYEXIT, 0, REG_MULTI_SZ, (LPBYTE) &ScyExit,
			scye_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the SendExit value to key
	ret = RegSetValueEx(keyHandle, SENDEXIT, 0, REG_MULTI_SZ,
			(LPBYTE) &SendExit, snde_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the Userid value to key
	ret = RegSetValueEx(keyHandle, USERID, 0, REG_MULTI_SZ, (LPBYTE) &Userid,
			user_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the Sslpeer value to key
	ret = RegSetValueEx(keyHandle, SSLPEER, 0, REG_MULTI_SZ, (LPBYTE) &Sslpeer,
			sslp_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the Sslciph value to key
	ret = RegSetValueEx(keyHandle, SSLCIPH, 0, REG_MULTI_SZ, (LPBYTE) &Sslciph,
			sslc_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the Trptype value to key
	// ret=RegSetValueEx(keyHandle, TRPTYPE, 0, REG_MULTI_SZ, (LPBYTE) &Trptype, trpt_indx);
	//  if (ret != ERROR_SUCCESS) ErrorHandler("Unable to add value to key", GetLastError());

	// add the Hbint value to key
	ret = RegSetValueEx(keyHandle, HBINT, 0, REG_MULTI_SZ, (LPBYTE) &Hbint,
			hbin_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the Kaint value to key
	ret = RegSetValueEx(keyHandle, KAINT, 0, REG_MULTI_SZ, (LPBYTE) &Kaint,
			kain_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());
	// add the Channel Username value to key
	ret = RegSetValueEx(keyHandle, CHANNELUID, 0, REG_MULTI_SZ,
			(LPBYTE) &ChannelUserId, chlui_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	ret = RegSetValueEx(keyHandle, CHANNELPW, 0, REG_MULTI_SZ,
			(LPBYTE) &ChannelPassword, chlpw_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the mqcdVersion value to key
	ret = RegSetValueEx(keyHandle, MA7K_MQCD_VERSION, 0, REG_MULTI_SZ,
			(LPBYTE) &mqcdVersion, mqcd_indx);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the WaitInterval value to key
	ret = RegSetValueEx(keyHandle, WAITINTERVAL, 0, REG_DWORD,
			(LPBYTE) &WaitIntervalDefault, sizeof(WaitIntervalDefault));
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the LongRty value to key
	ret = RegSetValueEx(keyHandle, LONGRTY, 0, REG_DWORD,
			(LPBYTE) &LongRtyDefault, sizeof(LongRtyDefault));
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the LongTmr value to key
	ret = RegSetValueEx(keyHandle, LONGTMR, 0, REG_DWORD,
			(LPBYTE) &LongTmrDefault, sizeof(LongTmrDefault));
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the ShortRty value to key
	ret = RegSetValueEx(keyHandle, SHORTRTY, 0, REG_DWORD,
			(LPBYTE) &ShortRtyDefault, sizeof(ShortRtyDefault));
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the ShortTmr value to key
	ret = RegSetValueEx(keyHandle, SHORTTMR, 0, REG_DWORD,
			(LPBYTE) &ShortTmrDefault, sizeof(ShortTmrDefault));
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the EventLevel value to key
	ret = RegSetValueEx(keyHandle, EVENTLEVEL, 0, REG_DWORD,
			(LPBYTE) &EventLevelDefault, sizeof(EventLevelDefault));
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the mqseries dll name to key
	ret = RegSetValueEx(keyHandle, MQDLL, 0, REG_SZ, (LPBYTE) &MQdllDefault,
			strlen(MQdllDefault) + 1);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the Notes agent redir output key
	ret = RegSetValueEx(keyHandle, AGENTREDIR, 0, REG_SZ,
			(LPBYTE) &AgentRedirDefault, strlen(AgentRedirDefault) + 1);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add the KeyRepository output key
	ret = RegSetValueEx(keyHandle, KEYREPOS, 0, REG_SZ, (LPBYTE) &keyRepos,
			strlen(keyRepos) + 1);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// add our path key
	ret = RegSetValueEx(keyHandle, MYEXEPATH, 0, REG_SZ, (LPBYTE) &Path,
			strlen(Path) + 1);
	if (ret != ERROR_SUCCESS)
		ErrorHandler("Unable to add value to key", GetLastError());

	// close the key
	RegCloseKey(keyHandle);

	printf("Finished writeRegistry. \n");
	return 0;
}

void makeGoodForRegistry(char * pTarget, char * pSource, int lSource,
		int* piTarget) {

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
}
