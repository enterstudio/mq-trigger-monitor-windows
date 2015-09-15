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
// Remove.cpp
//
//
// See trigsvc.cpp for change log
// This program removes all registry entries and definitions for the trigsvc programs
#include <windows.h>
#include <iostream>
#include <stdio.h>

#include "trigsvc.h"

//using namespace std;

int main(int argc, char *argv[]) {
	LONG ret;
	CHAR strBuf[500];
	SC_HANDLE service, scm;
	BOOL success;
	SERVICE_STATUS status;
	char response[256];
	int debug;

	void getServiceName(char *);
	void getServiceLabel(char *);

	char serviceName[128];
	memset(serviceName, '\0', sizeof(serviceName));
	getServiceName(serviceName);
	if (argc > 1 && !strcmp(argv[1], "-d")) {
		printf("Enabled debug mode\n");
		debug = 1;
	}

	printf("This program REMOVES the Registry entries and Service definitions");
	printf(" for the \"%s\"\n", serviceName);

	printf("Enter \"Y\" to continue, anything else to exit.\n");

	fgets(response, sizeof(response), stdin);
	if (response[0] != 'y' && response[0] != 'Y') {
		printf("Exiting\n");
		return 4;
	}/* End if*/

	//
	// This segment removes the registry entries for event messages
	//

	// build the path for the key
	strcpy(strBuf, "SYSTEM\\CurrentControlSet\\");
	strcat(strBuf, "Services\\EventLog\\Application\\");
	strcat(strBuf, serviceName);

	if (debug) {
		printf("about to remove key %s and it's children.\n", strBuf);
	}
	// delete the key
	ret = RegDeleteKey(HKEY_LOCAL_MACHINE, strBuf);
	if (ret != ERROR_SUCCESS) {
		printf(
				"Unable to delete key: Ensure that the above named service actually\n was installed and ");
		printf(
				"check and make sure you are a member of Administrators. \nError=%ld\n",
				GetLastError());
	} else {
		printf("Event key deleted\n");
	}

	//
	// This service deletes the service entry in the applet
	//
	// open a connection to the SCM
	scm = OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE);
	if (!scm) {
		printf("In OpenScManager %ld\n", GetLastError());
	} else {
		if (debug) {
			printf(" ServiceControlManager opened\n");
		}
	}
	//
	// The service might already be installed, try to open it and delete
	// it if it already exists
	//
	// Get the service's handle
	service = OpenService(scm, serviceName,
	SERVICE_ALL_ACCESS | DELETE);
	if (debug) {
		printf("Opened service %s to remove it.\n", serviceName);
	}

	if (service) {
		printf("Deleting Service  \"%s\"\n", serviceName);
		// Stop the service if necessary
		success = QueryServiceStatus(service, &status);
		if (!success)
			printf("Failed in QueryServiceStatus with Error = %ld\n",
					GetLastError());
		if (status.dwCurrentState != SERVICE_STOPPED) {
			printf("Stopping service...(this will take awhile)\n");
			success = ControlService(service,
			SERVICE_CONTROL_STOP, &status);
			if (!success)
				printf("Failed in ControlService with Error = %ld\n",
						GetLastError());
			if (debug) {
				printf("About to sleep to give service a chance to stop.\n");
			}
			Sleep(5000);
		}
		// Remove the service
		success = DeleteService(service);
		if (success)
			printf(" %s Service removed\n", serviceName);
		else
			printf("Failed in DeleteService with Error = %ld\n",
					GetLastError());
		Sleep(5000);
		CloseServiceHandle(service);
	}  // end of service opened

	CloseServiceHandle(scm);
	if (debug) {
		printf(" ServiceControlManager closed\n");
	}

	//
	// Lastly, we delete the application keys
	//

	// build the path for the new key
	strcpy(strBuf, KEYPREFIX);
	strcat(strBuf, serviceName);
	if (debug) {
		printf(" Path to registry keys is %s\n", strBuf);
	}

	// DELETE key for the new source
	ret = RegDeleteKey(HKEY_LOCAL_MACHINE, strBuf);
	if (ret != ERROR_SUCCESS) {
		printf(
				"Unable to delete key: Ensure that the above named service actually\n was installed and ");
		printf(
				"check and make sure you are a member of Administrators. \nError= %ld\n",
				GetLastError());
	} else {
		printf("Registry key for application deleted.\n");
	}

	printf("Removal complete\n");
	return 0;
}

