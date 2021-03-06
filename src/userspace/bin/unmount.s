require lib/sys/sys.s

requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/getpath.s

db usageErrorStr 'usage: unmount dir\n',0

ab dirPath PathMax

; Read dir argument
mov r0 SyscallIdArgvN
mov r1 1
syscall

cmp r1 r0 r0
skipneqz r1
jmp usageerror

mov r1 r0
mov r0 dirPath
call getpath

; Invoke unmount syscall
mov r0 SyscallIdUnmount
mov r1 dirPath
syscall

; Exit
mov r0 0
call exit

; Usage error
label usageerror
mov r0 usageErrorStr
call puts0
mov r0 1
call exit
