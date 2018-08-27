require ../sys/sys.s

requireend ../std/str/inttostr.s
requireend ../std/str/strcpy.s

db pinopenPrefix '/dev/pin',0
const pinopenPrefixLen 8
ab pinopenPathBuf PathMax

label pinopen ; num=r0, returns fd in r0 (or 0 on failure)
; Copy prefix into path buf
push8 r0
mov r0 pinopenPathBuf
mov r1 pinopenPrefix
call strcpy
pop8 r1
; Append given pin num to path buf
mov r0 pinopenPathBuf
mov r2 pinopenPrefixLen
add r0 r0 r2
call inttostr
; open file at calculated path
mov r0 SyscallIdOpen
mov r1 pinopenPathBuf
syscall
ret
