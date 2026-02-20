	assume	adl=1
	section	.text
	public	_pick_best_score

; uint8_t pick_best_score(const int16_t *scores, uint8_t count, uint8_t start)
;
; Scans scores[start..count-1] and returns the index of the highest value.
; Used by pick_move() for selection sort — replaces the C inner loop
; which the compiler generates with duplicate comparisons + __setflag calls.
;
; Calling convention (eZ80 CE toolchain):
;   Stack: [ret_addr(3)] [scores(3)] [count(3)] [start(3)]
;   Return: A = best index
;
; Register usage inside loop:
;   IY = pointer to scores[i] (advances by 2 per iteration)
;   DE = best_score (16-bit signed, stored as XOR 0x80 in D for unsigned cmp)
;   B  = count (loop bound)
;   C  = best index
;   A  = loop counter i
;
; Signed 16-bit comparison trick: XOR high byte with 0x80 converts
; signed ordering to unsigned ordering. We store D already flipped,
; so each iteration only flips the loaded score's high byte.

_pick_best_score:
	push	ix
	ld	ix, 0
	add	ix, sp

	; Load arguments
	ld	iy, (ix + 6)		; IY = scores pointer
	ld	b, (ix + 9)		; B = count
	ld	c, (ix + 12)		; C = start (= initial best index)

	; Compute IY = &scores[start]
	ld	a, c
	add	a, a			; A = start * 2 (int16_t stride)
	ld	l, a
	ld	h, 0
	push	hl			; can't add to IY directly easily
	pop	de
	add	iy, de			; IY = scores + start*2

	; Load initial best_score from scores[start]
	ld	e, (iy)			; low byte
	ld	d, (iy + 1)		; high byte
	ld	a, d
	xor	a, 80h			; flip sign bit for unsigned comparison
	ld	d, a			; D = flipped high byte of best_score

	; Advance to scores[start+1]
	lea	iy, iy + 2
	ld	a, c
	inc	a			; A = i = start + 1

.loop:
	cp	a, b			; i >= count?
	jr	nc, .done

	; Load scores[i] high byte, flip sign bit
	ld	h, (iy + 1)		; high byte of scores[i]
	ld	l, h			; save original for potential update
	ld	h, a			; save i temporarily
	ld	a, l
	xor	a, 80h			; flip sign bit
	ld	l, a			; L = flipped high byte of scores[i]
	ld	a, h			; restore i

	; Compare high bytes (unsigned): D vs L
	; if D < L: scores[i] > best_score → new best
	; if D > L: scores[i] < best_score → skip
	; if D == L: compare low bytes
	push	af			; save i+flags
	ld	a, d
	cp	a, l			; compare flipped high bytes
	jr	c, .new_best		; best < scores[i] (high byte)
	jr	nz, .skip		; best > scores[i] (high byte)

	; High bytes equal — compare low bytes (unsigned)
	ld	a, e
	cp	a, (iy)			; compare low bytes
	jr	nc, .skip		; best_score >= scores[i]

.new_best:
	; Update best
	ld	e, (iy)			; E = new best_score low byte
	ld	d, l			; D = new flipped high byte (already flipped)
	pop	af			; restore i
	ld	c, a			; C = best = i

	; Advance
	lea	iy, iy + 2
	inc	a
	jr	.loop

.skip:
	pop	af			; restore i
	lea	iy, iy + 2
	inc	a
	jr	.loop

.done:
	ld	a, c			; return best index in A
	pop	ix
	ret
