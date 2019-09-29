#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bytecode.h"
#include "procman.h"

#define PathMax 64 // TODO: use KernelFsPathMax
#define InitPid 0
#define StdinFd 1
#define StdoutFd 1

typedef struct {
	int argc;
	char **argv;

	char pwd[PathMax];
	char path[PathMax];
	uint8_t stdinFd;
	uint8_t stdoutFd;
} ProcessEnvVars;

typedef struct {
	BytecodeWord regs[8];

	uint8_t memory[BytecodeMemoryTotalSize]; // combined progmem+ram

	unsigned skipCounter; // skip next n instructions?

	unsigned instructionCount;
	BytecodeWord pid;

	ProcessEnvVars envVars;
} Process;

Process *process=NULL;
bool infoSyscalls=false;
bool infoInstructions=false;
bool infoState=false;
bool slow=false;
bool passOnExitStatus=false;
int exitStatus=EXIT_SUCCESS;

bool processRunNextInstruction(Process *process);
void processDebug(const Process *process);

int main(int argc, char **argv) {
	FILE *inputFile=NULL;

	// Parse arguments
	if (argc<2) {
		printf("Usage: %s [--infosyscalls] [--infoinstructions] [--infostate] [--slow] [--passonexitstatus] inputfile [inputargs ...]\n", argv[0]);
		goto done;
	}

	int i;
	for(i=1; i<argc; ++i) {
		if (strncmp(argv[i], "--", 2)!=0)
			break;
		if (strcmp(argv[i], "--infosyscalls")==0)
			infoSyscalls=true;
		else if (strcmp(argv[i], "--infoinstructions")==0)
			infoInstructions=true;
		else if (strcmp(argv[i], "--infostate")==0)
			infoState=true;
		else if (strcmp(argv[i], "--passonexitstatus")==0)
			passOnExitStatus=true;
		else if (strcmp(argv[i], "--slow")==0)
			slow=true;
		else
			printf("Warning: unknown option '%s'\n", argv[i]);
	}

	const int inputArgBaseIndex=i;

	// Allocate process data struct
	process=malloc(sizeof(Process));
	if (process==NULL) {
		printf("Error: Could not allocate process data struct\n");
		goto done;
	}

	process->regs[BytecodeRegisterSP]=0;
	process->regs[BytecodeRegisterIP]=0;
	process->skipCounter=0;
	process->instructionCount=0;
	srand(time(NULL));
	process->pid=(rand()%(ProcManPidMax-1))+1; // +1 to avoid InitPid at 0

	process->envVars.argc=argc-inputArgBaseIndex;
	process->envVars.argv=argv+inputArgBaseIndex;
	strcpy(process->envVars.pwd, "/bin");
	strcpy(process->envVars.path, "/bin");
	process->envVars.stdinFd=StdinFd;
	process->envVars.stdoutFd=StdoutFd;

	// Read-in input file
	const char *inputPath=argv[inputArgBaseIndex];
	if (strcmp(inputPath, "-")!=0) {
		// Read standard file
		inputFile=fopen(inputPath, "r");
		if (inputFile==NULL) {
			printf("Error: Could not open input file '%s' for reading\n", inputPath);
			goto done;
		}
	} else {
		// Read from stdin instead
		inputFile=stdin;
	}

	int c;
	uint8_t *next=process->memory;
	c=fgetc(inputFile);
	if (c!=BytecodeMagicByte1) {
		printf("Error: first magic byte is not 0x%02X as expected\n", BytecodeMagicByte1);
		goto done;
	}
	*next++=c;
	c=fgetc(inputFile);
	if (c!=BytecodeMagicByte2) {
		printf("Error: second magic byte is not 0x%02X as expected\n", BytecodeMagicByte2);
		goto done;
	}
	*next++=c;
	while((c=fgetc(inputFile))!=EOF)
		*next++=c;

	// Run process
	do {
		if (slow)
			sleep(1);

		if (infoState)
			processDebug(process);
	} while(processRunNextInstruction(process));

	// Done
	done:
	if (inputFile!=NULL && strcmp(inputPath, "-")!=0)
		fclose(inputFile);
	free(process);

	return exitStatus;
}

bool processRunNextInstruction(Process *process) {
	BytecodeWord originalIP=process->regs[BytecodeRegisterIP];

	BytecodeInstruction3Byte instruction;
	instruction[0]=process->memory[process->regs[BytecodeRegisterIP]++];
	BytecodeInstructionLength length=bytecodeInstructionParseLength(instruction);
	if (length==BytecodeInstructionLength2Byte || length==BytecodeInstructionLength3Byte)
		instruction[1]=process->memory[process->regs[BytecodeRegisterIP]++];
	if (length==BytecodeInstructionLength3Byte)
		instruction[2]=process->memory[process->regs[BytecodeRegisterIP]++];

	BytecodeInstructionInfo info;
	bytecodeInstructionParse(&info, instruction);

	if (process->skipCounter>0) {
		if (infoInstructions)
			printf("Info: Skipping instruction\n");
		--process->skipCounter;
		return true;
	}

	switch(info.type) {
		case BytecodeInstructionTypeMemory:
			switch(info.d.memory.type) {
				case BytecodeInstructionMemoryTypeStore8:
					process->memory[process->regs[info.d.memory.destReg]]=process->regs[info.d.memory.srcReg];
					if (infoInstructions)
						printf("Info: *r%i=r%i (*%u=%u)\n", info.d.memory.destReg, info.d.memory.srcReg, process->regs[info.d.memory.destReg], process->regs[info.d.memory.srcReg]);
					if (process->regs[info.d.memory.destReg]<BytecodeMemoryProgmemSize) {
						printf("Error: store8 with address pointing into read-only region.\n");
						return false;
					}
				break;
				case BytecodeInstructionMemoryTypeLoad8: {
					BytecodeWord srcAddr=process->regs[info.d.memory.srcReg];
					process->regs[info.d.memory.destReg]=process->memory[srcAddr];
					if (infoInstructions)
						printf("Info: r%i=*r%i (=*%i=%i)\n", info.d.memory.destReg, info.d.memory.srcReg, srcAddr, process->regs[info.d.memory.destReg]);
				} break;
				case BytecodeInstructionMemoryTypeSet4: {
					process->regs[info.d.memory.destReg]=info.d.memory.set4Value;
					if (infoInstructions)
						printf("Info: r%i=%u\n", info.d.memory.destReg, info.d.memory.set4Value);
				} break;
			}
		break;
		case BytecodeInstructionTypeAlu: {
			int opA=process->regs[info.d.alu.opAReg];
			int opB=process->regs[info.d.alu.opBReg];
			switch(info.d.alu.type) {
				case BytecodeInstructionAluTypeInc: {
					int pre=process->regs[info.d.alu.destReg];
					process->regs[info.d.alu.destReg]+=info.d.alu.incDecValue;
					if (infoInstructions) {
						if (info.d.alu.incDecValue==1)
							printf("Info: r%i++ (r%i=%i+1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
						else
							printf("Info: r%i+=%i (r%i=%i+%i=%i)\n", info.d.alu.destReg, info.d.alu.incDecValue, info.d.alu.destReg, pre, info.d.alu.incDecValue, process->regs[info.d.alu.destReg]);
					}
				} break;
				case BytecodeInstructionAluTypeDec: {
					int pre=process->regs[info.d.alu.destReg];
					process->regs[info.d.alu.destReg]-=info.d.alu.incDecValue;
					if (infoInstructions) {
						if (info.d.alu.incDecValue==1)
							printf("Info: r%i-- (r%i=%i-1=%i)\n", info.d.alu.destReg, info.d.alu.destReg, pre, process->regs[info.d.alu.destReg]);
						else
							printf("Info: r%i-=%i (r%i=%i-%i=%i)\n", info.d.alu.destReg, info.d.alu.incDecValue, info.d.alu.destReg, pre, info.d.alu.incDecValue, process->regs[info.d.alu.destReg]);
					}
				} break;
				case BytecodeInstructionAluTypeAdd:
					process->regs[info.d.alu.destReg]=opA+opB;
					if (infoInstructions)
						printf("Info: r%i=r%i+r%i (=%i+%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeSub:
					process->regs[info.d.alu.destReg]=opA-opB;
					if (infoInstructions)
						printf("Info: r%i=r%i-r%i (=%i-%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeMul:
					process->regs[info.d.alu.destReg]=opA*opB;
					if (infoInstructions)
						printf("Info: r%i=r%i*r%i (=%i*%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeDiv:
					if (opB==0) {
						printf("Error: division by 0\n");
						return false;
					}
					process->regs[info.d.alu.destReg]=opA/opB;
					if (infoInstructions)
						printf("Info: r%i=r%i/r%i (=%i/%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeXor:
					process->regs[info.d.alu.destReg]=opA^opB;
					if (infoInstructions)
						printf("Info: r%i=r%i^r%i (=%i^%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeOr:
					process->regs[info.d.alu.destReg]=opA|opB;
					if (infoInstructions)
						printf("Info: r%i=r%i|r%i (=%i|%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeAnd:
					process->regs[info.d.alu.destReg]=opA&opB;
					if (infoInstructions)
						printf("Info: r%i=r%i&r%i (=%i&%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeCmp: {
					BytecodeWord *d=&process->regs[info.d.alu.destReg];
					*d=0;
					*d|=(opA==opB)<<BytecodeInstructionAluCmpBitEqual;
					*d|=(opA==0)<<BytecodeInstructionAluCmpBitEqualZero;
					*d|=(opA!=opB)<<BytecodeInstructionAluCmpBitNotEqual;
					*d|=(opA!=0)<<BytecodeInstructionAluCmpBitNotEqualZero;
					*d|=(opA<opB)<<BytecodeInstructionAluCmpBitLessThan;
					*d|=(opA<=opB)<<BytecodeInstructionAluCmpBitLessEqual;
					*d|=(opA>opB)<<BytecodeInstructionAluCmpBitGreaterThan;
					*d|=(opA>=opB)<<BytecodeInstructionAluCmpBitGreaterEqual;

					if (infoInstructions)
						printf("Info: r%i=cmp(r%i,r%i) (=cmp(%i,%i)=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				} break;
				case BytecodeInstructionAluTypeShiftLeft:
					process->regs[info.d.alu.destReg]=opA<<opB;
					if (infoInstructions)
						printf("Info: r%i=r%i<<r%i (=%i<<%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeShiftRight:
					process->regs[info.d.alu.destReg]=opA>>opB;
					if (infoInstructions)
						printf("Info: r%i=r%i>>r%i (=%i>>%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opBReg, opA, opB, process->regs[info.d.alu.destReg]);
				break;
				case BytecodeInstructionAluTypeSkip: {
					unsigned skipDist=(info.d.alu.opBReg+1);
					process->skipCounter=((process->regs[info.d.alu.destReg] & (1u<<info.d.alu.opAReg)) ? skipDist : 0);

					if (infoInstructions)
						printf("Info: skip%u r%i (=%i, %s, skip=%i)\n", info.d.alu.opAReg, info.d.alu.destReg, process->regs[info.d.alu.destReg], byteCodeInstructionAluCmpBitStrings[info.d.alu.opAReg], process->skipCounter);
				} break;
				case BytecodeInstructionAluTypeExtra: {
					switch(info.d.alu.opBReg) {
						case BytecodeInstructionAluExtraTypeNot:
							process->regs[info.d.alu.destReg]=~opA;
							if (infoInstructions)
								printf("Info: r%i=~r%i (=~%i=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, opA, process->regs[info.d.alu.destReg]);
						break;
						case BytecodeInstructionAluExtraTypeStore16:
							process->memory[process->regs[info.d.alu.destReg]]=(process->regs[info.d.alu.opAReg]>>8);
							process->memory[process->regs[info.d.alu.destReg]+1]=(process->regs[info.d.alu.opAReg]&0xFF);
							if (infoInstructions)
								printf("Info: [r%i]=r%i (16 bit) ([%i]=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, process->regs[info.d.alu.destReg], opA);
							if (process->regs[info.d.alu.destReg]<BytecodeMemoryProgmemSize) {
								printf("Error: store16 with address pointing into read-only region.\n");
								return false;
							}
						break;
						case BytecodeInstructionAluExtraTypeLoad16:
							process->regs[info.d.alu.destReg]=(((BytecodeWord)process->memory[process->regs[info.d.alu.opAReg]])<<8) |
							                                   process->memory[process->regs[info.d.alu.opAReg]+1];
							if (infoInstructions)
								printf("Info: r%i=[r%i] (16 bit) (=[%i]=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, opA, process->regs[info.d.alu.destReg]);
						break;
						case BytecodeInstructionAluExtraTypePush16:
							process->memory[process->regs[info.d.alu.destReg]]=(opA>>8);
							++process->regs[info.d.alu.destReg];
							process->memory[process->regs[info.d.alu.destReg]]=(opA&0xFF);
							++process->regs[info.d.alu.destReg];
							if (infoInstructions)
								printf("Info: [r%i]=r%i, r%i+=2 (16 bit push) ([%i]=%i)\n", info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.destReg, process->regs[info.d.alu.destReg]-2, opA);
						break;
						case BytecodeInstructionAluExtraTypePop16:
							--process->regs[info.d.alu.opAReg];
							process->regs[info.d.alu.destReg]=process->memory[process->regs[info.d.alu.opAReg]];
							--process->regs[info.d.alu.opAReg];
							process->regs[info.d.alu.destReg]|=(((BytecodeWord)process->memory[process->regs[info.d.alu.opAReg]])<<8);
							if (infoInstructions)
								printf("Info: r%i-=2, r%i=[r%i] (16 bit pop) ([%i]=%i)\n", info.d.alu.opAReg, info.d.alu.destReg, info.d.alu.opAReg, process->regs[info.d.alu.opAReg], process->regs[info.d.alu.destReg]);
						break;
						case BytecodeInstructionAluExtraTypeCall: {
							BytecodeWord preIPReg=process->regs[BytecodeRegisterIP];

							// Push return address
							process->memory[process->regs[info.d.alu.opAReg]]=(preIPReg>>8);
							++process->regs[info.d.alu.opAReg];
							process->memory[process->regs[info.d.alu.opAReg]]=(preIPReg&0xFF);
							++process->regs[info.d.alu.opAReg];

							// Jump to call address
							process->regs[BytecodeRegisterIP]=process->regs[info.d.alu.destReg];

							// Logging
							if (infoInstructions)
								printf("Info (%u): call r%i r%i ([r%u=%u]=r%u=%u, r%u+=2, r%u=r%u=%u)\n", originalIP, info.d.alu.destReg, info.d.alu.opAReg, info.d.alu.opAReg, process->regs[info.d.alu.opAReg]-2, BytecodeRegisterIP, preIPReg, info.d.alu.opAReg, BytecodeRegisterIP, info.d.alu.destReg, process->regs[BytecodeRegisterIP]);
						} break;
						case BytecodeInstructionAluExtraTypeXchg8: {
							BytecodeWord addr=process->regs[info.d.alu.destReg];
							BytecodeRegister srcDestReg=info.d.alu.opAReg;
							uint8_t memValue=process->memory[addr];
							process->memory[addr]=(process->regs[srcDestReg] & 0xFF);
							process->regs[srcDestReg]=memValue;
							if (infoInstructions)
								printf("Info: xchg *r%i r%i\n", info.d.alu.destReg, srcDestReg);
						} break;
						default:
							printf("Error: Unknown alu extra instruction with type %i\n", info.d.alu.opBReg);
							return false;
						break;
					}
				}
			}
		} break;
		case BytecodeInstructionTypeMisc:
			switch(info.d.misc.type) {
				case BytecodeInstructionMiscTypeNop:
					if (infoInstructions)
						printf("Info: nop\n");
				break;
				case BytecodeInstructionMiscTypeSyscall: {
					uint16_t syscallId=process->regs[0];
					if (infoInstructions)
						printf("Info: syscall id=%i\n", syscallId);
					switch(syscallId) {
						case BytecodeSyscallIdExit:
							if (passOnExitStatus)
								exitStatus=process->regs[1];
							if (infoSyscalls)
								printf("Info: syscall(id=%i [exit], status=%u)\n", syscallId, process->regs[1]);
							return false;
						break;
						case BytecodeSyscallIdGetPid: {
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpid], return=%u)\n", syscallId, process->pid);

							process->regs[0]=process->pid;
						} break;
						case BytecodeSyscallIdGetArgC: {
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getargc], return=%u)\n", syscallId, process->envVars.argc);

							process->regs[0]=process->envVars.argc;
						} break;
						case BytecodeSyscallIdGetArgVN: {
							int n=process->regs[1];
							int buf=process->regs[2];
							int len=process->regs[3];

							if (n>=process->envVars.argc) {
								process->regs[0]=0;

								if (infoSyscalls)
									printf("Info: syscall(id=%i [getargvn], n=%i, buf=%i, len=%i, return=0 as n>=argc)\n", syscallId, n, buf, len);
							} else {
								const char *str=process->envVars.argv[n];
								int trueLen=strlen(str);

								if (infoSyscalls)
									printf("Info: syscall(id=%i [getargvn], n=%i, buf=%i, len=%i, return=%u with '%s')\n", syscallId, n, buf, len, trueLen, str);

								for(int i=0; i<len && i<trueLen; ++i)
									process->memory[buf+i]=str[i];
								process->regs[0]=trueLen;
							}
						} break;
						case BytecodeSyscallIdFork:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [fork] (unimplemented)\n", syscallId);

							// This is not implemented - simply return error
							process->regs[0]=ProcManPidMax;
						break;
						case BytecodeSyscallIdExec:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [exec] (unimplemented)\n", syscallId);

							// This is not implemented - simply return false
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdWaitPid: {
							ProcManPid waitPid=process->regs[1];
							BytecodeWord timeout=process->regs[2];

							if (infoSyscalls)
								printf("Info: syscall(id=%i [waitpid], pid=%u, timeout=%u\n", syscallId, waitPid, timeout);
							if (waitPid==InitPid || waitPid==process->pid) {
								if (timeout==0) {
									printf("Warning: Entered infinite waitpid syscall (waiting for own or init's PID with infinite timeout), exiting\n");
									return false;
								}
								sleep(timeout);
								process->regs[0]=ProcManExitStatusTimeout;
							} else {
								process->regs[0]=ProcManExitStatusNoProcess;
							}
						} break;
						case BytecodeSyscallIdGetPidPath:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpidpath] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdGetPidState:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpidstate] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdGetAllCpuCounts:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getallcpucounts] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdKill: {
							ProcManPid pid=process->regs[1];
							if (infoSyscalls)
								printf("Info: syscall(id=%i [kill], pid=%u\n", syscallId, pid);
							if (pid==process->pid) {
								printf("Killed by own kill syscall\n");
								return false;
							}
						} break;
						case BytecodeSyscallIdGetPidRam:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpidram] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdSignal:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [signal] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdGetPidFdN:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpidfdn] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdExec2:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [exec2] (unimplemented)\n", syscallId);

							// This is not implemented - simply return false
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdRead:
							if (process->regs[1]==process->envVars.stdinFd) {
								ssize_t result=read(STDIN_FILENO, &process->memory[process->regs[3]], process->regs[4]);
								if (result>=0)
									process->regs[0]=result;
								else
									process->regs[0]=0;
							} else
								process->regs[0]=0;

							if (infoSyscalls) {
								printf("Info: syscall(id=%i [read], fd=%u, data=%u [", syscallId, process->regs[1], process->regs[3]);
								for(int i=0; i<process->regs[0]; ++i)
									printf("%c", process->memory[process->regs[3]+i]);
								printf("], len=%u, read=%u)\n", process->regs[4], process->regs[0]);
							}
						break;
						case BytecodeSyscallIdWrite: {
							uint8_t fd=process->regs[1];
							// uint16_t offset=process->regs[2]; // offset is ignored as we currently only allow writing to stdout
							uint16_t bufAddr=process->regs[3];
							uint16_t bufLen=process->regs[4];

							if (fd==process->envVars.stdoutFd) {
								for(int i=0; i<bufLen; ++i)
									printf("%c", process->memory[bufAddr+i]);
								fflush(stdout);
								process->regs[0]=bufLen;
							} else
								process->regs[0]=0;

							if (infoSyscalls) {
								printf("Info: syscall(id=%i [write], fd=%u, data addr=%u [", syscallId, fd, bufAddr);
								for(int i=0; i<bufLen; ++i)
									printf("%c", process->memory[bufAddr+i]);
								printf("], len=%u, written=%u)\n", bufLen, process->regs[0]);
							}
						} break;
						case BytecodeSyscallIdOpen:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [open] (unimplemented)\n", syscallId);

							// This is not implemented - simply return invalid fd
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdClose:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [close] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdDirGetChildN:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [dirgetchildn] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdGetPath: {
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getpath] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						} break;
						case BytecodeSyscallIdResizeFile:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [resizefile] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdFileGetLen:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [filegetlen] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdTryReadByte:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [tryreadbyte] (unimplemented)\n", syscallId);
							process->regs[0]=256;
						break;
						case BytecodeSyscallIdIsDir:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [isdir] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdFileExists:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [fileexists] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdDelete:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [delete] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdEnvGetStdinFd:
							process->regs[0]=process->envVars.stdinFd;
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envgetstinfd] (return fd = %u)\n", syscallId, process->regs[0]);
						break;
						case BytecodeSyscallIdEnvSetStdinFd:
							process->envVars.stdinFd=process->regs[0];
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetstdinfd], new fd %u\n", syscallId, process->envVars.stdinFd);
						break;
						case BytecodeSyscallIdEnvGetPwd:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpwd] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdEnvSetPwd:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpwd] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdEnvGetPath:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpath] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdEnvSetPath:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetpath] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdEnvGetStdoutFd:
							process->regs[0]=process->envVars.stdoutFd;
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envgetstoutfd] (return fd = %u)\n", syscallId, process->regs[0]);
						break;
						case BytecodeSyscallIdEnvSetStdoutFd:
							process->envVars.stdoutFd=process->regs[0];
							if (infoSyscalls)
								printf("Info: syscall(id=%i [envsetstdoutfd], new fd %u\n", syscallId, process->envVars.stdoutFd);
						break;
						case BytecodeSyscallIdTimeMonotonic:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [timemonotonic] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdRegisterSignalHandler:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [registersignalhandler] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdShutdown:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [shutdown] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdMount:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [mount] (unimplemented)\n", syscallId);
							process->regs[0]=0;
						break;
						case BytecodeSyscallIdUnmount:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [unmount] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdIoctl:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [ioctl] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdGetLogLevel:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [getloglevel] (unimplemented)\n", syscallId);
							process->regs[0]=3; // i.e. none
						break;
						case BytecodeSyscallIdSetLogLevel:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [setloglevel] (unimplemented)\n", syscallId);
						break;
						case BytecodeSyscallIdStrchr: {
							// TODO: Check arguments better
							uint16_t strAddr=process->regs[1];
							uint16_t c=process->regs[2];

							process->regs[0]=0;
							uint16_t i;
							for(i=strAddr; process->memory[i]!=0; ++i) {
								if (process->memory[i]==c) {
									process->regs[0]=i;
									break;
								}
							}

							if (infoSyscalls)
								printf("Info: syscall(id=%i [strchr], str addr=%u, c=%u, result=%u\n", syscallId, strAddr, c, process->regs[0]);
						} break;
						case BytecodeSyscallIdStrchrnul: {
							// TODO: Check arguments better
							uint16_t strAddr=process->regs[1];
							uint16_t c=process->regs[2];

							uint16_t i;
							for(i=strAddr; process->memory[i]!=0; ++i) {
								if (process->memory[i]==c)
									break;
							}
							process->regs[0]=i;

							if (infoSyscalls)
								printf("Info: syscall(id=%i [strchrnul], str addr=%u, c=%u, result=%u\n", syscallId, strAddr, c, process->regs[0]);
						} break;
						case BytecodeSyscallIdMemmove: {
							// TODO: Check arguments better
							uint16_t destAddr=process->regs[1];
							uint16_t srcAddr=process->regs[2];
							uint16_t size=process->regs[3];

							memmove(process->memory+destAddr, process->memory+srcAddr, size);

							if (infoSyscalls)
								printf("Info: syscall(id=%i [memmove], dest addr=%u, src addr=%u, size=%u\n", syscallId, destAddr, srcAddr, size);
						} break;
						case ByteCodeSyscallIdMemcmp: {
							// TODO: Check arguments better
							uint16_t p1Addr=process->regs[1];
							uint16_t p2Addr=process->regs[2];
							uint16_t size=process->regs[3];

							process->regs[0]=memcmp(process->memory+p1Addr, process->memory+p2Addr, size);

							if (infoSyscalls)
								printf("Info: syscall(id=%i [memcmp], p1 addr=%u, p2 addr=%u, size=%u, ret %i\n", syscallId, p1Addr, p2Addr, size, process->regs[0]);
						} break;
						case ByteCodeSyscallIdStrrchr: {
							// TODO: Check arguments better
							uint16_t strAddr=process->regs[1];
							uint16_t c=process->regs[2];

							process->regs[0]=0;
							uint16_t i;
							for(i=strAddr; process->memory[i]!=0; ++i)
								if (process->memory[i]==c)
									process->regs[0]=i;

							if (infoSyscalls)
								printf("Info: syscall(id=%i [strrchr], str addr=%u, c=%u, result=%u\n", syscallId, strAddr, c, process->regs[0]);
						} break;
						case ByteCodeSyscallIdStrcmp: {
							// TODO: Check arguments better
							uint16_t p1Addr=process->regs[1];
							uint16_t p2Addr=process->regs[2];

							process->regs[0]=strcmp((const char *)(process->memory+p1Addr), (const char *)(process->memory+p2Addr));

							if (infoSyscalls)
								printf("Info: syscall(id=%i [strcmp], p1 addr=%u, p2 addr=%u, ret %i\n", syscallId, p1Addr, p2Addr, process->regs[0]);
						} break;
						case BytecodeSyscallIdHwDeviceRegister: {
							// Always fail
							uint16_t id=process->regs[1];
							uint16_t type=process->regs[2];

							process->regs[0]=0;

							if (infoSyscalls)
								printf("Info: syscall(id=%i [hwdeviceregister], id=%u, type=%u\n", syscallId, id, type);
						} break;
						case BytecodeSyscallIdHwDeviceDeregister: {
							// Nothing to do - cannot register such devices in the first place
							uint16_t id=process->regs[1];

							if (infoSyscalls)
								printf("Info: syscall(id=%i [hwdevicederegister], id=%u\n", syscallId, id);
						} break;
						case BytecodeSyscallIdHwDeviceGetType: {
							// Must be unused - cannot register such devices in the first place
							uint16_t id=process->regs[1];
							process->regs[0]=0; // TODO: fix magic number 0 with typeunused constant from somewhere

							if (infoSyscalls)
								printf("Info: syscall(id=%i [hwdevicegettype], id=%u\n", syscallId, id);
						} break;
						case BytecodeSyscallIdHwDeviceSdCardReaderMount: {
							// SPI devices are unsupported
							uint16_t id=process->regs[1];
							process->regs[0]=0;

							if (infoSyscalls)
								printf("Info: syscall(id=%i [hwdevicesdcardreadermount], id=%u\n", syscallId, id);
						} break;
						case BytecodeSyscallIdHwDeviceSdCardReaderUnmount: {
							// SPI devices are unsupported
							uint16_t id=process->regs[1];

							if (infoSyscalls)
								printf("Info: syscall(id=%i [hwdevicesdcardreaderunmount], id=%u\n", syscallId, id);
						} break;
						case BytecodeSyscallIdHwDeviceDht22GetTemperature: {
							// SPI devices are unsupported
							uint16_t id=process->regs[1];

							process->regs[0]=0;

							if (infoSyscalls)
								printf("Info: syscall(id=%i [hwdevicedht22gettemperature], id=%u\n", syscallId, id);
						} break;
						case BytecodeSyscallIdHwDeviceDht22GetHumidity: {
							// SPI devices are unsupported
							uint16_t id=process->regs[1];

							process->regs[0]=0;

							if (infoSyscalls)
								printf("Info: syscall(id=%i [hwdevicedht22gethumidity], id=%u\n", syscallId, id);
						} break;
						default:
							if (infoSyscalls)
								printf("Info: syscall(id=%i [unknown])\n", syscallId);
							printf("Error: Unknown syscall with id %i\n", syscallId);
							return false;
						break;
					}
				} break;
				case BytecodeInstructionMiscTypeClearInstructionCache:
					// We do not use an instruction cache so nothing to do
				break;
				case BytecodeInstructionMiscTypeIllegal:
					if (infoInstructions)
						printf("Info: illegal\n");
					printf("Error: illegal instruction encountered\n");
					return false;
				break;
				case BytecodeInstructionMiscTypeDebug:
					if (infoInstructions)
						printf("Info: debug\n");
					processDebug(process);
				break;
				case BytecodeInstructionMiscTypeSet8:
					process->regs[info.d.misc.d.set8.destReg]=info.d.misc.d.set8.value;
					if (infoInstructions)
						printf("Info: r%i=%u\n", info.d.misc.d.set8.destReg, info.d.misc.d.set8.value);
				break;
				case BytecodeInstructionMiscTypeSet16:
					process->regs[info.d.misc.d.set16.destReg]=info.d.misc.d.set16.value;
					if (infoInstructions)
						printf("Info: r%i=%u\n", info.d.misc.d.set16.destReg, info.d.misc.d.set16.value);
				break;
			}
		break;
	}

	++process->instructionCount;

	return true;
}

void processDebug(const Process *process) {
	printf("Info:\n");
	printf("	PID: %u\n", process->pid);
	printf("	IP: %u\n", process->regs[BytecodeRegisterIP]);
	printf("	SP: %u\n", process->regs[BytecodeRegisterSP]);
	printf("	Instruction count: %u\n", process->instructionCount);
	printf("	Regs:");
	for(int i=0; i<8; ++i)
		printf(" r%i=%u", i, process->regs[i]);
	printf("\n");
}
