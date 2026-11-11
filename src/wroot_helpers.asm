; Helpers placed in WROOT's dead putchar body (image 0x22D0..0x23E2), same segment as overlays.
; Encoding: ASCII 1 byte = 1 cell; 0x80 = ZAP (-1 cell); 0x81..0xDF = 1-byte glyph (1 cell);
;           0xE0..0xFF lead + trail byte = 1 cell.

; int dstrlen(char *s)  -- drop-in for strlen() at layout (centering) call sites
dstrlen:
    push bp
    mov bp, sp
    push si
    mov si, word ptr [bp+4]
    xor dx, dx
ds_l:
    mov al, byte ptr [si]
    inc si
    or al, al
    je ds_done
    cmp al, 0x80
    jne ds_n1
    dec dx
    jmp ds_l
ds_n1:
    cmp al, 0xe0
    jb ds_one
    cmp byte ptr [si], 0
    je ds_one
    inc si
ds_one:
    inc dx
    jmp ds_l
ds_done:
    mov ax, dx
    pop si
    pop bp
    RET

; word-wrap fix: replaces "mov [bp-4],ax" (AX = byte length of word at [bp-56h])
; stores the display width in [bp-4]
fixw:
    push si
    push cx
    push dx
    mov si, word ptr [bp-0x56]
    mov cx, ax
    xor dx, dx
fw_l:
    jcxz fw_done
    mov al, byte ptr [si]
    inc si
    dec cx
    cmp al, 0x80
    jne fw_n1
    dec dx
    jmp fw_l
fw_n1:
    cmp al, 0xe0
    jb fw_one
    jcxz fw_one
    inc si
    dec cx
fw_one:
    inc dx
    jmp fw_l
fw_done:
    mov word ptr [bp-4], dx
    pop dx
    pop cx
    pop si
    RET
