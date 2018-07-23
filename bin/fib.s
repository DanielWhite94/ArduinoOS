jmp start

include lib.s

label start
; Init sequence
mov r4 0 ; r4 is the lower of the two current values
mov r5 1 ; r5 is the higher

label loopstart
; Print lowest of two values (protecting r4 and r5)
push r4
push r5
mov r0 r4
call printDec
pop r5
pop r4

; Time to end?
mov r3 40000
cmp r3 r4 r3
skiplt r3 ; if r4<40000 is true this causes us to skip the next instruction and continue executing the loop
jmp loopend ; but if false then we end up here and break out of the loop

; Print comma
push r4
push r5
mov r0 ','
call printChar
mov r0 32 ; TODO: Use ' ' constant once supported
call printChar
pop r5
pop r4

; Perform single Fibonacci step
add r3 r4 r5 ; add two numbers and store into temp register
mov r4 r5 ; move higher into lower
mov r5 r3 ; move sum into higher

jmp loopstart

; Print newline to terminate list
label loopend
mov r0 '\n'
call printChar

; Exit
mov r0 0
mov r1 0
syscall
