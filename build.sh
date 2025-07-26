arm-none-eabi-gcc -mcpu=arm7tdmi -nostartfiles -nodefaultlibs -mthumb -fPIC  -mpic-data-is-text-relative -mno-single-pic-base -Os -fno-toplevel-reorder -Wall -Wextra -Wshadow -Werror payload.c -T payload.ld -o payload.elf


arm-none-eabi-objcopy -O binary payload.elf payload.bin
xxd -i payload.bin > payload_bin.c 
gcc -g patcher.c payload_bin.c -o patcher