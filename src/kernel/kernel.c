#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ARDUINO
#include <termios.h>
#endif
#include <unistd.h>

#include "debug.h"
#include "kernel.h"
#include "kernelfs.h"
#include "minifs.h"
#include "procman.h"
#include "progmembin.h"
#include "progmemlibcurses.h"
#include "progmemlibstdio.h"
#include "progmemlibstdmath.h"
#include "progmemlibstdproc.h"
#include "progmemlibstdmem.h"
#include "progmemlibstdstr.h"
#include "progmemlibstdtime.h"
#include "progmemusrbin.h"
#include "wrapper.h"

#define KernelTmpDataPoolSize (8*1024) // 8kb - used as ram (will have to be smaller on Uno presumably)
uint8_t *kernelTmpDataPool=NULL;

#define KernelBinSize PROGMEMbinDATASIZE
#define KernelLibCursesSize PROGMEMlibcursesDATASIZE
#define KernelLibStdIoSize PROGMEMlibstdioDATASIZE
#define KernelLibStdMathSize PROGMEMlibstdmathDATASIZE
#define KernelLibStdProcSize PROGMEMlibstdprocDATASIZE
#define KernelLibStdMemSize PROGMEMlibstdmemDATASIZE
#define KernelLibStdStrSize PROGMEMlibstdstrDATASIZE
#define KernelLibStdTimeSize PROGMEMlibstdtimeDATASIZE
#define KernelUsrBinSize PROGMEMusrbinDATASIZE

#define KernelEepromSize (8*1024) // Mega has 4kb for example
#ifndef ARDUINO
const char *kernelFakeEepromPath="./eeprom";
FILE *kernelFakeEepromFile=NULL;
#endif

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

void kernelBoot(void);
void kernelShutdown(void);

void kernelHalt(void);

void kernelFatalError(const char *format, ...);
void kernelFatalErrorV(const char *format, va_list ap);

bool kernelRootGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelDevGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelLibGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelLibStdGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelMediaGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
bool kernelUsrGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]);
int kernelBinReadFunctor(KernelFsFileOffset addr);
int kernelLibCursesReadFunctor(KernelFsFileOffset addr);
int kernelLibStdIoReadFunctor(KernelFsFileOffset addr);
int kernelLibStdMathReadFunctor(KernelFsFileOffset addr);
int kernelLibStdProcReadFunctor(KernelFsFileOffset addr);
int kernelLibStdMemReadFunctor(KernelFsFileOffset addr);
int kernelLibStdStrReadFunctor(KernelFsFileOffset addr);
int kernelLibStdTimeReadFunctor(KernelFsFileOffset addr);
int kernelHomeReadFunctor(KernelFsFileOffset addr);
bool kernelHomeWriteFunctor(KernelFsFileOffset addr, uint8_t value);
uint8_t kernelHomeMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelHomeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int kernelTmpReadFunctor(KernelFsFileOffset addr);
bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value);
uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData);
void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData);
int kernelDevZeroReadFunctor(void);
bool kernelDevZeroCanReadFunctor(void);
bool kernelDevZeroWriteFunctor(uint8_t value);
int kernelDevFullReadFunctor(void);
bool kernelDevFullCanReadFunctor(void);
bool kernelDevFullWriteFunctor(uint8_t value);
int kernelDevNullReadFunctor(void);
bool kernelDevNullCanReadFunctor(void);
bool kernelDevNullWriteFunctor(uint8_t value);
int kernelDevURandomReadFunctor(void);
bool kernelDevURandomCanReadFunctor(void);
bool kernelDevURandomWriteFunctor(uint8_t value);
int kernelDevTtyS0ReadFunctor(void);
bool kernelDevTtyS0CanReadFunctor(void);
bool kernelDevTtyS0WriteFunctor(uint8_t value);
int kernelUsrBinReadFunctor(KernelFsFileOffset addr);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

void setup() {
	// Init
	kernelBoot();

	// Run processes
	while(procManGetProcessCount()>0)
		procManTickAll();

	// Quit
	kernelShutdown();
}

void loop() {
	// Do nothing - we should never reach here
}

#ifndef ARDUINO

int main(int argc, char **argv) {
	// save terminal settings
	static struct termios termOld, termNew;
	tcgetattr(STDIN_FILENO, &termOld);

	// change terminal settings to avoid waiting for a newline before submitting any data
	termNew=termOld;
	termNew.c_lflag&=~ICANON;
	tcsetattr(STDIN_FILENO, TCSANOW, &termNew);

	// setup and main loop
	setup();
	loop();

	// restore terminal settings
	tcsetattr(STDIN_FILENO, TCSANOW, &termOld);

	return 0;
}

#endif

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

void kernelBoot(void) {
	debugLog(DebugLogTypeInfo, "booting\n");

#ifndef ARDUINO
	// ON the Arduino we can leave this at 0 but otherwise we have to save offset
	kernelBootTime=millisRaw();
	debugLog(DebugLogTypeInfo, "set kernel boot time to %lu (PC wrapper)\n", kernelBootTime);
#endif

	// Arduino-only: connect to serial (ready to mount as /dev/ttyS0).
#ifdef ARDUINO
	Serial.begin(9600);
	while (!Serial) ;
	debugLog(DebugLogTypeInfo, "initialised serial\n");
#endif

	// Allocate space for /tmp
	kernelTmpDataPool=malloc(KernelTmpDataPoolSize);
	if (kernelTmpDataPool==NULL)
		kernelFatalError("could not allocate /tmp data pool (size %u)\n", KernelTmpDataPoolSize);
	debugLog(DebugLogTypeInfo, "allocated /tmp space (size %u)\n", KernelTmpDataPoolSize);

	// Non-arduino-only: create pretend EEPROM storage in a local file
#ifndef ARDUINO
	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "a+");
	if (kernelFakeEepromFile==NULL)
		kernelFatalError("could not open/create pseudo EEPROM storage file at '%s' (PC wrapper)\n", kernelFakeEepromPath);
	fclose(kernelFakeEepromFile);

	kernelFakeEepromFile=fopen(kernelFakeEepromPath, "r+");
	if (kernelFakeEepromFile==NULL)
		kernelFatalError("could not open pseudo EEPROM storage file at '%s' for reading and writing (PC wrapper)\n", kernelFakeEepromPath);
	fseek(kernelFakeEepromFile, 0L, SEEK_END);
	int eepromInitialSize=ftell(kernelFakeEepromFile);

	while(eepromInitialSize<KernelEepromSize) {
		fputc(0xFF, kernelFakeEepromFile);
		++eepromInitialSize;
	}

	debugLog(DebugLogTypeInfo, "openned pseudo EEPROM storage file (PC wrapper)\n");
#endif

	// Format /home if it does not look like it has been already
	MiniFs homeMiniFs;
	if (miniFsMountSafe(&homeMiniFs, &kernelHomeMiniFsReadFunctor, &kernelHomeMiniFsWriteFunctor, NULL)) {
		miniFsUnmount(&homeMiniFs); // Unmount so we can mount again when we initialise the file system
		debugLog(DebugLogTypeInfo, "/home volume already exists\n");
	} else {
		if (!miniFsFormat(&kernelHomeMiniFsWriteFunctor, NULL, KernelEepromSize))
			kernelFatalError("could not format new /home volume\n");
		debugLog(DebugLogTypeInfo, "formatted new /home volume (size %u)\n", KernelEepromSize);

		// Add a few example files
		// TODO: this is only temporary
		miniFsMountSafe(&homeMiniFs, &kernelHomeMiniFsReadFunctor, &kernelHomeMiniFsWriteFunctor, NULL);
		debugMiniFsAddDir(&homeMiniFs, "../src/tools/builder/mockups/homemockup");
		miniFsDebug(&homeMiniFs);
		miniFsUnmount(&homeMiniFs);
		debugLog(DebugLogTypeInfo, "add example files to /home\n");
	}

	// Format RAM used for /tmp
	if (!miniFsFormat(&kernelTmpMiniFsWriteFunctor, NULL, KernelTmpDataPoolSize))
		kernelFatalError("could not format /tmp volume\n");
	debugLog(DebugLogTypeInfo, "formatted volume representing /tmp\n");

	// Init file system and add virtual devices
	kernelFsInit();

	bool error=false;
	error|=!kernelFsAddDirectoryDeviceFile("/", &kernelRootGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/bin", KernelFsBlockDeviceFormatCustomMiniFs, KernelBinSize, &kernelBinReadFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/dev", &kernelDevGetChildFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/zero", &kernelDevZeroReadFunctor, &kernelDevZeroCanReadFunctor, &kernelDevZeroWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/full", &kernelDevFullReadFunctor, &kernelDevFullCanReadFunctor, &kernelDevFullWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/null", &kernelDevNullReadFunctor, &kernelDevNullCanReadFunctor, &kernelDevNullWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/urandom", &kernelDevURandomReadFunctor, &kernelDevURandomCanReadFunctor, &kernelDevURandomWriteFunctor);
	error|=!kernelFsAddCharacterDeviceFile("/dev/ttyS0", &kernelDevTtyS0ReadFunctor, &kernelDevTtyS0CanReadFunctor, &kernelDevTtyS0WriteFunctor);
	error|=!kernelFsAddBlockDeviceFile("/home", KernelFsBlockDeviceFormatCustomMiniFs, KernelEepromSize, &kernelHomeReadFunctor, kernelHomeWriteFunctor);
	error|=!kernelFsAddDirectoryDeviceFile("/lib", &kernelLibGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/lib/curses", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibCursesSize, &kernelLibCursesReadFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/lib/std", &kernelLibStdGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/io", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdIoSize, &kernelLibStdIoReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/math", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdMathSize, &kernelLibStdMathReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/proc", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdProcSize, &kernelLibStdProcReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/mem", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdMemSize, &kernelLibStdMemReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/str", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdStrSize, &kernelLibStdStrReadFunctor, NULL);
	error|=!kernelFsAddBlockDeviceFile("/lib/std/time", KernelFsBlockDeviceFormatCustomMiniFs, KernelLibStdTimeSize, &kernelLibStdTimeReadFunctor, NULL);
	error|=!kernelFsAddDirectoryDeviceFile("/media", &kernelMediaGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/tmp", KernelFsBlockDeviceFormatCustomMiniFs, KernelTmpDataPoolSize, &kernelTmpReadFunctor, kernelTmpWriteFunctor);
	error|=!kernelFsAddDirectoryDeviceFile("/usr", &kernelUsrGetChildFunctor);
	error|=!kernelFsAddBlockDeviceFile("/usr/bin", KernelFsBlockDeviceFormatCustomMiniFs, KernelUsrBinSize, &kernelUsrBinReadFunctor, NULL);

	if (error)
		kernelFatalError("could not initialise filesystem\n");
	debugLog(DebugLogTypeInfo, "initialised filesystem\n");

	// Initialise process manager and start init process
	procManInit();
	debugLog(DebugLogTypeInfo, "initialised process manager\n");

	debugLog(DebugLogTypeInfo, "starting init\n");
	if (procManProcessNew("/bin/init")==ProcManPidMax)
		kernelFatalError("could not start init at '%s'\n", "/bin/init");
}

void kernelShutdown(void) {
	debugLog(DebugLogTypeInfo, "shutting down\n");

	// Quit process manager
	debugLog(DebugLogTypeInfo, "killing all processes\n");
	procManQuit();

	// Quit file system
	debugLog(DebugLogTypeInfo, "unmounting filesystem\n");
	kernelFsQuit();

	// Arduino-only: close serial connection (was mounted as /dev/ttyS0).
#ifdef ARDUINO
	debugLog(DebugLogTypeInfo, "closing serial connection\n");
	Serial.end();
#endif

	// Free /tmp memory pool
	debugLog(DebugLogTypeInfo, "freeing /tmp space\n");
	free(kernelTmpDataPool);

	// Non-arduino-only: close pretend EEPROM storage file
#ifndef ARDUINO
	debugLog(DebugLogTypeInfo, "closing pseudo EEPROM storage file (PC wrapper)\n");
	fclose(kernelFakeEepromFile); // TODO: Check return
#endif

	// Halt
	kernelHalt();
}

void kernelHalt(void) {
#ifdef ARDUINO
	debugLog(DebugLogTypeInfo, "halting\n");
	while(1)
		;
#else
	debugLog(DebugLogTypeInfo, "exiting (PC wrapper)\n");
	exit(0);
#endif
}

void kernelFatalError(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	kernelFatalErrorV(format, ap);
	va_end(ap);
}

void kernelFatalErrorV(const char *format, va_list ap) {
	debugLogV(DebugLogTypeError, format, ap);
	kernelHalt();
}

bool kernelRootGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/bin"); return true; break;
		case 1: strcpy(childPath, "/dev"); return true; break;
		case 2: strcpy(childPath, "/lib"); return true; break;
		case 3: strcpy(childPath, "/home"); return true; break;
		case 4: strcpy(childPath, "/media"); return true; break;
		case 5: strcpy(childPath, "/tmp"); return true; break;
		case 6: strcpy(childPath, "/usr"); return true; break;
	}
	return false;
}

bool kernelDevGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/dev/zero"); return true; break;
		case 1: strcpy(childPath, "/dev/full"); return true; break;
		case 2: strcpy(childPath, "/dev/null"); return true; break;
		case 3: strcpy(childPath, "/dev/urandom"); return true; break;
		case 4: strcpy(childPath, "/dev/ttyS0"); return true; break;
	}
	return false;
}

bool kernelLibGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/lib/std"); return true; break;
		case 1: strcpy(childPath, "/lib/curses"); return true; break;
	}
	return false;
}

bool kernelLibStdGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/lib/std/io"); return true; break;
		case 1: strcpy(childPath, "/lib/std/math"); return true; break;
		case 2: strcpy(childPath, "/lib/std/proc"); return true; break;
		case 3: strcpy(childPath, "/lib/std/mem"); return true; break;
		case 4: strcpy(childPath, "/lib/std/str"); return true; break;
		case 5: strcpy(childPath, "/lib/std/time"); return true; break;
	}
	return false;
}

bool kernelMediaGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	// TODO: this (along with implementing mounting of external drives)
	return false;
}

bool kernelUsrGetChildFunctor(unsigned childNum, char childPath[KernelFsPathMax]) {
	switch(childNum) {
		case 0: strcpy(childPath, "/usr/bin"); return true; break;
	}
	return false;
}

int kernelBinReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelBinSize);
	return progmembinData[addr];
}

int kernelLibCursesReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibCursesSize);
	return progmemlibcursesData[addr];
}

int kernelLibStdIoReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdIoSize);
	return progmemlibstdioData[addr];
}

int kernelLibStdMathReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdMathSize);
	return progmemlibstdmathData[addr];
}

int kernelLibStdProcReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdProcSize);
	return progmemlibstdprocData[addr];
}

int kernelLibStdMemReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdProcSize);
	return progmemlibstdmemData[addr];
}

int kernelLibStdStrReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdStrSize);
	return progmemlibstdstrData[addr];
}

int kernelLibStdTimeReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelLibStdTimeSize);
	return progmemlibstdtimeData[addr];
}

int kernelHomeReadFunctor(KernelFsFileOffset addr) {
#ifdef ARDUINO
	return EEPROM.read(addr);
#else
	int res=fseek(kernelFakeEepromFile, addr, SEEK_SET);
	assert(res==0);
	assert(ftell(kernelFakeEepromFile)==addr);
	int c=fgetc(kernelFakeEepromFile);
	if (c==EOF)
		return -1;
	else
		return c;
#endif
}

bool kernelHomeWriteFunctor(KernelFsFileOffset addr, uint8_t value) {
#ifdef ARDUINO
	EEPROM.update(addr, value);
	return true;
#else
	int res=fseek(kernelFakeEepromFile, addr, SEEK_SET);
	assert(res==0);
	assert(ftell(kernelFakeEepromFile)==addr);
	return (fputc(value, kernelFakeEepromFile)!=EOF);
#endif
}

uint8_t kernelHomeMiniFsReadFunctor(uint16_t addr, void *userData) {
	int value=kernelHomeReadFunctor(addr);
	assert(value>=0 && value<256);
	return value;
}

void kernelHomeMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelHomeWriteFunctor(addr, value);
	assert(result);
}

int kernelTmpReadFunctor(KernelFsFileOffset addr) {
	return kernelTmpDataPool[addr];
}

bool kernelTmpWriteFunctor(KernelFsFileOffset addr, uint8_t value) {
	kernelTmpDataPool[addr]=value;
	return true;
}

uint8_t kernelTmpMiniFsReadFunctor(uint16_t addr, void *userData) {
	int value=kernelTmpReadFunctor(addr);
	assert(value>=0 && value<256);
	return value;
}

void kernelTmpMiniFsWriteFunctor(uint16_t addr, uint8_t value, void *userData) {
	bool result=kernelTmpWriteFunctor(addr, value);
	assert(result);
}

int kernelDevZeroReadFunctor(void) {
	return 0;
}

bool kernelDevZeroCanReadFunctor(void) {
	return true;
}

bool kernelDevZeroWriteFunctor(uint8_t value) {
	return true;
}

int kernelDevFullReadFunctor(void) {
	return 0;
}

bool kernelDevFullCanReadFunctor(void) {
	return true;
}

bool kernelDevFullWriteFunctor(uint8_t value) {
	return false;
}

int kernelDevNullReadFunctor(void) {
	return 0;
}

bool kernelDevNullCanReadFunctor(void) {
	return true;
}

bool kernelDevNullWriteFunctor(uint8_t value) {
	return true;
}

int kernelDevURandomReadFunctor(void) {
	return rand()&0xFF;
}

bool kernelDevURandomCanReadFunctor(void) {
	return true;
}

bool kernelDevURandomWriteFunctor(uint8_t value) {
	return false;
}

int kernelDevTtyS0ReadFunctor(void) {
#ifdef ARDUINO
	return Serial.read();
#else
	int c=getchar();
	return (c!=EOF ? c : -1);
#endif
}

bool kernelDevTtyS0CanReadFunctor(void) {
#ifdef ARDUINO
	return (Serial.available()>0);
#else
	struct pollfd pollFds[0];
	memset(pollFds, 0, sizeof(pollFds));
	pollFds[0].fd=STDIN_FILENO;
	pollFds[0].events=POLLIN;
	if (poll(pollFds, 1, 0)<=0)
		return false;

	if (!(pollFds[0].revents & POLLIN))
		return false;

	return true;
#endif
}

bool kernelDevTtyS0WriteFunctor(uint8_t value) {
#ifdef ARDUINO
	return (Serial.write(value)==1);
#else
	if (putchar(value)!=value)
		return false;
	fflush(stdout);
	return true;
#endif
}

int kernelUsrBinReadFunctor(KernelFsFileOffset addr) {
	assert(addr<KernelUsrBinSize);
	return progmemusrbinData[addr];
}