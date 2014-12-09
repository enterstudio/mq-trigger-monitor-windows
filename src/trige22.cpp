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
 
 /********************************************************************/
 /*                                                                  */
 /* Module Name: trige22.c                                           */
 /*                                                                  */
 /* Description: Trigger Monitor - Run Notes Agent functions         */
 /*                                                                  */
 /*                                                                  */
 /********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <global.h>
#include <nif.h>
#include <nsfdb.h>
#include <nsfnote.h>
#include <nsfdata.h>
#include <agents.h>
#include <osmisc.h>
#include <names.h>
#include <miscerr.h>
#include <osenv.h>
#include <osmem.h>
#include <globerr.h>
#include <oserr.h>
#include <miscerr.h>

#include <windows.h>
#include <winbase.h>
/*#include <iostream.h>*/
#include <stdio.h>
#include <cmqc.h>


#include <cmqc.h>

#include "trigsvc.h"
#include "trigs22.h"
#include "msgs.h"

/*********************************************************************/
/*                                                                   */
/* Function prototypes                                               */
/*                                                                   */
/*********************************************************************/

void writeLog ( HANDLE logHandle,       // event log handle
                DWORD valueEventLevel,  // logging value from registry
                long   msgId,           // symbolic message id
                STATUS notesRC,         // rc from notes or 0.
                char   *string1,        // sub string 1
                char   *string2,        // sub string 2
                char   *string3         // sub string 3
                ) ;

STATUS  SetItemText( NOTEHANDLE  hParmNote
                           , char      * pItemName
                           , char      * pItemSrc
                           , long        lItemLen
                           );

/*********************************************************************/
/*                                                                   */
/* Function: MAIN                                                    */
/*                                                                   */
/*    Runs the Notes Agent specified by the ApplId parameter         */
/*                                                                   */
/* Parameters in:                                                    */
/*                                                                   */
/*    TrigMsg     Complete Trigger Message                           */
/*    valueEventLevel  controls how many messages sent to NT log     */
/*    NotesIni file -- used to setup notes initialization            */
/*                                                                   */
/* Parameters out: none                                              */
/*                                                                   */
/* Returns:       Notes API error codes                              */
/*                                                                   */
/*********************************************************************/


void main( int argc, char **argv)
{
  char *     DBName;                       /* Agent Database name     */
  char *     AgentName;                    /* Name of Agent           */
  NOTEID     AgentNoteID;                  /* Note ID of Agent        */
  DBHANDLE   hDb             = NULLHANDLE; /* Database handle         */
  HAGENT     hAgent          = NULL;       /* Agent handle            */
  HAGENTCTX  hRunContext     = NULL;       /* Run context handle      */
  STATUS     error           = NOERROR;    /* notes error reurn code  */
  NOTEHANDLE hParmNote       = NULLHANDLE; /* note handle             */
  int        RedirStdOut     = FALSE;      /* redirect stdout flag    */
  char *     pFieldName = NULL;
  char *     pFieldValue = NULL;
  int        FieldLength = 0;
  char       printstr[257];
  int        i=0;       /* loop variable */

  HKEY       keyhandle;
  CHAR       strBuf[80];
  DWORD      valueType;
  DWORD      strBuflen = sizeof(strBuf);
  DWORD      ret = 0;
  PMQTMC2    pTrigMsg;
  char       ApplId[sizeof(pTrigMsg->ApplId)];
  int        initialized = 0;
  char       NotesIni[MAX_PATH];
  char       RealNotesIni[MAX_PATH];
  char       serviceName[128];  // name of the service

  void       getServiceName(char *);

  HANDLE logHandle;
  int valueEventLevel = 0;
  HANDLE fileHandle;

  //
  // make a local copy of the parameters
  //

  pTrigMsg = (PMQTMC2)argv[1];
  valueEventLevel = atoi(argv[2]);

  memset(NotesIni, '\0', sizeof(NotesIni));
  if (argc > 3) {
    strcpy(NotesIni, argv[3]);
  }

  getServiceName(serviceName);

  logHandle =RegisterEventSource( NULL, serviceName );

  strcpy(printstr, "Entering: ");
  strcat(printstr, argv[0]);
  strcat(printstr, " (v1.4.0--Linked with Notes API ");
  strcat(printstr, NVERSION);
  strcat(printstr, " )");

  writeLog ( logHandle,            // event log handle
           valueEventLevel,        // logging value from registry
           DEBUG_ERR,              // symbolic message id
           0,                      // rc from notes or 0.
           printstr,               // sub string 1
           "",                     // sub string 2
           ""                      // sub string 3
           ) ;


  /********************************************************************/
  /*  Get the environment variable                                    */
  /* to tell whether or not to redirect the agent's output            */
  /********************************************************************/

   strcpy(strBuf, KEYPREFIX);
   strcat(strBuf, serviceName);
   ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, strBuf,
                          0, KEY_QUERY_VALUE, &keyhandle);
   if (!ret) {
       ret = RegQueryValueEx(keyhandle,        // handle of key to query
             AGENTREDIR,                        // address of name of value to query
             0,                                 // reserved
             &valueType,                        // address of buffer for value type
             (LPBYTE) strBuf,                 // address of data buffer
             &strBuflen );               // data buffer size

            RegCloseKey(keyhandle);  // close the key handle .. done for now ...
                if (!ret) {
                    for (i=0; i<sizeof(strBuf)-1 && strBuf[i] != '\0'; i++)
                         strBuf[i] = tolower(strBuf[i]);
                 strBuf[i] = '\0';
                 if ( strcmp( strBuf, AGENT_REDIR_YES ) == 0 )
                        RedirStdOut = TRUE;
                } /* end if ! ret */

        }

  /********************************************************************/
  /*  Calling Notes initialization                                    */
  /********************************************************************/

   // The NotesIni filed can contain one of four values:
   // "EnvData" or "EnvrData" -- take the notes.ini file name from the Envdata field of the TM
   // "UserData" -- take the notes.ini file name from the UserData field of the TM
   // "TrigData" -- take the notes.ini file name from the TrigData field of the TM
   //  else ... the field IS the name of the notes.ini file

   memset(RealNotesIni, '\0', sizeof(RealNotesIni));
   // Make everything uppercase
   for (int i2 = 0; i2 < strlen(NotesIni); i2++) *(NotesIni+i2) = toupper(*(NotesIni+i2));

   if (!strcmp(NotesIni, "ENVDATA") || !strcmp(NotesIni, "ENVRDATA"))
     memcpy(RealNotesIni, pTrigMsg->EnvData, sizeof(pTrigMsg->EnvData));
   else if (!strcmp(NotesIni, "USERDATA") )
     memcpy(RealNotesIni, pTrigMsg->UserData, sizeof(pTrigMsg->UserData));
   else if (!strcmp(NotesIni, "TRIGDATA") )
     memcpy(RealNotesIni, pTrigMsg->TriggerData, sizeof(pTrigMsg->TriggerData));
   else
     memcpy(RealNotesIni, NotesIni, sizeof(NotesIni));

   for ( i = sizeof(RealNotesIni)-1;
         i>= 0 && ( RealNotesIni[i]==' ' || RealNotesIni[i]=='\0') ;
         i--)
         RealNotesIni[i] = '\0';

   if (RealNotesIni[0])
     {
      fileHandle = CreateFile(RealNotesIni, 0, FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
      if (fileHandle == INVALID_HANDLE_VALUE)
        {
          error = 2;
          writeLog ( logHandle,           // event log handle
                  valueEventLevel,        // logging value from registry
                  INI_FILE_NOT_FOUND,     // symbolic message id
                  error,                  // rc from notes or 0.
                  RealNotesIni,                // sub string 1
                  NotesIni,                     // sub string 2
                  ""                      // sub string 3
                  );
        } else {
          error = NotesInitIni(RealNotesIni);
          writeLog ( logHandle,              // event log handle
                     valueEventLevel,        // logging value from registry
                     NOTESAPI_ERR,            // symbolic message id
                     error,                  // rc from notes or 0.
                     "NotesInitIni",         // sub string 1
                     RealNotesIni,                // sub string 2
                     NotesIni                      // sub string 3
                     ) ;
        }
     } else {
      error = NotesInitExtended (argc, argv);
      writeLog ( logHandle,              // event log handle
                 valueEventLevel,        // logging value from registry
                 NOTESAPI_ERR,            // symbolic message id
                 error,                  // rc from notes or 0.
                 "NotesInitExtended",    // sub string 1
                 "",                     // sub string 2
                 ""                      // sub string 3
                 ) ;

    }

   // Did we initialize okay?
   if ( error == NOERROR ) initialized = 1;
   sprintf(printstr, "Initialized flag set to: %d",initialized);

   writeLog ( logHandle,            // event log handle
            valueEventLevel,        // logging value from registry
            DEBUG_ERR,              // symbolic message id
            0,                      // rc from notes or 0.
            printstr,               // sub string 1
            "",                     // sub string 2
            ""                      // sub string 3
            ) ;


  /********************************************************************/
  /*  Calling Notes to open the agent database.                       */
  /*                                                                  */
  /*  Get the name of the agent database and the name of the agent    */
  /*  from the Application identifier (ApplId). This will be in the   */
  /*  following format:                                               */
  /*                                                                  */
  /*  agentdb.nsf Agent Name                                          */
  /*                                                                  */
  /*  Note that the database name may not contain spaces but the      */
  /*  agent name following it can contain spaces.                     */
  /********************************************************************/
  if ( error == NOERROR )
  {
    /*******************************************************************/
    /* Make a null terminated copy of the applicid field               */
    /*******************************************************************/

    memcpy(ApplId, pTrigMsg->ApplId, sizeof(ApplId));
    for ( i=sizeof(ApplId)-1; i>= 0 && ApplId[i]==' '; i--) ApplId[i] = '\0';

    DBName = strtok (ApplId, " ");          /* get name of agent Database */
    if ( DBName == NULL )
    {
      DBName = "";
    }

    strcpy(printstr, "About to load Notes DB named \"");
    strcat(printstr, DBName);
    strcat(printstr, "\"");

    writeLog ( logHandle,            // event log handle
             valueEventLevel,        // logging value from registry
             DEBUG_ERR,              // symbolic message id
             0,                      // rc from notes or 0.
             printstr,               // sub string 1
             "",                     // sub string 2
             ""                      // sub string 3
             ) ;

    error = NSFDbOpen (DBName, &hDb);

    writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "NotesDbOpen",   // sub string 1
               DBName,                     // sub string 2
               ""                      // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to find the agent Note ID. Note that the Agent    */
  /*  name follows the database name (in ApplId) and is therefore     */
  /*  olny present if the database name has non-zero length).         */
  /*  Get the name of the agent from the Application identifier       */
  /*  (ApplId).                                                       */
  /*  This will be in the following format:                           */
  /*                                                                  */
  /*  agentdb.nsf Agent Name                                          */
  /*                                                                  */
  /*  Thus the agent name only exists if a valid database name exists */
  /*                                                                  */
  /*  Note that although the database name may not contain spaces,    */
  /*  the agent name following it can contain spaces.                 */
  /*                                                                  */
  /********************************************************************/
  if ( error == NOERROR )
  {

    if ( DBName != NULL  && strlen(DBName) > 0 )
    {
      AgentName = &ApplId[strlen(DBName) + 1];
    }
    else
    {
      AgentName = "";
    }
    strcpy(printstr, "About to find notes agent named \"");
    strcat(printstr, AgentName);
    strcat(printstr, "\"");

    writeLog ( logHandle,            // event log handle
             valueEventLevel,        // logging value from registry
             DEBUG_ERR,              // symbolic message id
             0,                      // rc from notes or 0.
             printstr,               // sub string 1
             "",                     // sub string 2
             ""                      // sub string 3
             ) ;

    error = NIFFindDesignNote (hDb, AgentName, NOTE_CLASS_FILTER, &AgentNoteID);

    if ( error == ERR_NOT_FOUND )
    {
      error = NIFFindPrivateDesignNote (hDb, AgentName, NOTE_CLASS_FILTER, &AgentNoteID);
    }
    writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "NIFFindDesignNote",    // sub string 1
               DBName,                 // sub string 2
               AgentName               // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to open the agent                                 */
  /********************************************************************/
  if (error == NOERROR )
  {
	    strcpy(printstr, "About to open notes agent named  \"");
	    strcat(printstr, AgentName);
	    strcat(printstr, "\"");

	    writeLog ( logHandle,            // event log handle
	             valueEventLevel,        // logging value from registry
	             DEBUG_ERR,              // symbolic message id
	             0,                      // rc from notes or 0.
	             printstr,               // sub string 1
	             "",                     // sub string 2
	             ""                      // sub string 3
	             ) ;

	error = AgentOpen (hDb, AgentNoteID, &hAgent);
    writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "AgentOpen",            // sub string 1
               DBName,                 // sub string 2
               AgentName               // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to create the trigger message parameter document  */
  /********************************************************************/
  if ( error == NOERROR )
  {
	    strcpy(printstr, "About to create trigger message parameter document ");

	    writeLog ( logHandle,            // event log handle
	             valueEventLevel,        // logging value from registry
	             DEBUG_ERR,              // symbolic message id
	             0,                      // rc from notes or 0.
	             printstr,               // sub string 1
	             "",                     // sub string 2
	             ""                      // sub string 3
	             ) ;
    error = NSFNoteCreate ( hDb, &hParmNote );
    writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "AgentOpen",            // sub string 1
               DBName,                 // sub string 2
               AgentName               // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to set the Notes Class of the trigger message     */
  /*  parameter document to NOTE_CLASS_DOCUMENT.                      */
  /*  This function never returns an error.                           */
  /********************************************************************/
  if ( error == NOERROR )
  {
    WORD wNoteClass;                                    /* note class */
    wNoteClass = NOTE_CLASS_DOCUMENT;

    NSFNoteSetInfo ( hParmNote, _NOTE_CLASS, &wNoteClass );
    writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "NSFNoteSetInfo",       // sub string 1
               DBName,                 // sub string 2
               AgentName               // sub string 3
               ) ;
  }
  /********************************************************************/
  /* We make the next test so that we don't incorrectly write an      */
  /* SetItemText error message at the bottom of the block             */
  /********************************************************************/


  if ( error == NOERROR ) {


  /********************************************************************/
  /*  Calling Notes to set the Form name of the MQTMC2 trigger        */
  /*  message parameter document.                                     */
  /********************************************************************/
  if ( error == NOERROR )
  {
        pFieldName = "FORM";
        pFieldValue = FORM_NAME;
        FieldLength = sizeof(FORM_NAME);
    error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
  }

  /********************************************************************/
  /*  Calling Notes to set the Structure identifier (field) item of   */
  /*  the MQTMC2 trigger message parameter document.                  */
  /********************************************************************/
  if ( error == NOERROR )
  {
        pFieldName = STRUCID;
        pFieldValue = pTrigMsg->StrucId;
        FieldLength = sizeof(pTrigMsg->StrucId);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
  }

  /********************************************************************/
  /*  Calling Notes to set the Structure version number (field) item  */
  /*  of the MQTMC2 trigger message parameter document.               */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = VERSION;
        pFieldValue = pTrigMsg->Version;
        FieldLength = sizeof(pTrigMsg->Version);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
    }

  /********************************************************************/
  /*  Calling Notes to set the Triggered queue name (field) item     */
  /*  of the MQTMC2 trigger message parameter document.               */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = QNAME;
        pFieldValue = pTrigMsg->QName;
        FieldLength = sizeof(pTrigMsg->QName);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
   }

  /********************************************************************/
  /*  Calling Notes to set the Process name (field) item              */
  /*  of the MQTMC2 trigger message parameter document.               */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = PROCESSNAME;
        pFieldValue = pTrigMsg->ProcessName;
        FieldLength = sizeof(pTrigMsg->ProcessName);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
    }

  /********************************************************************/
  /*  Calling Notes to set the Trigger Data (field) item of the       */
  /*  MQTMC2 trigger message parameter document.                      */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = TRIGGERDATA;
        pFieldValue = pTrigMsg->TriggerData;
        FieldLength = sizeof(pTrigMsg->TriggerData);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
   }

  /********************************************************************/
  /*  Calling Notes to set the Application type (field) item of the   */
  /*  MQTMC2 trigger message parameter document.                      */
  /*  Note that according to the current MQSeries documentation, the  */
  /*  Application Type field (ApplType) in the MQTMC2 structure is    */
  /*  always blank.                                                   */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = APPLTYPE;
        pFieldValue =  pTrigMsg->ApplType;
        FieldLength = sizeof( pTrigMsg->ApplType);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
}

  /********************************************************************/
  /*  Calling Notes to set the Application identifier (field) item of */
  /*  the MQTMC2 trigger message parameter document.                  */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = APPLID;
        pFieldValue = pTrigMsg->ApplId;
        FieldLength = sizeof(pTrigMsg->ApplId);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
}

  /********************************************************************/
  /*  Calling Notes to set the Environment data (field) item of      */
  /*  the MQTMC2 trigger message parameter document.                  */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = ENVDATA;
        pFieldValue = pTrigMsg->EnvData;
        FieldLength = sizeof(pTrigMsg->EnvData);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
  }

  /********************************************************************/
  /*  Calling Notes to set the User data (field) item of              */
  /*  the MQTMC2 trigger message parameter document.                  */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = USERDATA;
        pFieldValue = pTrigMsg->UserData;
        FieldLength = sizeof(pTrigMsg->UserData);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
   }

  /********************************************************************/
  /*  Calling Notes to set the Queue manager name (field) item of     */
  /*  the MQTMC2 trigger message parameter document.                  */
  /********************************************************************/
  if ( error == NOERROR )
  {
    pFieldName = QMGRNAME;
        pFieldValue =  pTrigMsg->QMgrName;
        FieldLength = sizeof( pTrigMsg->QMgrName);
        error = SetItemText ( hParmNote, pFieldName, pFieldValue, FieldLength);
   }

   strncpy(printstr, pFieldValue, FieldLength );
   writeLog (  logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "NSFSetItemText",       // sub string 1
               pFieldName,             // sub string 2
               printstr                // sub string 3
               ) ;
} /* end of first if error == noerror block */

  strcpy(printstr, "Created and populated the trigger message parameter document ");

  writeLog ( logHandle,            // event log handle
           valueEventLevel,        // logging value from registry
           DEBUG_ERR,              // symbolic message id
           0,                      // rc from notes or 0.
           printstr,               // sub string 1
           "",                     // sub string 2
           ""                      // sub string 3
           ) ;

  /********************************************************************/
  /*  Calling Notes to create agent run context                       */
  /********************************************************************/
  if (error == NOERROR )
  {
	    strcpy(printstr, "About to create agent run context ");

	    writeLog ( logHandle,            // event log handle
	             valueEventLevel,        // logging value from registry
	             DEBUG_ERR,              // symbolic message id
	             0,                      // rc from notes or 0.
	             printstr,               // sub string 1
	             "",                     // sub string 2
	             ""                      // sub string 3
	             ) ;
    error = AgentCreateRunContext (hAgent, 0, 0, &hRunContext);
        writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "AgentCreateRunContext",   // sub string 1
               DBName,                     // sub string 2
               AgentName                      // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to redirect stdout if requested                   */
  /********************************************************************/
  if ( error == NOERROR  && RedirStdOut )
  {
	    strcpy(printstr, "About to redirect stdout");

	    writeLog ( logHandle,            // event log handle
	             valueEventLevel,        // logging value from registry
	             DEBUG_ERR,              // symbolic message id
	             0,                      // rc from notes or 0.
	             printstr,               // sub string 1
	             "",                     // sub string 2
	             ""                      // sub string 3
	             ) ;
    error = AgentRedirectStdout (hRunContext, AGENT_REDIR_MEMORY);
        writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "AgentRedirectStdout",   // sub string 1
               DBName,                     // sub string 2
               AgentName                      // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to set the agent run context                      */
  /********************************************************************/
  if (error == NOERROR )
  {
	    strcpy(printstr, "About to set agent run context");

	    writeLog ( logHandle,            // event log handle
	             valueEventLevel,        // logging value from registry
	             DEBUG_ERR,              // symbolic message id
	             0,                      // rc from notes or 0.
	             printstr,               // sub string 1
	             "",                     // sub string 2
	             ""                      // sub string 3
	             ) ;

    error = AgentSetDocumentContext( hRunContext, hParmNote );
        writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "AgentSetDocumentContext",   // sub string 1
               DBName,                     // sub string 2
               AgentName                      // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to run the agent                                  */
  /********************************************************************/
  if ( error == NOERROR )
  {
    writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTES_RUN,              // symbolic message id
               0,                      // rc from notes or 0.
               DBName,                     // sub string 1
               AgentName,                      // sub string 2
                           (char*) pTrigMsg                                     // sub string 3
               ) ;

   printf("Running \"%s\" in database \"%s\" with \"%s\"\n\n",
          AgentName, DBName, pTrigMsg);

    error = AgentRun (hAgent, hRunContext, 0, 0);
        writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               error,                  // rc from notes or 0.
               "AgentRun",   // sub string 1
               DBName,                     // sub string 2
               AgentName                      // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to print the agent stdout buffer if requested     */
  /*  Note that AgentQueryStdoutBuffer, OSLock, and OSUnlock never    */
  /*  return error.                                                   */
  /********************************************************************/
  if ( error == NOERROR )
  {
    if ( RedirStdOut )
    {
      HANDLE          hStdout;
      DWORD           dwLen;
      char            *pStdout;

      AgentQueryStdoutBuffer ( hRunContext, &hStdout, &dwLen);
      pStdout = OSLock(char, hStdout);
      pStdout[dwLen]='\0';
      writeLog ( logHandle,        // event log handle
               valueEventLevel,    // logging value from registry
               AGENT_OUT,          // symbolic message id
               0,                  // rc from notes or 0.
               pStdout,            // sub string 1
               "",                 // sub string 2
               ""                  // sub string 3
               ) ;
      printf("%s\n\n", pStdout);
      OSUnlock(hStdout);

    }
  }
  /********************************************************************/
  /*  Calling Notes to close the MQTMC2 trigger message parameter     */
  /*  document.                                                       */
  /********************************************************************/
  if ( hParmNote != NULLHANDLE )
  {
    STATUS CloseError;
    CloseError = NSFNoteClose ( hParmNote );
        writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               CloseError,                  // rc from notes or 0.
               "NSFNoteClose",   // sub string 1
               DBName,                     // sub string 2
               AgentName                      // sub string 3
               ) ;
  }

  /********************************************************************/
  /*  Calling Notes to destroy the agent run context                  */
  /*  This function never returns an error.                           */
  /********************************************************************/
  if ( hRunContext != NULL )
  {
    AgentDestroyRunContext (hRunContext);
  }

  /********************************************************************/
  /*  Calling Notes to close the agent                                */
  /*  This function never returns an error.                           */
  /********************************************************************/
  if ( hAgent != NULL )
  {
    AgentClose (hAgent);
  }

  /********************************************************************/
  /*  Calling Notes to close the database                             */
  /********************************************************************/
  if ( hDb != NULLHANDLE )
  {
    STATUS CloseError;
    CloseError = NSFDbClose ( hDb );
        writeLog ( logHandle,              // event log handle
               valueEventLevel,        // logging value from registry
               NOTESAPI_ERR,            // symbolic message id
               CloseError,                  // rc from notes or 0.
               "NSFDbClose",   // sub string 1
               DBName,                     // sub string 2
               ""                      // sub string 3
               ) ;
  }

  if ( initialized )  NotesTerm();   /* close the connection to Notes */

  /* v1.3.1a added ExitProcess to clean up nicely, Windows *sometimes* */
  /* seems to leave the DOS windows open */

  fflush(stdout);
  fflush(stderr);
  ExitProcess(0);


} // end of start proc

/*********************************************************************/
/*                                                                   */
/* Function: PutErrorEvent                                           */
/*                                                                   */
/*    Writes an entry into the NT event log.                         */
/*                                                                   */
/*  If the Notes return code is 0, then we log this as informational,*/
/*  else we log this as an error.                                    */
/*                                                                   */
/*********************************************************************/

void writeLog ( HANDLE logHandle,       // event log handle
                DWORD valueEventLevel,  // logging value from registry
                long   msgId,           // symbolic message id
                STATUS notesRC,         // rc from notes or 0.
                char   *string1,        // sub string 1
                char   *string2,        // sub string 2
                char   *string3 )       // sub string 3
{
        char MsgString[MAXPATH] = "";    /* message string */
        LPCSTR    substr[5];
        int       errorType=0;
        char      decString[16];

        /* Get a Notes string explaining the error only if it is  */
        /* a notes API error code that we have here ...    */

        if (notesRC && (msgId == NOTESAPI_ERR)) {
           OSLoadString(NULLHANDLE, ERR(notesRC), MsgString, sizeof(MsgString)-1 );
             substr[4] = MsgString;
        } else {
             substr[4] = "";
        }

      /* only log to Event log if notesRC > 0 or eventLevel > 2 */
      /* or if it is the "about to run agent" message    */

        if (notesRC ||
            valueEventLevel > 2 ||
            (valueEventLevel > 1 && msgId == NOTES_RUN) ||
            (msgId == AGENT_OUT) ) {
           if (notesRC)
               if ((notesRC == ERR_NOEXIST) ||
                   (notesRC == ERR_NOT_FOUND ))
                  errorType = EVENTLOG_WARNING_TYPE;
               else
                  errorType = EVENTLOG_ERROR_TYPE;
           else
               errorType = EVENTLOG_INFORMATION_TYPE;

           substr[0] = string1;
           sprintf(decString, "%d", notesRC);
           substr[1] = decString;
           substr[2] = string2;
           substr[3] = string3;

           ReportEvent( logHandle, errorType,  NULL,
                        msgId, NULL, 5, 0, substr, NULL);
        }

} // end of writeLog


/*********************************************************************/
/*                                                                   */
/* Function: SetItemText                                             */
/*                                                                   */
/*    Writes a text item to the specified note (Notes document)      */
/*                                                                   */
/* Parameters in:   none                                             */
/*     hParmNote    NOTEHANDLE of the target note                    */
/*     pItemName    name of the item to be updated (Notes field)     */
/*     pItemSrc     text data to be written                          */
/*     lItemLen     length of data to be written                     */
/*                                                                   */
/* Parameters out: none                                              */
/*                                                                   */
/* Returns:        Notes error code                                  */
/*                                                                   */
/*********************************************************************/

STATUS  SetItemText( NOTEHANDLE  hParmNote
                           , char      * pItemName
                           , char      * pItemSrc
                           , long        lItemLen
                           )
{
  char    ItemData[sizeof(MQTMC2) + 1];
  char *  pItemData = ItemData;
  STATUS  error     = NOERROR;
  long    i;

  memcpy ( pItemData, pItemSrc, lItemLen );
  pItemData[ lItemLen ] = '\0';
  for ( i = lItemLen - 1 ; i >= 0 && pItemData[i] == ' '; i-- )
  {
    pItemData[i] = '\0';
  }
  error = NSFItemSetText ( hParmNote
                         , pItemName
                         , pItemData
                         , MAXWORD);

  return ERR(error);
}
