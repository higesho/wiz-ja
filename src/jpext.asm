; ============================================================================
;  Wizardry 6 Japanese text extension  (runs inside the EGA driver segment)
;  Assembled at runtime offset 0x2400 (driver file offset 0x2300).
;  Syntax: Intel (keystone) + build.py macros:
;     CALL x / JMP x      -> near rel16 (keystone emits bad 0x66 forms otherwise)
;     JCCF cc x           -> long conditional jump (inverse short + JMP)
;     RET / RETF          -> 0xC3 / 0xCB
;     {NAME}              -> constant substituted by build.py
;
;  Cell word format in window buffers (WROOT):
;     text   : AL = char (font0),  AH = color<<4 | 0
;     icon   : AL = glyph,         AH = 1..4 (WFONT1-4)
;     JP tile: AL = id & 0xFF,     AH = color<<4 | (5 + (id>>8))   id = 0..1023
; ============================================================================

hdr:
    JMP putc_entry          ; 0x2400  far-called from WROOT putchar stub
    JMP rcell_entry         ; 0x2403  far-called from WROOT refreshwindow stub
    JMP putc2_entry         ; 0x2406  far-called from WROOT putchar2 (icon-font text) stub
    .byte {PAD}             ; padding up to 0x2480 (vars live in 0x2410..0x247F)

; ---------------------------------------------------------------------------
; putchar2(win, ch, fontidx)  : text drawn with the 4-plane icon fonts (menus, titles)
; ---------------------------------------------------------------------------
putc2_entry:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    push bx
    push cx
    push dx
    mov ax, word ptr [bp+4]
    mov word ptr cs:[{WCS}], ax
    mov word ptr cs:[{GDS}], ds
    push cs
    call word ptr cs:[{VMHIDE}]
    mov si, word ptr [bp+10]
    mov al, byte ptr [bp+12]
    mov dx, word ptr [bp+14]
    mov byte ptr cs:[{INV}], 0
    cmp al, 0x80
    jae p2_jp
    mov byte ptr cs:[{PEND}], 0
    mov byte ptr cs:[{LRUN}], 0
    mov ah, dl
    mov word ptr cs:[{CELLW}], ax
    mov al, byte ptr [si+6]
    mov ah, byte ptr [si+7]
    CALL put_cell
    CALL advance1
    JMP pc_done
p2_jp:
    and dl, 0x0f
    mov byte ptr cs:[{COLOR}], dl      ; COLOR = icon font index (1..4)
    mov byte ptr cs:[{BGMODE}], 1
    JMP pc_jp

; ---------------------------------------------------------------------------
; putchar(win, ch, color)   far frame: [bp+4]=WROOT CS, [bp+10]=win, [bp+12]=ch, [bp+14]=color
; ---------------------------------------------------------------------------
putc_entry:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    push bx
    push cx
    push dx
    mov ax, word ptr [bp+4]
    mov word ptr cs:[{WCS}], ax
    mov word ptr cs:[{GDS}], ds
    push cs
    call word ptr cs:[{VMHIDE}]
    mov si, word ptr [bp+10]
    mov al, byte ptr [bp+12]
    mov dx, word ptr [bp+14]
    mov byte ptr cs:[{INV}], 0
    mov byte ptr cs:[{BGMODE}], 0
    test dh, 0x80
    je pc_noinv
    mov byte ptr cs:[{INV}], 1
    neg dx
pc_noinv:
    mov byte ptr cs:[{COLOR}], dl
    cmp al, 0x80
    jae pc_jp
    ; ---- plain ASCII: identical to original putchar ----
    mov byte ptr cs:[{PEND}], 0
    mov byte ptr cs:[{LRUN}], 0
    mov ah, dl
    mov cl, 4
    shl ah, cl
    mov word ptr cs:[{CELLW}], ax
    mov al, byte ptr [si+6]
    mov ah, byte ptr [si+7]
    CALL put_cell
    CALL advance1
    JMP pc_done
pc_jp:
    ; encoding: 0x80 = ZAP, 0x81..0xDF = single-byte glyph (code 1..95),
    ;           0xE0..0xFF lead + 0x80..0xFF trail = code 96 + ((lead-0xE0)<<7 | trail&0x7F)
    mov ah, byte ptr cs:[{PEND}]
    or ah, ah
    jne pc_trail
    cmp al, 0xe0
    jae pc_lead
    sub al, 0x80
    xor ah, ah
    jmp pc_small
pc_lead:
    mov byte ptr cs:[{PEND}], al
    JMP pc_done
pc_trail:
    mov byte ptr cs:[{PEND}], 0
    sub ah, 0xe0
    and al, 0x7f
    shr ah, 1
    jnc pc_t1
    or al, 0x80
pc_t1:
    add ax, 96
pc_small:
    ; ---- small (8x8) glyph ----
    mov byte ptr cs:[{LRUN}], 0
    or ax, ax
    jne pc_s1
    ; code 0 = ZAP: step the cursor back one cell (hides a hotkey letter)
    mov al, byte ptr [si+6]
    or al, al
    je pc_zret
    dec byte ptr [si+6]
pc_zret:
    JMP pc_done
pc_s1:
    cmp ax, word ptr cs:[{NSMALL}]
    JCCF ae pc_done
    mov cl, 3
    shl ax, cl
    add ax, word ptr cs:[{SFONT}]
    push si
    push ds
    push cs
    pop ds
    push cs
    pop es
    mov si, ax
    mov di, {TILEBUF}
    movsw
    movsw
    movsw
    movsw
    pop ds
    pop si
    CALL make_tile_cell
    mov al, byte ptr [si+6]
    mov ah, byte ptr [si+7]
    CALL put_cell
    CALL advance1
    JMP pc_done
pc_large:
    sub ax, 0x2000
    cmp ax, word ptr cs:[{NLARGE}]
    JCCF ae pc_done
    mov bx, 24
    mul bx
    add ax, word ptr cs:[{LFONT}]
    mov word ptr cs:[{GPTR}], ax
    CALL draw_large
pc_done:
    push cs
    call word ptr cs:[{VMSHOW}]
    pop dx
    pop cx
    pop bx
    pop di
    pop si
    pop es
    pop ds
    pop bp
    RETF

; ---------------------------------------------------------------------------
; refresh hook: AX = cell word, DI = VRAM offset; frame [bp+6]=invert, [bp+8]=mapword
; ---------------------------------------------------------------------------
rcell_entry:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    CALL prep_cell
    or bl, bl
    je rc_text
    push word ptr [bp+8]
    push cs
    call word ptr cs:[{VENTRY1}]
    add sp, 2
    jmp rc_out
rc_text:
    push word ptr [bp+8]
    push word ptr [bp+6]
    push cs
    call word ptr cs:[{VENTRY0}]
    add sp, 4
rc_out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    RETF

; ---------------------------------------------------------------------------
; advance cursor by one cell (same as original putchar)    SI = window (DS = game)
; ---------------------------------------------------------------------------
advance1:
    mov al, byte ptr [si+6]
    inc al
    cmp al, byte ptr [si]
    jl adv_s
    xor al, al
    inc byte ptr [si+7]
    mov ah, byte ptr [si+7]
    cmp ah, byte ptr [si+1]
    jl adv_s
    mov byte ptr [si+7], 0
adv_s:
    mov byte ptr [si+6], al
    RET

; ---------------------------------------------------------------------------
; put_cell: AL = col, AH = row (window relative), CELLW = word.  SI = window.
; Stores into the window buffer and draws it if visible & owned.
; ---------------------------------------------------------------------------
put_cell:
    mov byte ptr cs:[{TCOL}], al
    mov byte ptr cs:[{TROW}], ah
    cmp al, byte ptr [si]
    jae pcl_ret
    cmp ah, byte ptr [si+1]
    jae pcl_ret
    mov es, word ptr cs:[{WCS}]
    mov di, 0x1bd2
    mov dx, 1
pcl_l:
    mov bx, word ptr es:[di]
    cmp bx, si
    je pcl_f
    or bx, bx
    je pcl_ret
    add di, 2
    inc dx
    jmp pcl_l
pcl_f:
    mov byte ptr cs:[{WIDX}], dl
    mov al, byte ptr cs:[{TROW}]
    mul byte ptr [si]
    mov bl, byte ptr cs:[{TCOL}]
    xor bh, bh
    add bx, ax
    shl bx, 1
    mov ax, word ptr cs:[{CELLW}]
    mov word ptr [bx+si+0x10], ax
    test byte ptr [si+5], 1
    jne pcl_ret
    mov al, byte ptr [si+3]
    add al, byte ptr cs:[{TROW}]
    mov bl, 0x28
    mul bl
    mov word ptr cs:[{ROW40}], ax
    mov di, ax
    mov al, byte ptr [si+2]
    add al, byte ptr cs:[{TCOL}]
    cbw
    mov word ptr cs:[{COLX}], ax
    add di, ax
    CALL draw_cell_map
pcl_ret:
    RET

; DI = map index. Draw CELLW at ROW40/COLX if this window owns the cell.
draw_cell_map:
    push ds
    mov es, word ptr cs:[{WCS}]
    mov ax, word ptr es:[0x1b60]
    mov ds, ax
    mov al, byte ptr [di]
    and al, 0x3f
    cmp al, byte ptr cs:[{WIDX}]
    jne dcm_skip
    cmp byte ptr cs:[{INV}], 0
    je dcm_n
    or al, 0x80
dcm_n:
    mov ah, byte ptr [di]
    mov byte ptr [di], al
    push ax
    mov ax, word ptr cs:[{ROW40}]
    mov cl, 3
    shl ax, cl
    add ax, word ptr cs:[{COLX}]
    mov di, ax
    mov ax, word ptr cs:[{CELLW}]
    CALL prep_cell
    or bl, bl
    je dcm_text
    ; icon-font style cell: driver entry 1, arg = mapword only
    push cs
    call word ptr cs:[{VENTRY1}]
    add sp, 2
    pop ds
    RET
dcm_text:
    mov bl, byte ptr cs:[{INV}]
    xor bh, bh
    push bx
    push cs
    call word ptr cs:[{VENTRY0}]
    add sp, 4
dcm_skip:
    pop ds
    RET

; prep_cell: AX = cell word -> AX = word to hand to the driver, BL = 0 (entry0) / 1 (entry1)
;   nib 0      : font0 text          -> entry0 unchanged
;   nib 1..4   : icon font glyph     -> entry1 unchanged
;   nib 5..8   : JP tile, bg 0       -> font0 slot 0xFF, entry0
;   nib 9..12  : JP tile, icon style -> 4-plane glyph in icon font1 slot 0xFF, entry1
prep_cell:
    mov bl, ah
    and bl, 0x0f
    je pcx_text
    cmp bl, 5
    jb pcx_icon
    cmp bl, 9
    jb pcx_tile
    cmp bl, 13
    jb pcx_bg
    mov ax, 0x0020
pcx_text:
    xor bl, bl
    RET
pcx_icon:
    mov bl, 1
    RET
pcx_tile:
    CALL resolve_glyph
    xor bl, bl
    RET
pcx_bg:
    push cx
    push dx
    push si
    push di
    push ds
    push es
    sub bl, 9
    mov bh, bl
    mov bl, al
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, word ptr cs:[{VTILES}]
    mov si, bx
    mov bl, ah
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov dl, byte ptr cs:[bx+{FGTAB}]
    mov dh, byte ptr cs:[bx+{BGTAB}]
    push cs
    pop ds
    mov es, word ptr cs:[0x159]
    mov di, 0x1fe0
    mov bh, 1
pcx_p:
    xor cx, cx
pcx_y:
    mov bx, cx
    mov al, byte ptr [bx+si]
    xor ah, ah
    test dl, byte ptr cs:[{PBIT}]
    je pcx_nf
    or ah, al
pcx_nf:
    test dh, byte ptr cs:[{PBIT}]
    je pcx_nb
    not al
    or ah, al
pcx_nb:
    mov byte ptr es:[di], ah
    inc di
    inc cx
    cmp cx, 8
    jb pcx_y
    shl byte ptr cs:[{PBIT}], 1
    cmp byte ptr cs:[{PBIT}], 0x10
    jb pcx_p
    mov byte ptr cs:[{PBIT}], 1
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    mov ax, 0x01ff
    mov bl, 1
    RET

; AX = cell word. If it is a JP tile: copy tile into font0 slot 0xFF and
; return AX = (color<<4)<<8 | 0xFF.  Otherwise AX unchanged.  Preserves all but BX,CX.
resolve_glyph:
    mov bl, ah
    and bl, 0x0f
    cmp bl, 5
    jb rg_ret
    cmp bl, 9
    jae rg_ret
    sub bl, 5
    mov bh, bl
    mov bl, al
    and ah, 0xf0
    mov al, 0xff
    push ax
    push si
    push di
    push ds
    push es
    mov cl, 3
    shl bx, cl
    add bx, word ptr cs:[{VTILES}]
    mov si, bx
    push cs
    pop ds
    mov es, word ptr cs:[0x155]
    mov di, 0x7f8
    movsw
    movsw
    movsw
    movsw
    pop es
    pop ds
    pop di
    pop si
    pop ax
rg_ret:
    RET

; ---------------------------------------------------------------------------
; make_tile_cell: TILEBUF (cs) + COLOR -> CELLW (interned tile, or a space if blank)
; ---------------------------------------------------------------------------
make_tile_cell:
    push si
    push ds
    push cs
    pop ds
    mov si, {TILEBUF}
    mov ax, word ptr [si]
    or ax, word ptr [si+2]
    or ax, word ptr [si+4]
    or ax, word ptr [si+6]
    jne mtc_tile
    mov al, 0x20
    mov ah, byte ptr cs:[{COLOR}]
    cmp byte ptr cs:[{BGMODE}], 0
    jne mtc_sp2
    mov cl, 4
    shl ah, cl
mtc_sp2:
    mov word ptr cs:[{CELLW}], ax
    JMP mtc_ret
mtc_tile:
    CALL intern
    mov bl, ah
    add bl, 5
    cmp byte ptr cs:[{BGMODE}], 0
    je mtc_t1
    add bl, 4
mtc_t1:
    mov ah, byte ptr cs:[{COLOR}]
    mov cl, 4
    shl ah, cl
    or ah, bl
    mov word ptr cs:[{CELLW}], ax
mtc_ret:
    pop ds
    pop si
    RET

; ---------------------------------------------------------------------------
; thash: SI -> 8 bytes (DS) ; AX = hash 0..2047
; ---------------------------------------------------------------------------
thash:
    push bx
    push cx
    push si
    xor ax, ax
    mov cx, 4
th_l:
    mov bx, ax
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    sub ax, bx
    xor ax, word ptr [si]
    add si, 2
    loop th_l
    mov bx, ax
    xchg bl, bh
    xor ax, bx
    and ax, 0x7ff
    pop si
    pop cx
    pop bx
    RET

; ---------------------------------------------------------------------------
; intern: DS = CS, TILEBUF -> AX = tile id (0..1023)
; ---------------------------------------------------------------------------
intern:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push cs
    pop es
    mov byte ptr [{GCTRIED}], 0
in_start:
    mov si, {TILEBUF}
    CALL thash
    mov dx, ax
in_probe:
    mov bx, dx
    shl bx, 1
    add bx, word ptr [{VHASH}]
    mov ax, word ptr [bx]
    or ax, ax
    je in_new
    dec ax
    mov word ptr [{CAND}], ax
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, word ptr [{VTILES}]
    mov di, {TILEBUF}
    mov cx, 4
    repe cmpsw
    je in_found
    inc dx
    and dx, 0x7ff
    jmp in_probe
in_found:
    mov ax, word ptr [{CAND}]
    JMP in_ret
in_new:
    ; dx = empty hash slot. find free id
    mov cx, 1024
    mov bx, word ptr [{NEXTF}]
in_scan:
    and bx, 0x3ff
    mov si, bx
    add si, word ptr [{VUSED}]
    cmp byte ptr [si], 0
    je in_got
    inc bx
    loop in_scan
    cmp byte ptr [{GCTRIED}], 0
    jne in_emerg
    mov byte ptr [{GCTRIED}], 1
    CALL gc
    JMP in_start
in_emerg:
    xor bx, bx
    mov si, word ptr [{VUSED}]
in_got:
    mov byte ptr [si], 1
    mov ax, bx
    inc ax
    mov word ptr [{NEXTF}], ax
    ; copy TILEBUF -> TILES[bx]
    mov di, bx
    shl di, 1
    shl di, 1
    shl di, 1
    add di, word ptr [{VTILES}]
    mov si, {TILEBUF}
    movsw
    movsw
    movsw
    movsw
    ; hash slot
    mov si, dx
    shl si, 1
    add si, word ptr [{VHASH}]
    mov ax, bx
    inc ax
    mov word ptr [si], ax
    mov ax, bx
in_ret:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    RET

; ---------------------------------------------------------------------------
; gc: rebuild USED from all live window buffers, then rebuild HASH.  DS = CS.
; ---------------------------------------------------------------------------
gc:
    push es
    push cs
    pop es
    xor ax, ax
    mov di, word ptr [{VUSED}]
    mov cx, 512
    rep stosw
    mov di, word ptr [{VHASH}]
    mov cx, 2048
    rep stosw
    mov es, word ptr [{WCS}]
    mov bx, 0x1bd2
gc_w:
    mov si, word ptr es:[bx]
    or si, si
    je gc_rehash
    push ds
    mov ds, word ptr cs:[{GDS}]
    mov al, byte ptr [si]
    mul byte ptr [si+1]
    mov cx, ax
    lea di, [si+0x10]
    jcxz gc_wend
gc_c:
    mov ax, word ptr [di]
    mov dl, ah
    and dl, 0x0f
    cmp dl, 5
    jb gc_next
    cmp dl, 13
    jae gc_next
    sub dl, 5
    and dl, 3
    mov dh, dl
    mov dl, al
    push bx
    mov bx, dx
    add bx, word ptr cs:[{VUSED}]
    mov byte ptr cs:[bx], 1
    pop bx
gc_next:
    add di, 2
    loop gc_c
gc_wend:
    pop ds
    add bx, 2
    jmp gc_w
gc_rehash:
    xor bx, bx
gc_r:
    mov si, bx
    add si, word ptr [{VUSED}]
    cmp byte ptr [si], 0
    je gc_rn
    mov si, bx
    shl si, 1
    shl si, 1
    shl si, 1
    add si, word ptr [{VTILES}]
    CALL thash
gc_probe:
    mov si, ax
    shl si, 1
    add si, word ptr [{VHASH}]
    cmp word ptr [si], 0
    je gc_ins
    inc ax
    and ax, 0x7ff
    jmp gc_probe
gc_ins:
    mov ax, bx
    inc ax
    mov word ptr [si], ax
gc_rn:
    inc bx
    cmp bx, 1024
    jb gc_r
    mov word ptr [{NEXTF}], 0
    pop es
    RET

; ---------------------------------------------------------------------------
; get_tile_bytes: AX = cell word -> TILEBUF (cs) = its 8x8 bitmap
; ---------------------------------------------------------------------------
get_tile_bytes:
    push si
    push di
    push ds
    push es
    push cs
    pop es
    mov di, {TILEBUF}
    mov bl, ah
    and bl, 0x0f
    je gtb_font
    cmp bl, 5
    jb gtb_zero
    cmp bl, 13
    jae gtb_zero
    sub bl, 5
    and bl, 3
    mov bh, bl
    mov bl, al
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, word ptr cs:[{VTILES}]
    mov si, bx
    push cs
    pop ds
    jmp gtb_copy
gtb_font:
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    mov ds, word ptr cs:[0x155]
gtb_copy:
    movsw
    movsw
    movsw
    movsw
    jmp gtb_ret
gtb_zero:
    xor ax, ax
    stosw
    stosw
    stosw
    stosw
gtb_ret:
    pop es
    pop ds
    pop di
    pop si
    RET

; ---------------------------------------------------------------------------
; draw_large: 12x12 glyph at GPTR, placed at pixel pitch 12 horizontally.
; Glyph box spans 2 cells x 2 rows; top pixel row = cy*8 + VOFF.
; ---------------------------------------------------------------------------
draw_large:
    mov al, byte ptr [si+6]
    mov ah, byte ptr [si+7]
    mov byte ptr cs:[{CONT}], 0
    cmp byte ptr cs:[{LRUN}], 0
    je dl_new
    cmp si, word ptr cs:[{LWIN}]
    jne dl_new
    cmp ah, byte ptr cs:[{LCY}]
    jne dl_new
    cmp al, byte ptr cs:[{LNCX}]
    jne dl_new
    mov bx, word ptr cs:[{LPX}]
    mov byte ptr cs:[{CONT}], 1
    jmp dl_have
dl_new:
    xor bh, bh
    mov bl, al
    shl bx, 1
    shl bx, 1
    shl bx, 1
dl_have:
    mov word ptr cs:[{PX}], bx
    mov al, bl
    and al, 7
    mov byte ptr cs:[{SH}], al
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov byte ptr cs:[{BCOL}], bl
    mov byte ptr cs:[{BROW}], ah
    mov byte ptr cs:[{JJ}], 0
dl_j:
    mov byte ptr cs:[{KK}], 0
dl_k:
    CALL build_slice
    CALL make_tile_cell
    mov al, byte ptr cs:[{BCOL}]
    add al, byte ptr cs:[{KK}]
    mov ah, byte ptr cs:[{BROW}]
    add ah, byte ptr cs:[{JJ}]
    CALL put_cell
    inc byte ptr cs:[{KK}]
    cmp byte ptr cs:[{KK}], 2
    jb dl_k
    inc byte ptr cs:[{JJ}]
    cmp byte ptr cs:[{JJ}], 2
    jb dl_j
    ; run state
    mov ax, word ptr cs:[{PX}]
    add ax, 12
    mov word ptr cs:[{LPX}], ax
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov word ptr cs:[{LWIN}], si
    mov byte ptr cs:[{LRUN}], 1
    cmp al, byte ptr [si]
    jb dl_setcx
    mov byte ptr cs:[{LRUN}], 0
    mov byte ptr [si+6], 0
    inc byte ptr [si+7]
    mov ah, byte ptr [si+7]
    cmp ah, byte ptr [si+1]
    jb dl_ret
    mov byte ptr [si+7], 0
    jmp dl_ret
dl_setcx:
    mov byte ptr [si+6], al
    mov byte ptr cs:[{LNCX}], al
    mov al, byte ptr [si+7]
    mov byte ptr cs:[{LCY}], al
dl_ret:
    RET

; build_slice: TILEBUF = slice (JJ,KK) of glyph GPTR, OR-ed onto existing cell when
; continuing a run into a half-filled cell.  SI = window (DS = game).
build_slice:
    push si
    cmp byte ptr cs:[{KK}], 0
    jne bs_zero
    cmp byte ptr cs:[{SH}], 0
    je bs_zero
    cmp byte ptr cs:[{CONT}], 0
    je bs_zero
    ; fetch existing cell (BCOL, BROW+JJ)
    mov al, byte ptr cs:[{BROW}]
    add al, byte ptr cs:[{JJ}]
    cmp al, byte ptr [si+1]
    jae bs_zero
    mul byte ptr [si]
    mov bl, byte ptr cs:[{BCOL}]
    xor bh, bh
    add bx, ax
    shl bx, 1
    mov ax, word ptr [bx+si+0x10]
    CALL get_tile_bytes
    jmp bs_glyph
bs_zero:
    xor ax, ax
    mov word ptr cs:[{TILEBUF}], ax
    mov word ptr cs:[{TILEBUF}+2], ax
    mov word ptr cs:[{TILEBUF}+4], ax
    mov word ptr cs:[{TILEBUF}+6], ax
bs_glyph:
    push ds
    push cs
    pop ds
    ; y loop 0..7 : gy = JJ*8 + y - VOFF
    xor cx, cx
bs_y:
    mov al, byte ptr [{JJ}]
    shl al, 1
    shl al, 1
    shl al, 1
    add al, cl
    sub al, byte ptr [{VOFF}]
    cmp al, 12
    jae bs_ny
    xor ah, ah
    shl ax, 1
    mov si, ax
    add si, word ptr [{GPTR}]
    mov ah, byte ptr [si]
    mov al, byte ptr [si+1]
    cmp byte ptr [{SH}], 0
    je bs_nosh
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
bs_nosh:
    cmp byte ptr [{KK}], 0
    je bs_hi
    mov ah, al
bs_hi:
    mov si, {TILEBUF}
    add si, cx
    or byte ptr [si], ah
bs_ny:
    inc cx
    cmp cx, 8
    jb bs_y
    pop ds
    pop si
    RET
