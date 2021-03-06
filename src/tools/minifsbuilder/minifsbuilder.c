#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minifs.h"
#include "minifsextra.h"

typedef enum {
	MiniFsBuilderFormatCHeader,
	MiniFsBuilderFormatFlatFile,
	MiniFsBuilderFormatNB,
} MiniFsBuilderFormat;

const char *miniFsBuilderFormatOptionStrs[MiniFsBuilderFormatNB]={
	[MiniFsBuilderFormatCHeader]="-fcheader",
	[MiniFsBuilderFormatFlatFile]="-fflatfile",
};

bool buildVolumeMin(const char *name, const char *srcDir, const char *destDir, MiniFsBuilderFormat format);
bool buildVolumeExact(const char *name, uint16_t size, const char *srcDir, const char *destDir, MiniFsBuilderFormat format, bool verbose);

bool miniFsWriteCHeader(const char *name, uint16_t size, const char *destDir, uint8_t *dataArray, bool verbose);
bool miniFsWriteFlatFile(const char *name, uint16_t size, const char *destDir, uint8_t *dataArray, bool verbose);

uint16_t readFunctor(uint16_t addr, uint8_t *data, uint16_t len, void *userData);
uint16_t writeFunctor(uint16_t addr, const uint8_t *data, uint16_t len, void *userData);

int main(int argc, char **argv) {
	// Parse arguments
	if (argc<4) {
		printf("usage: %s [--size=SIZE] -fOUTPUTFORMAT srcdir volumename destdir\n", argv[0]);
		return 0;
	}

	MiniFsBuilderFormat outputFormat=MiniFsBuilderFormatNB;
	uint16_t maxSize=0; // 0 means use minimum required
	for(int i=1; i<argc-3; ++i) {
		if (strncmp(argv[i], "--size=", strlen("--size="))==0) {
			maxSize=atoi(argv[i]+strlen("--size="));
		} else {
			bool found=false;
			for(unsigned j=0; j<MiniFsBuilderFormatNB; ++j) {
				if (strcmp(argv[i], miniFsBuilderFormatOptionStrs[j])==0) {
					outputFormat=j;
					found=true;
				}
			}

			if (!found)
				printf("warning: unknown option '%s'\n", argv[i]);
		}
	}
	if (outputFormat==MiniFsBuilderFormatNB) {
		printf("unknown missing format argument\n");
		return 1;
	}
	const char *srcDir=argv[argc-3];
	const char *volumeName=argv[argc-2];
	const char *destDir=argv[argc-1];

	// Build volue
	bool success;
	if (maxSize==0)
		success=buildVolumeMin(volumeName, srcDir, destDir, outputFormat);
	else
		success=buildVolumeExact(volumeName, maxSize, srcDir, destDir, outputFormat, true);

	if (!success)
		printf("failed to build volume '%s'\n", volumeName);

	return 0;
}

bool buildVolumeMin(const char *name, const char *srcDir, const char *destDir, MiniFsBuilderFormat format) {
	// Loop, trying increasing powers of 2 for max volume size until we succeed (if we do at all).
	int32_t size; // needs to be 32 bit for loop termination condition to work
	for(size=MINIFSMINSIZE; size<=MINIFSMAXSIZE; size*=2) {
		if (buildVolumeExact(name, size, srcDir, destDir, format, false)) {
			// We have found a size that works, i.e an upper bound.

			// Try to shrink with a binary search
			uint16_t minGoodSize=size;
			uint16_t maxBadSize=(size>MINIFSMINSIZE ? size/2 : MINIFSMINSIZE);
			while(minGoodSize-MINIFSFACTOR>maxBadSize) {
				uint16_t trialSize=(maxBadSize+minGoodSize)/2;
				if (buildVolumeExact(name, trialSize, srcDir, destDir, format, false))
					minGoodSize=trialSize;
				else
					maxBadSize=trialSize;
			}

			// Use minimum size found
			if (buildVolumeExact(name, minGoodSize, srcDir, destDir, format, false))
				return true;
		}
	}

	// We have failed - run final size again but with logging turned on.
	return buildVolumeExact(name, MINIFSMAXSIZE, srcDir, destDir, format, true);
}

bool buildVolumeExact(const char *name, uint16_t size, const char *srcDir, const char *destDir, MiniFsBuilderFormat format, bool verbose) {
	MiniFs miniFs;
	uint8_t *dataArray=malloc(MINIFSMAXSIZE); // TODO: check return

	// clear data arary (not strictly necessary but might avoid confusion in the future when e.g. stdio functions are in unused part of the stdmath volume)
	// setting to 0xFF also matches value stored in uninitialised Arduino EEPROM
	memset(dataArray, 0xFF, MINIFSMAXSIZE);

	// format
	if (!miniFsFormat(&writeFunctor, dataArray, size)) {
		if (verbose)
			printf("could not format\n");
		goto error;
	}

	// mount
	if (!miniFsMountSafe(&miniFs, &readFunctor, &writeFunctor, dataArray)) {
		if (verbose)
			printf("could not mount\n");
		goto error;
	}

	// Loop over files in given source dir and write each one to minifs
	if (!miniFsExtraAddDir(&miniFs, srcDir, verbose)) {
		if (verbose)
			printf("could not add dir '%s'\n", srcDir);
		goto error;
	}

	// unmount to save any changes
	miniFsUnmount(&miniFs);

	// create output
	switch(format) {
		case MiniFsBuilderFormatCHeader:
			if (miniFsWriteCHeader(name, size, destDir, dataArray, verbose))
				goto success;
		break;
		case MiniFsBuilderFormatFlatFile:
			if (miniFsWriteFlatFile(name, size, destDir, dataArray, verbose))
				goto success;
		break;
		case MiniFsBuilderFormatNB:
			assert(false);
		break;
	}

	error:
	free(dataArray);
	return false;

	success:
	free(dataArray);
	return true;
}

bool miniFsWriteCHeader(const char *name, uint16_t size, const char *destDir, uint8_t *dataArray, bool verbose) {
	char hFilePath[1024]; // TODO: better
	sprintf(hFilePath, "%s/progmem%s.h", destDir, name);

	FILE *hFile=fopen(hFilePath, "w+");
	if (hFile==NULL) {
		if (verbose)
			printf("could not open '%s' for writing\n", hFilePath);
		return false;
	}

	fprintf(hFile, "// NOTE: File auto-generated (see minifsbuilder)\n\n");
	fprintf(hFile, "#ifndef PROGMEM%s_H\n", name);
	fprintf(hFile, "#define PROGMEM%s_H\n\n", name);
	fprintf(hFile, "#include <stdint.h>\n\n");

	fprintf(hFile, "#define PROGMEM%sDATASIZE %iu\n", name, size);
	fprintf(hFile, "#ifdef ARDUINO\n");
	fprintf(hFile, "#include <avr/pgmspace.h>\n");
	fprintf(hFile, "#define PROGMEM%sDATAPTR ((uint32_t)pgm_get_far_address(progmem%sData))\n", name, name);
	fprintf(hFile, "static const uint8_t progmem%sData[PROGMEM%sDATASIZE] PROGMEM ={\n", name, name);
	fprintf(hFile, "#else\n");
	fprintf(hFile, "#define PROGMEM%sDATAPTR ((uintptr_t)progmem%sData)\n", name, name);
	fprintf(hFile, "static const uint8_t progmem%sData[PROGMEM%sDATASIZE]={\n", name, name);
	fprintf(hFile, "#endif\n");
	fprintf(hFile, "\t");
	const int perLine=16;
	for(int i=0; i<size; ++i) {
		fprintf(hFile, "0x%02X", dataArray[i]);
		if (i+1<size)
			fprintf(hFile, ",");
		if ((i+1)%perLine==0) {
			fprintf(hFile, " // ");
			for(int j=0; j<perLine; ++j) {
				int c=dataArray[i+1-perLine+j];
				fprintf(hFile, "%c", isgraph(c) ? c : '.');
			}
			fprintf(hFile, " END\n");
			if (i+1!=size)
				fprintf(hFile, "\t");
		}
	}
	fprintf(hFile, "};\n");

	fprintf(hFile, "#endif\n");

	fclose(hFile);

	return true;
}

bool miniFsWriteFlatFile(const char *name, uint16_t size, const char *destDir, uint8_t *dataArray, bool verbose) {
	char outPath[1024]; // TODO: better
	sprintf(outPath, "%s/%s", destDir, name);
	FILE *outFile=fopen(outPath, "w");

	// Open file
	if (outFile==NULL) {
		if (verbose)
			printf("could not open '%s' for writing\n", outPath);
		return false;
	}

	// Write data
	if (fwrite(dataArray, 1, size, outFile)!=size) {
		if (verbose)
			printf("could not write full data to '%s'\n", outPath);
		return false;
	}

	// Close file
	fclose(outFile);

	return true;
}

uint16_t readFunctor(uint16_t addr, uint8_t *data, uint16_t len, void *userData) {
	memcpy(data, ((uint8_t *)userData)+addr, len);
	return len;
}

uint16_t writeFunctor(uint16_t addr, const uint8_t *data, uint16_t len, void *userData) {
	memcpy(((uint8_t *)userData)+addr, data, len);
	return len;
}
