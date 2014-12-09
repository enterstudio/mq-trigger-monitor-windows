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
 
//
//  Values are 32 bit values laid out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//


//
// Define the severity codes
//


//
// MessageId: STARTING
//
// MessageText:
//
// WebSphere MQ Client Trigger Service V1.5 (1 Feb 2011) Starting.
//
#define STARTING                         ((DWORD)0x40000100L)

//
// MessageId: STARTED
//
// MessageText:
//
// WebSphere MQ Client Trigger Service Started using parms: %1.
//
#define STARTED                          ((DWORD)0x40000101L)

//
// MessageId: STOPPED
//
// MessageText:
//
// WebSphere MQ Client Trigger Service Stopped.
// (See previous event message for stop reason)
//
#define STOPPED                          ((DWORD)0x40000102L)

//
// MessageId: CMD_MSG
//
// MessageText:
//
// Starting: %1.
//
#define CMD_MSG                          ((DWORD)0x40000103L)

//
// MessageId: PROC_WARN
//
// MessageText:
//
// Error %1 trying to start process (GetLastError= %2.)
// (See "Data:" for process string)
//
#define PROC_WARN                        ((DWORD)0x80000104L)

//
// MessageId: PROC_NOEXEC
//
// MessageText:
//
// Could not find executable.
// (See Data: for process string)
//
#define PROC_NOEXEC                      ((DWORD)0x80000105L)

//
// MessageId: MQAPI_ERR
//
// MessageText:
//
// "%1" returned %2 for queue "%3" and qmgr "%4".
//
#define MQAPI_ERR                        ((DWORD)0xC0000106L)

//
// MessageId: MQAPI_ERR_RETRY
//
// MessageText:
//
// "%1" returned %2 for queue "%3" and qmgr "%4", retrying ...
//
#define MQAPI_ERR_RETRY                  ((DWORD)0xC0000107L)

//
// MessageId: BAD_TRIG_LEN
//
// MessageText:
//
// Invalid trigger message received from queue "%1" with context "%2".
// (See Data: for message received)
//
#define BAD_TRIG_LEN                     ((DWORD)0x80000108L)

//
// MessageId: BAD_APPL_TYPE
//
// MessageText:
//
// Invalid application type received: type=%1.
// (See Data: for message received)
//
#define BAD_APPL_TYPE                    ((DWORD)0x80000109L)

//
// MessageId: FUNCTION_ERR
//
// MessageText:
//
// %1 call failed with return code %2.
//
#define FUNCTION_ERR                     ((DWORD)0xC000010AL)

//
// MessageId: LOAD_DLL_ERR
//
// MessageText:
//
// Failed to load dll "%1", return code %2.
//
#define LOAD_DLL_ERR                     ((DWORD)0xC000010BL)

//
// MessageId: STOP_REQ
//
// MessageText:
//
// User Requested Service Stop.
//
#define STOP_REQ                         ((DWORD)0xC000010CL)

//
// MessageId: TOO_MANY_RETRIES
//
// MessageText:
//
// Stopping, too many retry conditions.
//
#define TOO_MANY_RETRIES                 ((DWORD)0x4000010DL)

//
// MessageId: DEBUG_ERR
//
// MessageText:
//
// %1
//
#define DEBUG_ERR                        ((DWORD)0x4000010EL)

//
// MessageId: NOTESAPI_ERR
//
// MessageText:
//
// Lotus Notes call "%1" returned "%5" (rc=%2) (Insert2= "%3" Insert3 = "%4").
//
#define NOTESAPI_ERR                     ((DWORD)0xC000010FL)

//
// MessageId: NOTES_RUN
//
// MessageText:
//
// Starting Lotus Notes Agent "%3" in database "%1" with trigger parameter "%4".
//
#define NOTES_RUN                        ((DWORD)0xC0000110L)

//
// MessageId: AGENT_OUT
//
// MessageText:
//
// Lotus Notes Agent output: "%1".
//
#define AGENT_OUT                        ((DWORD)0xC0000111L)

//
// MessageId: STARTED_THREAD
//
// MessageText:
//
// Thread started to watch: %1.
//
#define STARTED_THREAD                   ((DWORD)0x40000112L)

//
// MessageId: EXITING_THREAD
//
// MessageText:
//
// Thread (Th# %2) for %1 exiting.
//
#define EXITING_THREAD                   ((DWORD)0x40000113L)

//
// MessageId: INI_FILE_NOT_FOUND
//
// MessageText:
//
// Notes ini file "%1" not found, source was "%3".
//
#define INI_FILE_NOT_FOUND               ((DWORD)0x40000114L)

//
// MessageId: DECRYPTED
//
// MessageText:
//
// Password for user "%1" was decrypted, value is "%2" .
//
#define DECRYPTED                        ((DWORD)0x40000115L)

