#!/bin/sh
# Builds tools/ffta_tag.s for each Final Fantasy Tactics Advance release and
# prints the C arrays for src/profiles.c.
set -e
cd "$(dirname "$0")"
t=$(mktemp -d)
build() { # name SUBSP CONT ANIM WEAPON DAMAGE
	llvm-mc -triple=armv4t-none-eabi -filetype=obj ffta_tag.s -o "$t/$1.o" \
		--defsym SUBSP=$2 --defsym CONT=$3 --defsym ANIM=$4 --defsym WEAPON=$5 --defsym DAMAGE=$6
	llvm-objcopy -O binary -j .text "$t/$1.o" "$t/$1.bin"
	python3 -c "
import sys
b = open(sys.argv[1], 'rb').read()
print('static const uint8_t ffta_tag_%s[%d] = {' % (sys.argv[2], len(b)))
for i in range(0, len(b), 12): print('\t' + ', '.join('0x%02X' % x for x in b[i:i + 12]) + ',')
print('};')" "$t/$1.bin" "$1"
}
#     release SUBSP CONT        ANIM        WEAPON      DAMAGE
build us      32    0x08001F41  0x080D311B  0x0808F3A9  0x080A8EB5
build eu      32    0x08001F79  0x080D6C2B  0x08092145  0x080AC73D
build jp      40    0x08001FA5  0x080C997B  0x08086BC1  0x080A0629
rm -r "$t"
