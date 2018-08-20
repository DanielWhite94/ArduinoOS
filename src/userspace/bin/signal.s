requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/str/strtoint.s

db usageStr 'usage: signal pid signalid',0

ab pidArgBuf 64
ab signalIdArgBuf 64

; Grab pid from first argument
mov r0 3
mov r1 1
mov r2 pidArgBuf
mov r3 64
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage
; Grab signalid from second argument
mov r0 3
mov r1 2
mov r2 signalIdArgBuf
mov r3 64
syscall
cmp r1 r0 r0
skipneqz r1
jmp usage

; Convert to pid and signalId to integers and invoke signal syscall
mov r0 pidArgBuf
call strtoint
push r0
mov r0 signalIdArgBuf
call strtoint
mov r2 r0
pop r1
mov r0 12
syscall

; Exit
mov r0 0
call exit

label usage
mov r0 usageStr
call puts0
mov r0 1
call exit