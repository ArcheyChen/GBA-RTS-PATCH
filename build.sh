$DEVKITARM/bin/arm-none-eabi-gcc -mcpu=arm7tdmi -nostartfiles -nodefaultlibs -mthumb -fPIE -Os -fno-toplevel-reorder payload.c -T payload.ld -o payload.elf
$DEVKITARM/bin/arm-none-eabi-objcopy -O binary payload.elf payload.bin
xxd -i payload.bin > payload_bin.c 
gcc -g patcher.c payload_bin.c -o patcher