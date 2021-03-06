require lib/sys/sys.s

requireend lib/curses/curses.s
requireend lib/std/io/fput.s
requireend lib/std/proc/exit.s
requireend lib/std/proc/openpath.s

db infoStr '\n\nwasd move/push, r redraw, q quit\n',27,'[30;1m#'27,'[0m wall, '27,'[33;1m@',27,'[0m player, '27,'[33;1m+',27,'[0m player-on-goal, ',27,'[38;5;130m$',27,'[0m box, ',27,'[32;1m*',27,'[0m box on goal, '27,'[33;1m.',27,'[0m goal\n\n', 0
db errorStrNoArg 'usage: sokoban LEVELPATH\n', 0
db errorStrBadLevel 'error: could not load given level\n', 0

db cellColourStrYellow 27,'[33;1m',0
db cellColourStrGrey 27,'[30;1m',0
db cellColourStrBrown 27,'[38;5;130m',0
db cellColourStrGreen 27,'[32;1m',0

const maxW 16
const maxH 16
ab levelArray 256 ; maxW*maxH

ab playerX 1
ab playerY 1

ab scratchByte 1

; Grab level path from arg
mov r0 SyscallIdArgvN
mov r1 1
syscall

cmp r1 r0 r0
skipneqz r1
jmp errorNoArg

; Read in level
call levelRead

cmp r0 r0 r0
skipneqz r0
jmp errorBadLevel

; Turn off echoing of input
mov r0 0
call cursesSetEcho

; Clear screen, show info and draw level
label redraw
call cursesReset
call cursesCursorHide
mov r0 infoStr
call puts0
call levelDraw

; Input loop
label inputLoop
; set cursor to common position
mov r0 0
mov r1 0 ; TODO: Think about this
call cursesSetPosXY

; wait for key press
call cursesGetChar
mov r1 256
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; Parse key
mov r1 'r'
cmp r1 r0 r1
skipneq r1
jmp redraw
mov r1 'q'
cmp r1 r0 r1
skipneq r1
jmp done
mov r1 'a'
cmp r1 r0 r1
skipneq r1
jmp moveLeft
mov r1 'd'
cmp r1 r0 r1
skipneq r1
jmp moveRight
mov r1 'w'
cmp r1 r0 r1
skipneq r1
jmp moveUp
mov r1 's'
cmp r1 r0 r1
skipneq r1
jmp moveDown

jmp inputLoop

; Exit
label done
call cursesReset
mov r0 1
call cursesSetEcho
mov r0 0
call exit

; Errors
label errorNoArg
mov r0 errorStrNoArg
call puts0
mov r0 1
call exit

label errorBadLevel
mov r0 errorStrBadLevel
call puts0
mov r0 1
call exit

; Read a level - takes path in r0, returns true/false in r0
label levelRead
; Open level file
mov r1 FdModeRO
call openpath
cmp r1 r0 r0
skipneqz r1
jmp levelReadFail

; load level into array (fd is in r0)
mov r2 0 ; read offset
mov r4 0 ; row (y)
mov r5 0 ; column (x)
label levelReadLoopStart
; read a byte
push8 r0
push8 r4
mov r1 r0
mov r0 SyscallIdRead
mov r3 scratchByte
mov r4 1
syscall
inc r2
mov r1 r0
pop8 r4
pop8 r0

; EOF?
cmp r1 r1 r1
skipneqz r1
jmp levelReadLoopEnd

; If player or player-on-goal, set playerX and Y
mov r1 scratchByte
load8 r1 r1
mov r3 '+'
cmp r3 r1 r3
skipneq r3
jmp levelReadLoopPlayerGoal
mov r3 '@'
cmp r3 r1 r3
skipneq r3
jmp levelReadLoopPlayer
jmp levelReadLoopNotPlayer

label levelReadLoopPlayerGoal
mov r1 scratchByte
mov r3 '.'
store8 r1 r3
mov r3 playerX
store8 r3 r5
mov r3 playerY
store8 r3 r4
jmp levelReadLoopNotPlayer

label levelReadLoopPlayer
mov r1 scratchByte
mov r3 ' '
store8 r1 r3
mov r3 playerX
store8 r3 r5
mov r3 playerY
store8 r3 r4
jmp levelReadLoopNotPlayer

label levelReadLoopNotPlayer

; store character in current row
mov r1 levelArray
add r1 r1 r5
mov r3 maxH
mul r3 r3 r4
add r1 r1 r3
mov r3 scratchByte
load8 r3 r3
store8 r1 r3

; newline?
mov r1 '\n'
cmp r1 r3 r1
skipeq r1
jmp levelReadLoopNotNewline
inc r4 ; advance current row
mov r5 0 ; reset x
jmp levelReadLoopStart
label levelReadLoopNotNewline

; advance to next cell in x direction and loop
inc r5
jmp levelReadLoopStart
label levelReadLoopEnd

; Close file
mov r1 r0
mov r0 SyscallIdClose
syscall

; Success
mov r0 1
ret

label levelReadFail
mov r0 0
ret

; Draw a level
label levelDraw

; loop over all cells
mov r4 0 ; y
label levelDrawYStart
mov r3 0 ; x
label levelDrawXStart

; load at data at (x,y)
push8 r3
push8 r4
mov r0 r3
mov r1 r4
call levelLoadCellAdjusted
pop8 r4
pop8 r3

; print this character
push8 r0
push8 r3
push8 r4
call levelDrawUpdateCellRawPrint
pop8 r4
pop8 r3
pop8 r0

; end of row?
mov r1 '\n'
cmp r1 r0 r1
skipeq r1
jmp levelDrawXContinue
; end of level? (0 length row)
cmp r1 r3 r3
skipneqz r1
jmp levelDrawYEnd ; end of level
; end of row - inc y
inc r4
jmp levelDrawYStart

; advance to next cell
label levelDrawXContinue
inc r3
jmp levelDrawXStart

; end of a line
inc r4
jmp levelDrawYStart
label levelDrawYEnd

ret

; levelLoadCell(x=r0, y=r1) - returns cell value in r0
label levelLoadCell
mov r2 levelArray ; base offset
add r0 r0 r2 ; add x
mov r2 maxH ; add y*maxH
mul r2 r2 r1
add r0 r0 r2
load8 r0 r0 ; load cell
ret

; levelLoadCellAdjusted(x=r0, y=r1) - returns cell value in r0, adjusted to put the player on if needed
label levelLoadCellAdjusted
; load raw cell value
push8 r0
push8 r1
call levelLoadCell
mov r2 r0
pop8 r1
pop8 r0
; check if this is the players cell also
mov r3 playerX
load8 r3 r3
cmp r3 r0 r3
skipeq r3
jmp levelLoadCellAdjustedRet
mov r3 playerY
load8 r3 r3
cmp r3 r1 r3
skipeq r3
jmp levelLoadCellAdjustedRet
; this is the players cell - update r2
mov r0 ' '
cmp r0 r0 r2
skipeq r0
jmp levelLoadCellAdjustedPlayerOnGoal
mov r2 '@'
jmp levelLoadCellAdjustedRet
label levelLoadCellAdjustedPlayerOnGoal
mov r2 '+'
label levelLoadCellAdjustedRet
mov r0 r2
ret

; levelSetCell(x=r0, y=r1, c=r2)
label levelSetCell
mov r3 levelArray ; base offset
add r0 r0 r3 ; add x
mov r3 maxH ; add y*maxH
mul r3 r3 r1
add r0 r0 r3
store8 r0 r2 ; update cell
ret

; levelDrawUpdateCell(x=r0, y=r1) - takes coordinates and updates that cell on screen to match value in array
label levelDrawUpdateCell
; move cursor to correct position
push8 r0
push8 r1
inc5 r1 ; due to printing infoStr
call cursesSetPosXY
pop8 r1
pop8 r0
; load character
call levelLoadCellAdjusted
mov r2 r0
; write character onto screen
call levelDrawUpdateCellRawPrint
ret

; levelDrawUpdateCellRawPrint(cell=r0) - takes cell and prints
label levelDrawUpdateCellRawPrint
mov r2 r0
mov r0 cellColourStrGrey
mov r1 '+'
cmp r1 r2 r1
skiplt r1
mov r0 cellColourStrYellow
mov r1 '*'
cmp r1 r1 r2
skipneq r1
mov r0 cellColourStrGreen
mov r1 '$'
cmp r1 r1 r2
skipneq r1
mov r0 cellColourStrBrown
push8 r2
call puts0
pop8 r0
call putc0
ret

label moveLeft
; load cell at (px-1,py)
mov r0 playerX
load8 r0 r0
dec r0
mov r1 playerY
load8 r1 r1
call levelLoadCell
; if wall cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; if floor or goal (without box) then we can move here
mov r1 ' '
cmp r1 r0 r1
skipneq r1
jmp moveLeftMovePlayer
mov r1 '.'
cmp r1 r0 r1
skipneq r1
jmp moveLeftMovePlayer
; otherwise we have a box or box on a goal
; need to check the next cell again
mov r0 playerX
load8 r0 r0
dec2 r0
mov r1 playerY
load8 r1 r1
call levelLoadCell
; if next cell again is a wall, a box or a box on a goal, we cannot push the first box, and thus cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '$'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '*'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; we can move to next cell
; if it is a goal cell, change to box on goal
mov r1 '.'
cmp r1 r0 r1
skipeq r1
jmp moveLeft2ndEmpty
mov r0 playerX
load8 r0 r0
dec2 r0
mov r1 playerY
load8 r1 r1
mov r2 '*'
call levelSetCell
jmp moveLeft2ndFinal
; otherwise it is empty, change to a box
label moveLeft2ndEmpty
mov r0 playerX
load8 r0 r0
dec2 r0
mov r1 playerY
load8 r1 r1
mov r2 '$'
call levelSetCell
label moveLeft2ndFinal
; update on screen the extra cell we have changed
mov r0 playerX
load8 r0 r0
dec2 r0
mov r1 playerY
load8 r1 r1
call levelDrawUpdateCell
; change players to-square to be empty or goal if previous box-on-goal not just box
mov r0 playerX
load8 r0 r0
dec r0
mov r1 playerY
load8 r1 r1
call levelLoadCell
mov r1 '*'
cmp r1 r0 r1
skipeq r1
jmp moveLeft2ndFinalEmpty
mov r0 playerX
load8 r0 r0
dec r0
mov r1 playerY
load8 r1 r1
mov r2 '.'
call levelSetCell
jmp moveLeftMovePlayer
label moveLeft2ndFinalEmpty
mov r0 playerX
load8 r0 r0
dec r0
mov r1 playerY
load8 r1 r1
mov r2 ' '
call levelSetCell
; move player
label moveLeftMovePlayer
mov r0 playerX
load8 r1 r0
dec r1
store8 r0 r1
; redraw players from and to squares
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
push8 r0
push8 r1
call levelDrawUpdateCell
pop8 r1
pop8 r0
inc r0
call levelDrawUpdateCell
; return to wait for next move
jmp inputLoop

label moveRight
; load cell at (px+1,py)
mov r0 playerX
load8 r0 r0
inc r0
mov r1 playerY
load8 r1 r1
call levelLoadCell
; if wall cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; if floor or goal (without box) then we can move here
mov r1 ' '
cmp r1 r0 r1
skipneq r1
jmp moveRightMovePlayer
mov r1 '.'
cmp r1 r0 r1
skipneq r1
jmp moveRightMovePlayer
; otherwise we have a box or box on a goal
; need to check the next cell again
mov r0 playerX
load8 r0 r0
inc2 r0
mov r1 playerY
load8 r1 r1
call levelLoadCell
; if next cell is a wall, a box or a box on a goal, we cannot push the first box, and thus cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '$'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '*'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; we can move to next cell
; if it is a goal cell, change to box on goal
mov r1 '.'
cmp r1 r0 r1
skipeq r1
jmp moveRight2ndEmpty
mov r0 playerX
load8 r0 r0
inc2 r0
mov r1 playerY
load8 r1 r1
mov r2 '*'
call levelSetCell
jmp moveRight2ndFinal
; otherwise it is empty, change to a box
label moveRight2ndEmpty
mov r0 playerX
load8 r0 r0
inc2 r0
mov r1 playerY
load8 r1 r1
mov r2 '$'
call levelSetCell
label moveRight2ndFinal
; update on screen the extra cell we have changed
mov r0 playerX
load8 r0 r0
inc2 r0
mov r1 playerY
load8 r1 r1
call levelDrawUpdateCell
; change players to-square to be empty or goal if previous box-on-goal not just box
mov r0 playerX
load8 r0 r0
inc r0
mov r1 playerY
load8 r1 r1
call levelLoadCell
mov r1 '*'
cmp r1 r0 r1
skipeq r1
jmp moveRight2ndFinalEmpty
mov r0 playerX
load8 r0 r0
inc r0
mov r1 playerY
load8 r1 r1
mov r2 '.'
call levelSetCell
jmp moveRightMovePlayer
label moveRight2ndFinalEmpty
mov r0 playerX
load8 r0 r0
inc r0
mov r1 playerY
load8 r1 r1
mov r2 ' '
call levelSetCell
; move player
label moveRightMovePlayer
mov r0 playerX
load8 r1 r0
inc r1
store8 r0 r1
; redraw players from and to squares
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
push8 r0
push8 r1
call levelDrawUpdateCell
pop8 r1
pop8 r0
dec r0
call levelDrawUpdateCell
; return to wait for next move
jmp inputLoop

label moveUp
; load cell at (px,py-1)
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec r1
call levelLoadCell
; if wall cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; if floor or goal (without box) then we can move here
mov r1 ' '
cmp r1 r0 r1
skipneq r1
jmp moveUpMovePlayer
mov r1 '.'
cmp r1 r0 r1
skipneq r1
jmp moveUpMovePlayer
; otherwise we have a box or box on a goal
; need to check the next cell again
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec2 r1
call levelLoadCell
; if next cell is a wall, a box or a box on a goal, we cannot push the first box, and thus cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '$'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '*'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; we can move to next cell
; if it is a goal cell, change to box on goal
mov r1 '.'
cmp r1 r0 r1
skipeq r1
jmp moveUp2ndEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec2 r1
mov r2 '*'
call levelSetCell
jmp moveUp2ndFinal
; otherwise it is empty, change to a box
label moveUp2ndEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec2 r1
mov r2 '$'
call levelSetCell
label moveUp2ndFinal
; update on screen the extra cell we have changed
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec2 r1
call levelDrawUpdateCell
; change players to-square to be empty or goal if previous box-on-goal not just box
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec r1
call levelLoadCell
mov r1 '*'
cmp r1 r0 r1
skipeq r1
jmp moveUp2ndFinalEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec r1
mov r2 '.'
call levelSetCell
jmp moveUpMovePlayer
label moveUp2ndFinalEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
dec r1
mov r2 ' '
call levelSetCell
; move player
label moveUpMovePlayer
mov r0 playerY
load8 r1 r0
dec r1
store8 r0 r1
; redraw players from and to squares
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
push8 r0
push8 r1
call levelDrawUpdateCell
pop8 r1
pop8 r0
inc r1
call levelDrawUpdateCell
; return to wait for next move
jmp inputLoop

label moveDown
; load cell at (px,py+1)
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc r1
call levelLoadCell
; if wall cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; if floor or goal (without box) then we can move here
mov r1 ' '
cmp r1 r0 r1
skipneq r1
jmp moveDownMovePlayer
mov r1 '.'
cmp r1 r0 r1
skipneq r1
jmp moveDownMovePlayer
; otherwise we have a box or box on a goal
; need to check the next cell again
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc2 r1
call levelLoadCell
; if next cell is a wall, a box or a box on a goal, we cannot push the first box, and thus cannot move
mov r1 '#'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '$'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
mov r1 '*'
cmp r1 r0 r1
skipneq r1
jmp inputLoop
; we can move to next cell
; if it is a goal cell, change to box on goal
mov r1 '.'
cmp r1 r0 r1
skipeq r1
jmp moveDown2ndEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc2 r1
mov r2 '*'
call levelSetCell
jmp moveDown2ndFinal
; otherwise it is empty, change to a box
label moveDown2ndEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc2 r1
mov r2 '$'
call levelSetCell
label moveDown2ndFinal
; update on screen the extra cell we have changed
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc2 r1
call levelDrawUpdateCell
; change players to-square to be empty or goal if previous box-on-goal not just box
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc r1
call levelLoadCell
mov r1 '*'
cmp r1 r0 r1
skipeq r1
jmp moveDown2ndFinalEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc r1
mov r2 '.'
call levelSetCell
jmp moveDownMovePlayer
label moveDown2ndFinalEmpty
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
inc r1
mov r2 ' '
call levelSetCell
; move player
label moveDownMovePlayer
mov r0 playerY
load8 r1 r0
inc r1
store8 r0 r1
; redraw players from and to squares
mov r0 playerX
load8 r0 r0
mov r1 playerY
load8 r1 r1
push8 r0
push8 r1
call levelDrawUpdateCell
pop8 r1
pop8 r0
dec r1
call levelDrawUpdateCell
; return to wait for next move
jmp inputLoop
