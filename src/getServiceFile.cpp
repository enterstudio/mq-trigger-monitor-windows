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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVICENAME  "WebSphere MQ Client Trigger Service"

void getServiceName(char * serviceName) {

  FILE *inf;
  strcpy(serviceName, SERVICENAME);
  if ((inf=fopen("servicename","r")) != NULL) {
    fgets(serviceName, 127, inf);
    int messlen = strlen(serviceName);       /* length without null        */
       if (serviceName[messlen-1] == '\n')  /* last char is a new-line    */
         serviceName[messlen-1]  = '\0';    /* replace new-line with null */
    fclose(inf);
  }

  //FILE *f;
  // if(f = fopen("c:/temp/trigsvc.txt", "a+")) {
  //   fprintf (f,"servicename = %s\n", serviceName);
  //   fclose(f);
  // }
  //  printf("setting serive name to %s", serviceName);
}
     

