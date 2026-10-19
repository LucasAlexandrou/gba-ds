@ Final Fantasy Tactics Advance: tags the sprites the game draws in the
@ world (spells, weapon swings, damage numbers) so the HUD split leaves them
@ on the game screen. Placed at 0x08FFFF00 (the ROM's 0xFF padding); the
@ bytes are in src/profiles.c (ffta_tag_us / _eu / _jp), built for each
@ release by tools/make_ffta_tag.sh (needs llvm-mc and llvm-objcopy) with:
@   SUBSP   the append's stack frame (its 'sub sp' after the pushes)
@   CONT    the append's code after its first 12 bytes (Thumb: +1)
@   ANIM, WEAPON, DAMAGE   return addresses (+1) of its calls from the
@           animation engine, weapon swings and damage numbers
	.syntax unified
@ FFTA: the sprite list append (0x08001F34) jumps here (its first 12 bytes
@ replaced by a Thumb->ARM long jump). Append as usual; when the caller is
@ one of the game's world-drawing systems (the table), set attr1 bit 11
@ (unused on non-affine sprites) on each entry added, for GBA DS's HUD split.
	.arm
entry:
	add r12, pc, #1
	bx r12
	.thumb
t:
	push {r4-r7, lr}
	sub sp, #28
	movs r4, #0
1:	add r5, sp, #48
	ldr r6, [r5, r4]
	mov r5, sp
	str r6, [r5, r4]
	adds r4, #4
	cmp r4, #28
	blt 1b
	ldr r7, =0x03000028
	ldrb r4, [r7]
	lsls r4, r4, #2
	ldr r5, =0x03000020
	ldr r4, [r5, r4]
	bl reloc
	@ the caller in the table?
	ldr r3, [sp, #44]
	adr r2, callers
5:	ldr r1, [r2]
	cmp r1, #0
	beq 4f
	adds r2, #4
	cmp r1, r3
	bne 5b
	ldrb r5, [r7]
	lsls r6, r5, #2
	ldr r1, =0x03000020
	ldr r6, [r1, r6]
	lsls r5, r5, #10
	ldr r1, =0x03000030
	adds r5, r5, r1
2:	cmp r4, r6
	bge 4f
	lsls r1, r4, #3
	adds r1, r1, r5
	ldrh r2, [r1, #0]
	lsls r3, r2, #23
	bmi 3f
	ldrh r2, [r1, #2]
	movs r3, #8
	lsls r3, r3, #8
	orrs r2, r3
	strh r2, [r1, #2]
3:	adds r4, #1
	b 2b
4:	add sp, #28
	pop {r4-r7}
	pop {r1}
	bx r1
	@ the append's first instructions, then the rest of it
reloc:
	push {r4-r7, lr}
	mov r7, r10
	mov r6, r9
	mov r5, r8
	push {r5-r7}
	sub sp, #SUBSP
	ldr r4, =CONT
	bx r4
	.align 2
callers:
	.word ANIM   @ animations: spells, abilities, explosions, weather
	.word WEAPON @ weapon swings
	.word DAMAGE @ damage numbers
	.word 0
	.ltorg
