require ../../sys/syscall.s

; memcp(p1Addr=r0, p2Addr=r1, size=r2)
label memcp
; simply use syscall
mov r3 r2
mov r2 r1
mov r1 r0
mov r0 SyscallIdMemCmp
syscall
ret
