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
 
// common defines for IBM WebSphere MQ Trigger Service
#define DISABLENOTES 1
#define SERVICEPROGRAM "TrigSvc.exe"
#define TRIGGERQUEUENAME "TriggerQueueName"
#define TRIGGERQUEUEMGRNAME "TriggerQueueMgrName"
#define SERVICENAME "ServiceName"
#define SERVICEQUEUEMGRNAME "ServiceQueueMgrName"
#define NOTESINI "NotesIniLocation"
#define WAITINTERVAL "WaitInterval"
#define LONGRTY "LongRty"
#define LONGTMR "LongTmr"
#define SHORTRTY "ShortRty"
#define SHORTTMR "ShortTmr"
#define EVENTLEVEL "EventLevel"
#define MQDLL "MQSeriesDLL"
#define DEFAULTQUEUE "SYSTEM.DEFAULT.INITIATION.QUEUE"
#define DEFAULTQMGR ""
#define KEYPREFIX "SOFTWARE\\IBM\\"
#define AGENTREDIR "AGENT_REDIR_STDOUT"
#define NOTESEXE "trige22.exe"
#define MYEXEPATH "ExePath"
#define MAXTHREADS 16
#define DEFAULTCMDSVRQ "SYSTEM.ADMIN.COMMAND.QUEUE\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"

/* V140 allows the direct specification of the MQCD via MQCONNX */
/* These strings define both the NT registry value names and the setup.ini keywords */

#define CONNAME           "CONNAME" 
#define CHANNEL           "CHANNEL"
#define TRPTYPE           "TRPTYPE"
#define LOCLADDR          "LOCLADDR"
#define HBINT             "HBINT"
#define RCVDATA           "RCVDATA"
#define RCVEXIT           "RCVEXIT"
#define SCYDATA           "SCYDATA"
#define SCYEXIT           "SCYEXIT"
#define SENDDATA          "SENDDATA"
#define SENDEXIT          "SENDEXIT"
#define USERID            "USERID"
#define SSLCIPH           "SSLCIPH"
#define SSLPEER           "SSLPEER"
#define KAINT             "KAINT"
#define CHANNELUID        "CHANNELUSERNAME"
#define CHANNELPW         "CHANNELPASSWORD"
#define MA7K_MQCD_VERSION "MQCD_VERSION"
#define KEYREPOS          "KEYREPOS"
#define USEQUOTES         "USEQUOTES"

#define PLACEHOLDER		  '*'

#ifndef MQRC_CHANNEL_NOT_AVAILABLE
#define MQRC_CHANNEL_NOT_AVAILABLE 2537
#endif
