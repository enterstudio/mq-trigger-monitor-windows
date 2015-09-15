#NOTESDRIVE=C:\IBMPROG
#NOTESDIR=\notesapi
MQDRIVE=C:\IBMPROG
MQDIR=\MQ80


NOTESSTR="\"Version 7.0\""
MA7KSTR="\"1.6\""

# Base linker flags
# 64 bit compile
LFLAGS=kernel32.lib crypt32.lib advapi32.lib /INCREMENTAL:NO /DEBUG /MACHINE:X64
# 32 bit compile
#LFLAGS=kernel32.lib crypt32.lib advapi32.lib /INCREMENTAL:NO /DEBUG /MACHINE:X86

#LFLAGS=$(LFLAGS) $(NOTESDRIVE)$(NOTESDIR)\lib\mswin32\notes.lib $(MQDRIVE)$(MQDIR)\Tools\Lib\MQIC.LIB
LFLAGS=$(LFLAGS) $(MQDRIVE)$(MQDIR)\Tools\Lib64\MQIC.LIB

INCL=$(MQDRIVE)$(MQDIR)\Tools\c\include /I $(MQDRIVE)$(MQDIR)\Tools\cplus\include 
#/I $(NOTESDRIVE)$(NOTESDIR)\include 
PATH=$(MQDRIVE)$(MQDIR)\bin:$(MQDRIVE)$(MQDIR)\Tools\c\samples\bin:$(PATH)

# License file
LICFILE=license\*

CC=cl
LC=link

ZIPTOOL=perl 
#ZIPCMD=-add -header=@$(LICFILE)
ZIPCMD= zip.pl

.SUFFIXES: .c .cpp .cxx .rc .mc .res .obj$(V7SUF) .exe$(V7SUF)

all: clean trigsvc.exe setup.exe update.exe remove.exe package
includeNotes: clean trigsvc.exe trige22.exe trige22.exe$(V7SUF) setup.exe update.exe remove.exe packageWithNotes
remove: remove.exe update.exe setup.exe trigsvc.exe

clean:
	-if exist *.obj 	 erase *.obj
        -if exist *.obj$(V7SUF)  erase *.obj$(V7SUF)
        -if exist *.err 	 erase *.err
#        -if exist *.bin 	 erase *.bin
#        -if exist *.rc           erase *.rc
#        -if exist *.res 	 erase *.res
        -if exist ma7k.zip 	 erase ma7k.zip
        -if exist *~ 		 erase *~
package:
        -if exist ma7k.zip 	 erase ma7k.zip
         $(ZIPTOOL) $(ZIPCMD) ma7k.zip setup.exe update.exe trigsvc.exe remove.exe ma7k-1.6.1.pdf $(LICFILE) setup.ini ma7k.mqsc servicename *.cpp *.h *.mc makefile

packageWithNotes:
        -if exist ma7k.zip 	 erase ma7k.zip
         $(ZIPTOOL) $(ZIPCMD) ma7k.zip setup.exe update.exe trigsvc.exe remove.exe trige22.exe ma7k-1.6.1.pdf $(LICFILE) setup.ini ma7k.mqsc servicename *.cpp *.h *.mc makefile
 

trigsvc.exe: msgs.res getServiceFile.obj trigsvc.obj
	$(LC) $(LFLAGS) getServiceFile.obj $*.obj msgs.res /out:$*.exe
		copy $*.exe ..\bin
	
getServiceFile.obj : getServiceFile.cpp
setup.exe  : setup.obj getServiceFile.obj
update.exe : update.obj getServiceFile.obj
remove.exe : remove.obj getServiceFile.obj
trige22.exe: trige22.obj
trigsvc.obj: trigsvc.cpp trigsvc.h msgs.h
trige22.obj: trige22.cpp trigsvc.h msgs.h trigs22.h
setup.obj  : setup.cpp trigsvc.h
update.obj : update.cpp trigsvc.h
remove.obj : remove.cpp trigsvc.h


msgs.h : msgs.mc
msgs.rc : msgs.mc  
msgs.res: msgs.rc

.cpp.obj:
   $(CC) /c /DNT /Zi /DNVERSION=$(MA7KSTR) /EHsc /I$(INCL) $*.cpp

.c.obj:
   $(CC) /c /DNT /Zi /DNVERSION=$(MA7KSTR) /EHsc /I$(INCL) $*.c

.obj.exe:
   $(LC) $(LFLAGS) getServiceFile.obj $*.obj /out:$*.exe
	copy $*.exe ..\bin


.mc.h:
   mc $*.mc

.mc.rc:
   mc $*.mc
