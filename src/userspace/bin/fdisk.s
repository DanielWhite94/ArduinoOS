require lib/sys/sys.s

requireend lib/std/io/fclose.s
requireend lib/std/io/fget.s
requireend lib/std/io/fopen.s
requireend lib/std/io/fput.s
requireend lib/std/io/fputdec.s
requireend lib/std/io/fputhex.s
requireend lib/std/io/fread.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s
requireend lib/std/int32/int32endianness.s
requireend lib/std/int32/int32fput.s

db usageStr 'usage: fdisk path\n', 0
db errorOpenStr 'Could not open disk\n', 0
db errorMagicBytesStr 'Bad magic bytes\n', 0

db partitionStr 'Partition ', 0
db partitionReadErrorStr '	could not read entry',0
db partitionAttributesStr '	Attributes: 0x',0
db partitionTypeStr '	Type: 0x',0
db partitionSectorsStr '	Num Sectors: ',0
db partitionStartSectorStr '	Start Sector: ',0

ab scratchBuf PathMax ; used to grab path argument, and also to hold partition table entries (hence size of MAX(16,PathMax))
ab pathBuf PathMax
ab fd 1

const MagicByte0 85 ; 0x55
const MagicByte1 170 ; 0xAA

; Set fd to invalid before we begin to simply error logic
mov r0 fd
mov r1 FdInvalid
store8 r0 r1

; Grab argument
mov r0 SyscallIdArgvN
mov r1 1
mov r2 scratchBuf
mov r3 PathMax
syscall
cmp r0 r0 r0
skipneqz r0
jmp showUsage

; Ensure path is absolute
mov r0 pathBuf
mov r1 scratchBuf
call getabspath

; Open disk
mov r0 pathBuf
call fopen
mov r1 FdInvalid
cmp r1 r0 r1
skipneq r1
jmp errorOpen

; Update fd
mov r1 fd
store8 r1 r0

; Read and verify magic bytes
mov r0 fd
load8 r0 r0
mov r1 510
call fgetc
mov r1 MagicByte0
cmp r1 r0 r1
skipeq r1
jmp errorMagicBytes

mov r0 fd
load8 r0 r0
mov r1 511
call fgetc
mov r1 MagicByte1
cmp r1 r0 r1
skipeq r1
jmp errorMagicBytes

; Loop over 4 standard partition table entries
mov r0 0
label partitionEntryLoopStart
; Print info for this entry
push8 r0
call fdiskPrintEntry
pop8 r0
; Loop again or are we done?
inc r0
mov r1 4
cmp r1 r0 r1
skipeq r1
jmp partitionEntryLoopStart

; Close disk (if needed)
label done
mov r0 fd
load8 r0 r0
call fclose ; note: fclose accepts FdInvalid (doing nothing)

; Exit
mov r0 0
call exit

; Errors
label showUsage
mov r0 usageStr
call puts0
jmp done

label errorOpen
mov r0 errorOpenStr
call puts0
jmp done

label errorMagicBytes
mov r0 errorMagicBytesStr
call puts0
jmp done

; Parse and print partition table entry info
; partition 'index' (0 based) in r0
label fdiskPrintEntry
; Protect index for now
push8 r0

; Print header string and id
mov r0 partitionStr
call puts0
pop8 r0
push8 r0
inc r0 ; usually 1-indexed not 0
call putdec
mov r0 ':'
call putc0
mov r0 '\n'
call putc0

; Read entry into scratch buffer
mov r0 fd
load8 r0 r0
pop8 r1 ; restore index and calculate entry offset
mov r2 16
mul r1 r1 r2
mov r2 446
add r1 r1 r2
mov r2 scratchBuf
mov r3 16 ; each entry is 16 bytes
call fread
mov r1 16
cmp r0 r0 r1
skipeq r0
jmp fdiskPrintEntryReadError

; Print attributes (byte 0)
mov r0 partitionAttributesStr
call puts0
mov r0 scratchBuf
load8 r0 r0
call puthex8
mov r0 '\n'
call putc0

; Print type (byte 4)
mov r0 partitionTypeStr
call puts0
mov r0 scratchBuf
inc4 r0
load8 r0 r0
call puthex8
mov r0 '\n'
call putc0

; Print sectors (bytes 12-15, little endian)
mov r0 partitionSectorsStr
call puts0
mov r0 scratchBuf
inc12 r0
call int32SwapEndianness
mov r0 scratchBuf
inc12 r0
call int32put0
mov r0 '\n'
call putc0

; Print start sector (bytes 8-11, little endian)
mov r0 partitionStartSectorStr
call puts0
mov r0 scratchBuf
inc8 r0
call int32SwapEndianness
mov r0 scratchBuf
inc8 r0
call int32put0
mov r0 '\n'
call putc0

; Partition table read error
label fdiskPrintEntryReadError
mov r0 partitionReadErrorStr
call puts0
ret