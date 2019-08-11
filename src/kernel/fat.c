#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat.h"
#include "log.h"

////////////////////////////////////////////////////////////////////////////////
// Private prototypes
////////////////////////////////////////////////////////////////////////////////

uint32_t fatRead(Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);
uint32_t fatWrite(Fat *fs, uint32_t addr, uint8_t *data, uint32_t len);

////////////////////////////////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////////////////////////////////

bool fatMountFast(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData) {
	uint8_t byte;

	// Set Fat struct fields
	fs->readFunctor=readFunctor;
	fs->writeFunctor=writeFunctor;
	fs->userData=functorUserData;

	// Verify two magic bytes at end of first block
	if (fatRead(fs, 510, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read first magic byte at addr 510\n"));
		return false;
	}
	if (byte!=0x55) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad first magic byte value 0x%02X at addr 510 (expecting 0x%02X)\n"), byte, 0x55);
		return false;
	}
	if (fatRead(fs, 511, &byte, 1)!=1) {
		kernelLog(LogTypeWarning, kstrP("fatMount: could not read second magic byte at addr 511\n"));
		return false;
	}
	if (byte!=0xAA) {
		kernelLog(LogTypeWarning, kstrP("fatMount: bad second magic byte value 0x%02X at addr 511 (expecting 0x%02X)\n"), byte, 0xAA);
		return false;
	}

	return true;
}

bool fatMountSafe(Fat *fs, FatReadFunctor *readFunctor, FatWriteFunctor *writeFunctor, void *functorUserData) {
	// Use mount fast first to do basic checks and fill in Fat structure.
	if (!fatMountFast(fs, readFunctor, writeFunctor, functorUserData))
		return false;

	// TODO: this for Fat file system support .....

	return false;
}

void fatUnmount(Fat *fs) {
	// TODO: this for Fat file system support .....
}

void fatDebug(const Fat *fs) {
	// TODO: this for Fat file system support .....
}

////////////////////////////////////////////////////////////////////////////////
// Private functions
////////////////////////////////////////////////////////////////////////////////

uint32_t fatRead(Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	return fs->readFunctor(addr, data, len, fs->userData);
}

uint32_t fatWrite(Fat *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	if (fs->writeFunctor==NULL)
		return 0;
	return fs->writeFunctor(addr, data, len, fs->userData);
}
