require pindef.s

label pinsetmode ; pinfd=r0, mode=r1
; call ioctl
mov r3 r1
mov r1 r0
mov r0 1283
mov r2 1
syscall
ret
