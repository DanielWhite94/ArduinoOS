require lib/sys/sys.s

requireend lib/std/proc/exit.s
requireend lib/std/proc/getabspath.s

ab argBuf PathMax
ab pathBuf PathMax
ab fd 1

; Register simple suicide handler
require lib/std/proc/suicidehandler.s

; Loop over args in turn
mov r0 1 ; 0 is program name
label loopstart
push8 r0
call catArgN ; this will exit for us if no such argument
pop8 r0
inc r0
jmp loopstart

; Exit
mov r0 0
call exit

label error
mov r0 1
call exit

label catArgN

; Get arg
mov r1 r0
mov r0 SyscallIdArgvN
mov r2 argBuf
mov r3 PathMax
syscall

; No arg found?
cmp r0 r0 r0
skipneqz r0
jmp error

; Convert to absolute path
mov r0 pathBuf
mov r1 argBuf
call getabspath

; Open file
mov r0 SyscallIdOpen
mov r1 pathBuf
syscall

mov r1 fd
store8 r1 r0

; Check for bad fd
cmp r1 r0 r0
skipneqz r1
jmp error

; Read data from file, printing to stdout
mov r2 0 ; loop index
label catArgNLoopStart

; Read block (reusing pathBuf)
mov r0 SyscallIdRead
mov r1 fd
load8 r1 r1
mov r3 pathBuf
mov r4 PathMax
syscall

; Check for EOF
cmp r4 r0 r0
skipneqz r4
jmp catArgNLoopEnd

; Print block
mov r4 r0
mov r0 SyscallIdEnvGetStdoutFd
syscall
mov r1 r0
mov r0 SyscallIdWrite
; we can leave r2 non-zero as offset is ignored for stdout
mov r3 pathBuf
syscall

cmp r0 r0 r0
skipneqz r0
jmp catArgNLoopEnd

; Advance to next block
add r2 r2 r4
jmp catArgNLoopStart
label catArgNLoopEnd

; Close file
mov r0 SyscallIdClose
mov r1 fd
load8 r1 r1
syscall

ret
