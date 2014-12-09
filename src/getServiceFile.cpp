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
     

