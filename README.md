# 免责声明 / Disclaimer

本代码按“原样”提供，不对其适用性、功能性或适合任何特定用途作出任何明示或暗示的保证。使用本代码所产生的任何后果和风险由使用者自行承担，作者不承担任何责任。

This code is provided "as is", without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose and noninfringement. In no event shall the authors be liable for any claim, damages or other liability, whether in an action of contract, tort or otherwise, arising from, out of or in connection with the code or the use or other dealings in the code.
# License / 许可声明

未经授权，禁止用于商业行为。使用该代码的衍生项目需要保持开源，并且需要指明该项目的原始仓库地址（https://github.com/ArcheyChen/GBA-RTS-PATCH）。代码中的 "Ausar'S-RTSFILE." 和 "<3 from Maniac" 等识别用字符串不应修改，而应当原样保留。

Commercial use is prohibited without authorization. Any derivative project using this code must remain open source and clearly indicate the original repository address (https://github.com/ArcheyChen/GBA-RTS-PATCH). Identification strings in the code such as "Ausar'S-RTSFILE." and "<3 from Maniac" must not be altered and should be preserved as is.

# GBA RTS Patcher(**Kind of working right now**)

This project provides automatic Real-Time Save (RTS) functionality for GBA games running on SRAM flash carts. It allows players to save and restore game state at any time using button combinations.




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

## Requirements

The cart must have at least 64KB of SRAM. The 32KB of SRAM carts don't have enought space to save.(but this can be fix, I just don't have time to do so)
## Usage

```bash
./patcher input.gba
```

For GUI users, drag the ROM onto the .exe in the file browser.

## Building

Requirements:
- devkitARM toolchain
- Standard GCC compiler

Build command:
```bash
./build.sh
```

## Contributing

Currently, the Flash programming part isn't optimized, and only supports 4 types of Flash, I hope someone can optimize this part.

## Credits
RTS & C language Porting: [Ausar](https://github.com/ArcheyChen)

Base on project: [gba auto batteryless patcher](https://github.com/metroid-maniac/gba-auto-batteryless-patcher) written by [metroid-maniac](https://github.com/metroid-maniac/)

Thanks to
- [ez-flash](https://github.com/ez-flash) for [EZ Flash Omega kernel](https://github.com/ez-flash/omega-kernel) containing examples for hooking the IRQ handler and RTS patch
- [Fexean](https://gitlab.com/Fexean) for [GBABF](https://gitlab.com/Fexean/gbabf)
- [vrodin](https://github.com/vrodin) for [Burn2Slot](https://github.com/vrodin/Burn2Slot)
- [Lesserkuma](https://github.com/lesserkuma) for [FlashGBX](https://github.com/lesserkuma/FlashGBX) and batteryless versions of [Goomba Color](https://github.com/lesserkuma/goombacolor) and [PocketNES](https://github.com/lesserkuma/PocketNES)
- [Ausar](https://github.com/ArcheyChen) for helping to port the payload to C.
