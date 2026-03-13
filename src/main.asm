org 0x7C00
bits 16


%define ENDL 0x0D, 0x0A


main:
    ; setup data segments
    MOV ax, 0   ; can't write to ds/es directly
    MOV ds, ax
    MOV es, ax

    ; setup stack
    MOV ss, ax
    MOV sp, 0x7C00  ; stack grows downwards from where we are loaded in memory

    ; print message
    MOV si, msg_hello
    CALL puts

    HLT

;
; Prints a string to the screen
; Params:
;   - ds:si points to string
;
puts:
    ; save registers we will modify
    PUSH si
    PUSH ax

.loop:
    LODSB       ; loads next character in al
    OR al,al    ; verify if next character is null
    JZ .done

    MOV ah, 0x0e
    MOV bh, 0
    INT 0x10

    JMP .loop
    
.done:
    POP ax
    POP si
    RET

.halt: jmp .halt

msg_hello: db 'Hello World!', ENDL, 0

TIMES 510-($-$$) db 0
dw 0x55AA