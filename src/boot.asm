[BITS 16]
[ORG 0x7C00]

start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    sti

    mov [boot_drive], dl

    mov si, msg_loading
    call print_string

    mov ax, 0x1000
    mov es, ax
    xor bx, bx

    mov ah, 0x02
    mov al, 63
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]

    int 0x13

    jc disk_error

    cli

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp 0x08:protected_mode_start

print_string:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    popa
    ret

disk_error:
    mov si, msg_disk_error
    call print_string
    hlt

boot_drive:     db 0
msg_loading:    db 'Loading kernel...', 13, 10, 0
msg_disk_error: db 'Disk read error!', 13, 10, 0

gdt_start:

; --- Null Descriptor (required by CPU - must be all zeros) ---
gdt_null:
    dd 0x00000000
    dd 0x00000000

gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00

gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

[BITS 32]

protected_mode_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000
    mov esp, ebp

    call 0x10000
    hlt

times 510-($-$$) db 0
dw 0xAA55
