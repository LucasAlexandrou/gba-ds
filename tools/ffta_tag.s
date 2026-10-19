@ Final Fantasy Tactics Advance: tags the sprites the game's UI code draws
@ (attr1 bit 10, unused on sprites that aren't rotated; bit 9 too for its
@ overlay list), so GBA DS's HUD split moves those and nothing else. Every sprite list the game builds
@ goes through one routine (the core, 0x080014C8 in the USA release); its
@ first 12 bytes are replaced by a Thumb->ARM long jump to this code, put
@ in the ROM's 0xFF padding at 0x08FFFF00. It runs the core, then tags the
@ entries it added, unless the core was called through the main list's
@ append (W2) or the overlay list's (W1) by one of the game's world-drawing
@ systems (the table: animations, weapon swings, damage numbers, the world
@ map's cursor). Units and thrown objects are
@ written to the lists by other code (in IWRAM) and are never tagged.
@
@ Built for each release by tools/make_ffta_tag.sh (needs llvm-mc and
@ llvm-objcopy); the bytes are in src/profiles.c (ffta_tag_us / _eu / _jp).
@   SUBSP     the core's stack frame (its 'sub sp' after the pushes)
@   CONT      the core after its first 12 bytes (Thumb: +1)
@   NARGS     the core's stack arguments; BASE / START: the offsets of the
@             list's entries and the first index among them
@   W2RET     the return address (+1) of W2's call to the core; W1RET the
@             same for W1, the overlay list's append
@   W2LR      where W2 (and W1: the same frame) keeps its own return
@             address, from sp at that call
@   ANIM, WEAPON, DAMAGE, CURSOR   return addresses (+1) of the world
@             systems' calls to W2 (W1 for the cursor)
	.syntax unified
	.arm
entry:
	add r12, pc, #1
	bx r12
	.thumb
t:
	push {r4-r7, lr}
	sub sp, #(NARGS * 4)
	@ the core's stack arguments, copied below ours
	movs r4, #0
1:	add r5, sp, #(NARGS * 4 + 20)
	ldr r6, [r5, r4]
	mov r5, sp
	str r6, [r5, r4]
	adds r4, #4
	cmp r4, #(NARGS * 4)
	blt 1b
	@ r7: tag (1) unless W2 was called by a world system (r0-r3 are the
	@ core's arguments: untouched until it's called)
	movs r7, #1
	ldr r4, [sp, #(NARGS * 4 + 16)]
	ldr r5, =W2RET
	cmp r4, r5
	beq 7f
	ldr r5, =W1RET
	cmp r4, r5
	bne 3f
7:	ldr r4, [sp, #(NARGS * 4 + 20 + W2LR)]
	adr r5, callers
2:	ldr r6, [r5]
	cmp r6, #0
	beq 3f
	adds r5, #4
	cmp r6, r4
	bne 2b
	movs r7, #0
3:	ldr r4, [sp, #BASE]
	ldr r5, [sp, #START]
	bl reloc
	cmp r7, #0
	beq 6f
	@ r6: the tag, bit 10, and bit 9 too on the overlay list (0x03002C58,
	@ two buffers of 48: the notices' arrow, the world map's date)
	movs r6, #4
	lsls r6, r6, #8
	ldr r3, =0x03002C58
	cmp r4, r3
	blo 4f
	ldr r3, =0x03002F58
	cmp r4, r3
	bhs 4f
	movs r3, #2
	lsls r3, r3, #8
	orrs r6, r3
	@ tag entries START..r0 (the count the core returns)
4:	cmp r5, r0
	bge 6f
	lsls r1, r5, #3
	adds r1, r1, r4
	ldrh r2, [r1, #0]
	lsls r3, r2, #23
	bmi 5f
	ldrh r2, [r1, #2]
	orrs r2, r6
	strh r2, [r1, #2]
5:	adds r5, #1
	b 4b
6:	add sp, #(NARGS * 4)
	pop {r4-r7}
	pop {r1}
	bx r1
	@ the core's first instructions, then the rest of it
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
	.word CURSOR @ the world map's cursor (the feather pen), through W1
	.word 0
	.ltorg
