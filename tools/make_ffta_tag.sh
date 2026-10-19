#!/bin/sh
# Builds tools/ffta_tag.s for each Final Fantasy Tactics Advance release and
# prints the C arrays for src/profiles.c.
set -e
cd "$(dirname "$0")"
t=$(mktemp -d)
build() { # name SUBSP CONT NARGS BASE START W2RET W2LR ANIM WEAPON DAMAGE
	llvm-mc -triple=armv4t-none-eabi -filetype=obj ffta_tag.s -o "$t/$1.o" \
		--defsym SUBSP=$2 --defsym CONT=$3 --defsym NARGS=$4 --defsym BASE=$5 --defsym START=$6 \
		--defsym W2RET=$7 --defsym W2LR=$8 --defsym ANIM=$9 --defsym WEAPON=${10} --defsym DAMAGE=${11}
	llvm-objcopy -O binary -j .text "$t/$1.o" "$t/$1.bin"
	python3 -c "
import sys
b = open(sys.argv[1], 'rb').read()
assert len(b) <= 256, len(b)
print('static const uint8_t ffta_tag_%s[%d] = {' % (sys.argv[2], len(b)))
for i in range(0, len(b), 12): print('\t' + ', '.join('0x%02X' % x for x in b[i:i + 12]) + ',')
print('};')" "$t/$1.bin" "$1"
}
#     release SUBSP CONT        NARGS BASE START W2RET       W2LR ANIM        WEAPON      DAMAGE
build us      20    0x080014D5  6     12   20    0x08001FC5  60   0x080D311B  0x0808F3A9  0x080A8EB5
build eu      20    0x0800150D  6     12   20    0x08001FFD  60   0x080D6C2B  0x08092145  0x080AC73D
build jp      36    0x080014D5  10    28   36    0x08002017  68   0x080C997B  0x08086BC1  0x080A0629
rm -r "$t"
