; ============================================================================
;  Wizardry 7 (DOS 版 DSAVANT) 日本語表示モジュール
;  VGA.DRV の後ろに足す 16 ビットのコード。ドライバのセグメント内で動く。
;  実行時のオフセット = ファイル位置 + 0x100 (DS.EXE がドライバを offset 0x100 に読み込む)。
;  構文: Intel (keystone) + build_dos.py のマクロ
;     CALL x / JMP x  -> near rel16    JCCF cc x -> 遠い条件分岐    RET / RETF
;     {NAME}          -> build_dos.py が数値に置き換える
;
;  日本語 1 文字 = 2 バイト [0x80 + n>>7] [0x80 + n&0x7f] (n = 字の番号)。
;  字形は 12x12 を 18 バイトに詰めたもの (2 行 = 3 バイト)。{GLYPHS} から n*18。
;  高さ {SMALLH} 以下のフォントでは、n < {NSMALL} の字を 8x8 (美咲、7 行に詰めて {SGLYPHS} から n*7) で描く。
;
;  入口:
;    putc_jp   : ドライバの 1 文字描画 (飛び先表 11 番) の代わり。半角は元の処理へ
;    setpos_jp : 描画位置の設定 (飛び先表 13 番) の代わり。2 バイト文字の途中状態を消す
;    width_far : DS.EXE の文字列幅 (0x38ca) から呼ばれる。全角 1 字 = 12 ドット
;    unit_far  : 物語の語分け (各オーバーレイ) から呼ばれ、次の折り返し単位の長さを返す
; ============================================================================

vars:
    .byte {VARPAD}

; ---------------------------------------------------------------------------
; putc_jp : 元の putc と同じ呼ばれ方 (near call)。AX = フォントのセグメント
;   [bp+0xc] = 文字, [bp+0xe] = フォント番号
; ---------------------------------------------------------------------------
putc_jp:
    push bp
    mov bp, sp
    mov bx, word ptr [bp+0xc]
    cmp bl, 0x80
    jae pj_hi
    mov byte ptr cs:[{PEND}], 0
    pop bp
    jmp word ptr cs:[{OPUTC}]
pj_hi:
    mov bh, byte ptr cs:[{PEND}]
    or bh, bh
    jne pj_trail
    mov byte ptr cs:[{PEND}], bl
    pop bp
    RET
pj_trail:
    mov byte ptr cs:[{PEND}], 0
    push ds
    push es
    push si
    push di
    push cx
    push dx
    mov ds, ax
    mov al, bh
    xor ah, ah
    sub al, 0x80
    mov cl, 7
    shl ax, cl
    xor bh, bh
    sub bl, 0x80
    add ax, bx
    mov word ptr cs:[{GW}], 12
    cmp ax, {NGLYPH}
    JCCF ae pj_adv
    cmp byte ptr ds:[1], {SMALLH}
    ja pj_big
    cmp ax, {NSMALL}
    jae pj_big
    ; 小さいフォント用 (美咲 8x8): 1 行 1 バイト、8 行目は空なので 7 バイトだけ持つ
    mov word ptr cs:[{GW}], 8
    mov word ptr cs:[{GH}], 8
    mov si, ax
    mov cl, 3
    shl ax, cl
    sub ax, si
    add ax, {SGLYPHS}
    mov si, ax
    mov di, {ROWBUF}
    mov cx, 7
pj_s:
    mov ah, byte ptr cs:[si]
    xor al, al
    mov word ptr cs:[di], ax
    inc si
    add di, 2
    loop pj_s
    mov word ptr cs:[di], 0
    jmp pj_geo
pj_big:
    ; 東雲 12x12: 2 行 = 3 バイト
    mov word ptr cs:[{GH}], 12
    mov cx, 18
    mul cx
    add ax, {GLYPHS}
    mov si, ax
    mov di, {ROWBUF}
    mov cx, 6
pj_unp:
    mov ah, byte ptr cs:[si]
    mov al, byte ptr cs:[si+1]
    and al, 0xf0
    mov word ptr cs:[di], ax
    mov ah, byte ptr cs:[si+1]
    mov al, byte ptr cs:[si+2]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov word ptr cs:[di+2], ax
    add si, 3
    add di, 4
    loop pj_unp
pj_geo:
    ; 字形はフォントの行の中央に置く: top = y + (h - GH) / 2
    ; 描く行は min(y, top) から max(y + h, top + GH) まで (不透明のとき文字の枠も塗る)
    mov al, byte ptr ds:[1]
    xor ah, ah
    mov bx, ax
    sub ax, word ptr cs:[{GH}]
    sar ax, 1
    add ax, word ptr cs:[0x297]
    mov word ptr cs:[{TOP}], ax
    mov cx, word ptr cs:[0x297]
    cmp ax, cx
    jge pj_s1
    mov cx, ax
pj_s1:
    mov word ptr cs:[{RSTART}], cx
    mov dx, word ptr cs:[0x297]
    add dx, bx
    mov word ptr cs:[{CY1}], dx
    add ax, word ptr cs:[{GH}]
    cmp ax, dx
    jle pj_s2
    mov dx, ax
pj_s2:
    mov word ptr cs:[{ROWS}], dx
    mov byte ptr cs:[{OPQ}], 1
    cmp word ptr cs:[0x299], 0
    jne pj_g2
    test byte ptr ds:[6], 2
    je pj_g2
    mov byte ptr cs:[{OPQ}], 0
pj_g2:
    mov ax, word ptr cs:[{RSTART}]
    mov cx, 320
    mul cx
    add ax, word ptr cs:[0x295]
    mov di, ax
    mov es, word ptr cs:[0x1e5]
    mov dl, byte ptr cs:[0x293]
    mov dh, byte ptr cs:[0x291]
    mov cx, word ptr cs:[{RSTART}]
pj_row:
    xor ax, ax
    mov si, cx
    sub si, word ptr cs:[{TOP}]
    jl pj_rb
    cmp si, word ptr cs:[{GH}]
    jge pj_rb
    shl si, 1
    mov ax, word ptr cs:[si+{ROWBUF}]
pj_rb:
    ; 背景を塗るのはフォントの行の高さの中だけ (はみ出した分は字の点だけ描き、隣の行を消さない)
    mov byte ptr cs:[{ROPQ}], 0
    cmp byte ptr cs:[{OPQ}], 0
    je pj_rc
    cmp cx, word ptr cs:[0x297]
    jl pj_rc
    cmp cx, word ptr cs:[{CY1}]
    jge pj_rc
    mov byte ptr cs:[{ROPQ}], 1
pj_rc:
    push di
    mov bx, word ptr cs:[{GW}]
pj_px:
    shl ax, 1
    jc pj_fg
    cmp byte ptr cs:[{ROPQ}], 0
    je pj_nx
    mov byte ptr es:[di], dh
    jmp pj_nx
pj_fg:
    mov byte ptr es:[di], dl
pj_nx:
    inc di
    dec bx
    jne pj_px
    pop di
    add di, 320
    inc cx
    cmp cx, word ptr cs:[{ROWS}]
    jl pj_row
pj_adv:
    mov ax, word ptr cs:[{GW}]
    add word ptr cs:[0x295], ax
    pop dx
    pop cx
    pop di
    pop si
    pop es
    pop ds
    pop bp
    RET

; ---------------------------------------------------------------------------
setpos_jp:
    mov byte ptr cs:[{PEND}], 0
    jmp word ptr cs:[{OSETPOS}]

; ---------------------------------------------------------------------------
; width_far : DS.EXE 0x38ca の差し替え先 (retf で飛んでくる。戻りは retf)
;   スタック [sp] = 戻り先, [sp+2] = DS.EXE のコードセグメント
;   bp は呼び出し元の枠: [bp+4] = 文字列, [bp+6] = フォント番号
; ---------------------------------------------------------------------------
width_far:
    cld
    push si
    push es
    push bx
    push cx
    push dx
    mov bx, sp
    mov es, word ptr ss:[bx+12]
    mov bx, word ptr [bp+6]
    shl bx, 1
    mov ax, word ptr es:[bx+0x364a]
    xor cx, cx
    or ax, ax
    je wf_done
    mov es, ax
    mov al, byte ptr es:[7]
    cbw
    mov dx, ax
    mov si, word ptr [bp+4]
wf_l:
    lodsb
    or al, al
    je wf_done
    cmp al, 0x80
    jae wf_hi
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov al, byte ptr es:[bx+0x11]
    xor ah, ah
    add cx, ax
    add cx, dx
    jmp wf_l
wf_hi:
    mov bh, al
    lodsb
    or al, al
    je wf_done
    push cx
    mov bl, al
    mov al, bh
    sub al, 0x80
    xor ah, ah
    mov cl, 7
    shl ax, cl
    sub bl, 0x80
    xor bh, bh
    add ax, bx
    pop cx
    mov bx, 12
    cmp byte ptr es:[1], {SMALLH}
    ja wf_h2
    cmp ax, {NSMALL}
    jae wf_h2
    mov bx, 8
wf_h2:
    add cx, bx
    jmp wf_l
wf_done:
    mov ax, cx
    pop dx
    pop cx
    pop bx
    pop es
    pop si
    RETF

; ---------------------------------------------------------------------------
; unit_far : 物語の語分けの差し替え (オーバーレイの split から DS.EXE の中継を経て来る)
;   [bp+4] = メッセージ位置へのポインタ (先頭の空白を飛ばした位置に書き換える)
;   [bp+6] = 1 ならメッセージの先頭, [bp+8] = つなぎの文字列 (" " か "") を書く場所
;   戻り値 AX = 次の単位の長さ (0 = 終わり)
;   単位 = 語頭の制御記号の並び / 半角の連続 + 全角 1 字 + 行頭禁則 (Gold 版 DLL と同じ)
; ---------------------------------------------------------------------------
unit_far:
    cld
    push si
    push di
    push bx
    push cx
    push dx
    cmp word ptr [bp+6], 0
    je uf_1
    mov byte ptr cs:[{SPB}], 1
uf_1:
    mov bx, word ptr [bp+4]
    mov si, word ptr [bx]
uf_sk:
    cmp byte ptr [si], 0x20
    jne uf_2
    mov byte ptr cs:[{SPB}], 1
    inc si
    jmp uf_sk
uf_2:
    mov word ptr [bx], si
    xor ax, ax
    cmp byte ptr [si], 0
    JCCF e uf_ret
    mov di, si
uf_c:
    mov al, byte ptr [di]
    CALL isctrl
    jne uf_cend
    inc di
    cmp al, 0x7e
    jne uf_c
    cmp byte ptr [di], 0
    je uf_c
    inc di
    jmp uf_c
uf_cend:
    mov ax, di
    sub ax, si
    je uf_unit
    mov bx, word ptr [bp+8]
    mov byte ptr [bx], 0
    JMP uf_ret
uf_unit:
    mov di, si
uf_we:
    mov al, byte ptr [di]
    or al, al
    je uf_we2
    cmp al, 0x20
    je uf_we2
    inc di
    jmp uf_we
uf_we2:
    mov word ptr cs:[{WEND}], di
    CALL unit_end
    mov bx, si
uf_us:
    cmp bx, di
    jae uf_us2
    cmp byte ptr [bx], 0x5f
    jne uf_us1
    mov byte ptr [bx], 0x20
uf_us1:
    inc bx
    jmp uf_us
uf_us2:
    mov al, 0x20
    cmp byte ptr cs:[{SPB}], 0
    jne uf_s1
    xor al, al
    jmp uf_s3
uf_s1:
    cmp byte ptr cs:[{LASTJP}], 0
    je uf_s3
    mov bx, si
    CALL isjp
    jne uf_s3
    xor al, al
uf_s3:
    mov bx, word ptr [bp+8]
    mov byte ptr [bx], al
    mov byte ptr cs:[{SPB}], 0
    mov byte ptr cs:[{LASTJP}], 0
    mov ax, di
    sub ax, si
    cmp ax, 2
    jb uf_l2
    mov bx, di
    sub bx, 2
    CALL isjp
    jne uf_l2
    mov byte ptr cs:[{LASTJP}], 1
uf_l2:
    mov ax, di
    sub ax, si
uf_ret:
    pop dx
    pop cx
    pop bx
    pop di
    pop si
    RETF

; al -> ZF=1 なら制御記号 (! % & ] @ # ~ |)
isctrl:
    cmp al, 0x21
    je ic_r
    cmp al, 0x25
    je ic_r
    cmp al, 0x26
    je ic_r
    cmp al, 0x5d
    je ic_r
    cmp al, 0x40
    je ic_r
    cmp al, 0x23
    je ic_r
    cmp al, 0x7e
    je ic_r
    cmp al, 0x7c
ic_r:
    RET

; bx -> ZF=1 なら [bx] は全角 1 字
isjp:
    test byte ptr [bx], 0x80
    je ij_no
    test byte ptr [bx+1], 0x80
    je ij_no
    cmp al, al
    RET
ij_no:
    push ax
    mov al, 1
    or al, al
    pop ax
    RET

; al -> ZF=1 なら英数字
isalnum:
    cmp al, 0x30
    jb an_no
    cmp al, 0x39
    jbe an_yes
    cmp al, 0x41
    jb an_no
    cmp al, 0x5a
    jbe an_yes
    cmp al, 0x61
    jb an_no
    cmp al, 0x7a
    jbe an_yes
an_no:
    push ax
    mov al, 1
    or al, al
    pop ax
    RET
an_yes:
    cmp al, al
    RET

; bx = 位置, dx = 表 -> ZF=0 なら表にある全角字
member:
    CALL isjp
    jne mb_no
    push ax
    push cx
    push si
    mov al, byte ptr [bx]
    sub al, 0x80
    xor ah, ah
    mov cl, 7
    shl ax, cl
    mov cl, byte ptr [bx+1]
    sub cl, 0x80
    xor ch, ch
    add ax, cx
    cmp ax, {NGLYPH}
    jae mb_no2
    mov si, ax
    mov cl, 3
    shr si, cl
    add si, dx
    mov cl, al
    and cl, 7
    mov al, 1
    shl al, cl
    test byte ptr cs:[si], al
    pop si
    pop cx
    pop ax
    RET
mb_no2:
    pop si
    pop cx
    pop ax
mb_no:
    cmp al, al
    RET

; 単位の終わり: si = 先頭, cs:[WEND] = 語の終わり -> di = 単位の終わり
unit_end:
    push bx
    push dx
    mov bx, si
ue_loop:
    cmp bx, word ptr cs:[{WEND}]
    jae ue_end
    CALL isjp
    je ue_b
    inc bx
    jmp ue_loop
ue_b:
    mov dx, {BM_NOEND}
    CALL member
    je ue_c
    mov ax, bx
    add ax, 2
    cmp ax, word ptr cs:[{WEND}]
    jae ue_c
    mov bx, ax
    jmp ue_loop
ue_c:
    add bx, 2
    CALL skip_nostart
    cmp bx, word ptr cs:[{WEND}]
    jae ue_d
    cmp byte ptr [bx], 0x28
    jne ue_d
    mov di, bx
ue_p:
    cmp di, word ptr cs:[{WEND}]
    jae ue_d
    cmp byte ptr [di], 0x29
    je ue_p2
    test byte ptr [di], 0x80
    jne ue_d
    inc di
    jmp ue_p
ue_p2:
    lea bx, [di+1]
    CALL skip_nostart
ue_d:
    mov di, bx
ue_r:
    cmp di, word ptr cs:[{WEND}]
    jae ue_end
    mov al, byte ptr [di]
    test al, 0x80
    jne ue_rq
    CALL isalnum
    je ue_rq
    inc di
    jmp ue_r
ue_rq:
    mov di, bx
    jmp ue_out
ue_end:
    mov di, word ptr cs:[{WEND}]
ue_out:
    pop dx
    pop bx
    RET

skip_nostart:
    mov dx, {BM_NOSTART}
sn_l:
    cmp bx, word ptr cs:[{WEND}]
    jae sn_r
    CALL member
    je sn_r
    add bx, 2
    jmp sn_l
sn_r:
    RET

; ---------------------------------------------------------------------------
; dlen_far : 中央寄せ用の「文字数」(DS.EXE の中継から retf で来る)。[bp+4] = 文字列
;   元のゲームは strlen x 6 ドットで中央寄せする。日本語 1 字は 2 バイトで幅 8 ドット (美咲) なので、
;   (英字 6 + 日本語 8 ドットの合計 + 5) / 6 を返す
; ---------------------------------------------------------------------------
dlen_far:
    cld
    push si
    push cx
    xor cx, cx
    mov si, word ptr [bp+4]
dl_l:
    lodsb
    or al, al
    je dl_d
    cmp al, 0x80
    jae dl_hi
    add cx, 6
    jmp dl_l
dl_hi:
    lodsb
    or al, al
    je dl_d
    add cx, 8
    jmp dl_l
dl_d:
    mov ax, cx
    add ax, 5
    xor dx, dx
    mov cx, 6
    div cx
    pop cx
    pop si
    RETF
