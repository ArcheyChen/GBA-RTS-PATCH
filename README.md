# GBA Auto Batteryless Patcher

This project provides automatic Real-Time Save (RTS) functionality for GBA games running on batteryless flash carts. It allows players to save and restore game state at any time using button combinations.

**not working right now**

## Features

- **Real-Time Save**: Press L+R+START to save current game state
- **Real-Time Load**: Press L+R+SELECT to restore previously saved state
- **Automatic patching**: Automatically detects game type and injects appropriate save code
- **Multiple flash chip support**: Compatible with various flash cart types

## How it Works

The patcher injects custom code into your GBA ROM that:
1. Hijacks the interrupt handler to detect button combinations
2. Saves complete game state (CPU registers, RAM, VRAM, etc.) to flash storage
3. Restores game state seamlessly when requested

## Technical Documentation

For detailed technical information, please refer to the following documents (written in Chinese, use Google Translate for English):

- **[PATCH_MECHANISM.md](PATCH_MECHANISM.md)** - Explains how the patch injection works, position-independent code implementation, and flash operation techniques
- **[SAVE_RESTORE_MECHANISM.md](SAVE_RESTORE_MECHANISM.md)** - Details the save data layout, sector allocation, and save/restore process
- **[CURRENT_ISSUES.md](CURRENT_ISSUES.md)** - Documents current technical challenges and debugging efforts

## Requirements

The game must be SRAM patched before using this program. GBATA or [Flash1M_Repro_SRAM_patcher](https://github.com/bbsan2k/Flash1M_Repro_SRAM_Patcher) can be used depending on the game.

## Usage

```bash
# Basic usage
./patcher input.gba output.gba

# With specific flash type (0=auto, 1-4=specific type)
./patcher input.gba output.gba [flash_type] [save_size]
```

For GUI users, drag the ROM onto the .exe in the file browser.

## Building

Requirements:
- devkitARM toolchain
- Standard GCC compiler

Build command:
```bash
$DEVKITARM/bin/arm-none-eabi-gcc -mcpu=arm7tdmi -nostartfiles -nodefaultlibs -mthumb -fPIE -Os -fno-toplevel-reorder payload.c -T payload.ld -o payload.elf
$DEVKITARM/bin/arm-none-eabi-objcopy -O binary payload.elf payload.bin
xxd -i payload.bin > payload_bin.c
gcc -g patcher.c payload_bin.c -o patcher
```

## Contributing

This project is actively seeking help with:
- Resolving CPU mode switching issues when calling C functions
- Optimizing flash programming routines
- Improving compatibility with different games
- Adding support for more flash chip types

Please see [CURRENT_ISSUES.md](CURRENT_ISSUES.md) for specific technical challenges that need solving.

## Credits
RTS & C language Porting: [Ausar](https://github.com/ArcheyChen)

Base on project: [gba auto batteryless patcher](https://github.com/metroid-maniac/gba-auto-batteryless-patcher) written by [metroid-maniac](https://github.com/metroid-maniac/)

RTS algorithm: [EzFlash's EZODE](https://github.com/ez-flash/omega-de-kernel/blob/main/source/gba_rts_patch.s)
Thanks to
- [ez-flash](https://github.com/ez-flash) for [EZ Flash Omega kernel](https://github.com/ez-flash/omega-kernel) containing examples for hooking the IRQ handler
- [Fexean](https://gitlab.com/Fexean) for [GBABF](https://gitlab.com/Fexean/gbabf)
- [vrodin](https://github.com/vrodin) for [Burn2Slot](https://github.com/vrodin/Burn2Slot)
- [Lesserkuma](https://github.com/lesserkuma) for [FlashGBX](https://github.com/lesserkuma/FlashGBX) and batteryless versions of [Goomba Color](https://github.com/lesserkuma/goombacolor) and [PocketNES](https://github.com/lesserkuma/PocketNES)
- [Ausar](https://github.com/ArcheyChen) for helping to port the payload to C.
