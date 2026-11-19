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

; Trap-name anagram (WTREA "inspect"): the game shuffles the trap name byte by byte.
; trap_prep(dst, src) replaces strcpy(0x50de, 0x50ca): each double-byte char is stored
; as a one-byte placeholder 1..16 and its two bytes are kept in trap_tbl.
trap_prep:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov di, word ptr [bp+4]
    mov si, word ptr [bp+6]
    mov bx, trap_tbl
    mov dl, 1
tpp_l:
    mov al, byte ptr [si]
    inc si
    cmp al, 0xe0
    jb tpp_st
    mov ah, byte ptr [si]
    or ah, ah
    je tpp_st
    cmp dl, 16
    ja tpp_st
    inc si
    mov word ptr cs:[bx], ax
    add bx, 2
    mov al, dl
    inc dl
tpp_st:
    mov byte ptr [di], al
    inc di
    or al, al
    jne tpp_l
    pop bx
    pop di
    pop si
    pop bp
    RET

; trap_put(win, ch, color) replaces putchar for the shuffled letters: placeholders print
; their double-byte char (lead + trail = one cell), anything else goes to putchar as is.
; (putchar 0x22b7 is called through DX: the CALL macro cannot reach absolute targets here)
trap_put:
    push bp
    mov bp, sp
    mov al, byte ptr [bp+6]
    dec al
    cmp al, 16
    jae tpu_plain
    push bx
    xor bx, bx
    mov bl, al
    shl bx, 1
    add bx, trap_tbl
    mov ax, word ptr cs:[bx]
    pop bx
    push ax
    push word ptr [bp+8]
    xor ah, ah
    push ax
    push word ptr [bp+4]
    mov dx, 0x22b7
    call dx
    add sp, 6
    pop ax
    push word ptr [bp+8]
    mov al, ah
    xor ah, ah
    push ax
    push word ptr [bp+4]
    mov dx, 0x22b7
    call dx
    add sp, 6
    pop bp
    RET
tpu_plain:
    pop bp
    mov dx, 0x22b7
    jmp dx

trap_tbl:
    .word 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
