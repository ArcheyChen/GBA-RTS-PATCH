/*
 * License / 许可声明
 *
 * 未经授权，禁止用于商业行为。使用该代码的衍生项目需要保持开源，并且需要指明该项目的原始仓库地址（https://github.com/ArcheyChen/GBA-RTS-PATCH）。
 * 代码中的 "Ausar'S-RTSFILE." 和 "<3 from Maniac" 等识别用字符串不应修改，而应当原样保留。
 *
 * Commercial use is prohibited without authorization. Any derivative project using this code must remain open source and clearly indicate the original repository address (https://github.com/ArcheyChen/GBA-RTS-PATCH).
 * Identification strings in the code such as "Ausar'S-RTSFILE." and "<3 from Maniac" must not be altered and should be preserved as is.
 *
 * 免责声明 / Disclaimer
 * 本代码按“原样”提供，不对其适用性、功能性或适合任何特定用途作出任何明示或暗示的保证。使用本代码所产生的任何后果和风险由使用者自行承担，作者不承担任何责任。
 * This code is provided "as is", without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose and noninfringement. In no event shall the authors be liable for any claim, damages or other liability, whether in an action of contract, tort or otherwise, arising from, out of or in connection with the code or the use or other dealings in the code.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_bin.h"
#include "payload_header.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#include <conio.h>
#elif defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
// MSYS2/MinGW/Cygwin 环境
#define strcasecmp _stricmp
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

FILE *romfile;
FILE *outfile;
uint32_t romsize;
uint8_t rom[0x02000000];
char signature[] = "<3 from Maniac";

// 从gba-auto-batteryless-patcher移植的存档函数签名
static unsigned char write_sram_signature[] = { 0x30, 0xB5, 0x05, 0x1C, 0x0C, 0x1C, 0x13, 0x1C, 0x0B, 0x4A, 0x10, 0x88, 0x0B, 0x49, 0x08, 0x40};
static unsigned char write_sram2_signature[] = { 0x80, 0xb5, 0x83, 0xb0, 0x6f, 0x46, 0x38, 0x60, 0x79, 0x60, 0xba, 0x60, 0x09, 0x48, 0x09, 0x49 };
static unsigned char write_sram_ram_signature[] = { 0x04, 0xC0, 0x90, 0xE4, 0x01, 0xC0, 0xC1, 0xE4, 0x2C, 0xC4, 0xA0, 0xE1, 0x01, 0xC0, 0xC1, 0xE4 };
static unsigned char write_eeprom_signature[] = { 0x70, 0xB5, 0x00, 0x04, 0x0A, 0x1C, 0x40, 0x0B, 0xE0, 0x21, 0x09, 0x05, 0x41, 0x18, 0x07, 0x31, 0x00, 0x23, 0x10, 0x78};
static unsigned char write_flash_signature[] = { 0x70, 0xB5, 0x00, 0x03, 0x0A, 0x1C, 0xE0, 0x21, 0x09, 0x05, 0x41, 0x18, 0x01, 0x23, 0x1B, 0x03};
static unsigned char write_flash2_signature[] = { 0x7C, 0xB5, 0x90, 0xB0, 0x00, 0x03, 0x0A, 0x1C, 0xE0, 0x21, 0x09, 0x05, 0x09, 0x18, 0x01, 0x23};
static unsigned char write_flash3_signature[] = { 0xF0, 0xB5, 0x90, 0xB0, 0x0F, 0x1C, 0x00, 0x04, 0x04, 0x0C, 0x03, 0x48, 0x00, 0x68, 0x40, 0x89 };
static unsigned char write_eepromv111_signature[] = { 0x0A, 0x88, 0x80, 0x21, 0x09, 0x06, 0x0A, 0x43, 0x02, 0x60, 0x07, 0x48, 0x00, 0x47, 0x00, 0x00 };

// 跨平台的"按任意键继续"函数
static void press_any_key(void)
{
    printf("Press any key to continue...");
    fflush(stdout);
    
#if defined(_MSC_VER) || defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
    // Windows/MSYS2/MinGW/Cygwin 环境使用 _getch() 或 getch()
    #ifdef _MSC_VER
        _getch();
    #else
        getch();  // MinGW/MSYS2 中使用 getch()
    #endif
#else
    // Linux/Unix 使用 termios 来实现单字符输入
    struct termios old_termios, new_termios;
    
    // 获取当前终端设置
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    
    // 设置为原始模式：不回显，不需要按回车
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    
    // 读取一个字符
    getchar();
    
    // 恢复原来的终端设置
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
#endif
    printf("\n");
}


static uint8_t *memfind(uint8_t *haystack, size_t haystack_size, uint8_t *needle, size_t needle_size, int stride)
{
    for (size_t i = 0; i < haystack_size - needle_size; i += stride)
    {
        if (!memcmp(haystack + i, needle, needle_size))
        {
            return haystack + i;
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    // 打印License声明
    puts("============================================================");
    puts("License / 许可声明");
    puts("未经授权，禁止用于商业行为。使用该代码的衍生项目需要保持开源，并且需要指明该项目的原始仓库地址（https://github.com/ArcheyChen/GBA-RTS-PATCH）。");
    puts("代码中的 'Ausar'S-RTSFILE.' 和 '<3 from Maniac' 等识别用字符串不应修改，而应当原样保留。");
    puts("");
    puts("Commercial use is prohibited without authorization. Any derivative project using this code must remain open source and clearly indicate the original repository address (https://github.com/ArcheyChen/GBA-RTS-PATCH).");
    puts("Identification strings in the code such as 'Ausar'S-RTSFILE.' and '<3 from Maniac' must not be altered and should be preserved as is.");
    puts("");
    puts("免责声明 / Disclaimer");
    puts("本代码按“原样”提供，不对其适用性、功能性或适合任何特定用途作出任何明示或暗示的保证。使用本代码所产生的任何后果和风险由使用者自行承担，作者不承担任何责任。");
    puts("This code is provided 'as is', without warranty of any kind, express or implied, including but not limited to the warranties of merchantability, fitness for a particular purpose and noninfringement. In no event shall the authors be liable for any claim, damages or other liability, whether in an action of contract, tort or otherwise, arising from, out of or in connection with the code or the use or other dealings in the code.");
    puts("============================================================");
    // 检查参数数量，必须为2（程序名+ROM文件名）
    puts("GBA RTS Patcher - Written by Ausar (Based on Maniac's batteryless patcher)");
    if (argc != 2)
    {
        puts("Usage: patcher <rom.gba>");
        puts("       rom.gba  - GBA ROM file to patch");
        puts("Or: just drag and drop a .gba file onto this executable");
        puts("       The output will be <rom_rts_keypad.gba>");
        puts("最简单的用法是直接拖放一个.gba文件到这个程序上");
        press_any_key();
        return 1;
    }

    // 初始化ROM缓冲区为0x00ff（理论上应该是0xFF，实际写法有点奇怪，但不影响）
    memset(rom, 0x00ff, sizeof rom);

    // 检查文件名后缀，必须为.gba
    size_t romfilename_len = strlen(argv[1]);
    if (romfilename_len < 4 || strcasecmp(argv[1] + romfilename_len - 4, ".gba"))
    {
        puts("File does not have .gba extension.");
        press_any_key();
        return 1;
    }

    // 打开ROM文件
    if (!(romfile = fopen(argv[1], "rb")))
    {
        puts("Could not open input file");
        puts(strerror(errno));
        press_any_key();
        return 1;
    }

    // 读取ROM到内存
    fseek(romfile, 0, SEEK_END);
    romsize = ftell(romfile);

    if (romsize > sizeof rom)
    {
        puts("ROM too large - not a GBA ROM?");
        press_any_key();
        return 1;
    }

    // 检查ROM是否256KB对齐，如果不是则补齐
    if (romsize & 0x3ffff)
    {
        puts("ROM has been trimmed and is misaligned. Padding to 256KB alignment");
        romsize &= ~0x3ffff;
        romsize += 0x40000;
    }

    fseek(romfile, 0, SEEK_SET);
    fread(rom, 1, romsize, romfile);

    // 检查ROM是否已经打过补丁（查找签名字符串）
    if (memfind(rom, romsize, signature, sizeof signature - 1, 4))
    {
        puts("Signature found. ROM already patched!");
        press_any_key();
        return 1;
    }

    // 查找并替换ROM中所有对IRQ handler地址的引用（0x037F00FC -> 0x037F00F4）
    uint8_t old_irq_addr[4] = { 0xfc, 0x7f, 0x00, 0x03 };
    uint8_t new_irq_addr[4] = { 0xf4, 0x7f, 0x00, 0x03 };

    int found_irq = 0;
    for (uint8_t *p = rom; p < rom + romsize; p += 4)
    {
        if (!memcmp(p, old_irq_addr, sizeof old_irq_addr))
        {
            ++found_irq;
            printf("Found a reference to the IRQ handler address at %lx, patching\n", p - rom);
            memcpy(p, new_irq_addr, sizeof new_irq_addr);
        }
    }
    if (!found_irq)
    {
        puts("Could not find any reference to the IRQ handler. Has the ROM already been patched?");
        press_any_key();
        return 1;
    }

    // 存档大小自动检测 - 扫描ROM寻找存档函数签名
    uint32_t detected_save_size = 0x20000; // 默认128KB
    int found_save_function = 0;
    
    printf("Scanning ROM for save function signatures...\n");
    
    for (uint8_t *scan_ptr = rom; scan_ptr < rom + romsize - 64; scan_ptr += 2)
    {
        // 检测SRAM写函数
        if (!memcmp(scan_ptr, write_sram_signature, sizeof write_sram_signature) ||
            !memcmp(scan_ptr, write_sram2_signature, sizeof write_sram2_signature) ||
            !memcmp(scan_ptr, write_sram_ram_signature, sizeof write_sram_ram_signature))
        {
            detected_save_size = 0x8000; // 32KB SRAM
            found_save_function = 1;
            printf("SRAM save function detected at offset %lx - Save size: 32KB\n", scan_ptr - rom);
            break;
        }
        // 检测EEPROM写函数
        else if (!memcmp(scan_ptr, write_eeprom_signature, sizeof write_eeprom_signature) ||
                 !memcmp(scan_ptr, write_eepromv111_signature, sizeof write_eepromv111_signature))
        {
            detected_save_size = 0x2000; // 8KB EEPROM (64kbit)
            found_save_function = 1;
            printf("EEPROM save function detected at offset %lx - Save size: 8KB\n", scan_ptr - rom);
            break;
        }
        // 检测Flash写函数
        else if (!memcmp(scan_ptr, write_flash_signature, sizeof write_flash_signature) ||
                 !memcmp(scan_ptr, write_flash2_signature, sizeof write_flash2_signature))
        {
            detected_save_size = 0x10000; // 64KB Flash
            found_save_function = 1;
            printf("Flash (64KB) save function detected at offset %lx - Save size: 64KB\n", scan_ptr - rom);
            break;
        }
        // 检测Flash 128KB写函数
        else if (!memcmp(scan_ptr, write_flash3_signature, sizeof write_flash3_signature))
        {
            detected_save_size = 0x20000; // 128KB Flash
            found_save_function = 1;
            printf("Flash (128KB) save function detected at offset %lx - Save size: 128KB\n", scan_ptr - rom);
            break;
        }
    }
    
    if (!found_save_function)
    {
        printf("No save function signatures found. Using default size: 128KB\n");
    }

    printf("Final save configuration:\n");
    printf("  Save size: %u KB (0x%X bytes)\n", detected_save_size / 1024, detected_save_size);

    // 查找插入payload的位置：在ROM末尾找到足够的空间放置payload
    int payload_base;
    for (payload_base = romsize - payload_bin_len; payload_base >= 0; payload_base -= 0x40000)
    {
        int is_all_zeroes = 1;
        int is_all_ones = 1;
        for (int i = 0; i < payload_bin_len; ++i)
        {
            if (rom[payload_base+i] != 0)
            {
                is_all_zeroes = 0;
            }
            if (rom[payload_base+i] != 0xFF)
            {
                is_all_ones = 0;
            }
        }
        if (is_all_zeroes || is_all_ones)
        {
            break;
        }
    }
    // 如果没有找到合适空间，则扩展ROM
    if (payload_base < 0)
    {
        puts("ROM too small to install payload.");
        if (romsize + payload_bin_len > 0x2000000)
        {
            puts("ROM already max size. Cannot expand. Cannot install payload");
            press_any_key();
            return 1;
        }
        else
        {
            puts("Expanding ROM");
            romsize += payload_bin_len;
            payload_base = romsize - payload_bin_len;
        }
    }

    printf("Installing payload at offset %x\n", payload_base);
    printf("Payload ROM address: 0x%08X\n", 0x08000000 + payload_base);
    printf("Payload size: %u bytes (0x%X)\n", payload_bin_len, payload_bin_len);
    
    // 拷贝payload_bin到ROM指定位置
    memcpy(rom + payload_base, payload_bin, payload_bin_len);
    struct PayloadHeader *header = (struct PayloadHeader*)&rom[payload_base];
    header->save_size = detected_save_size;

    // 修改ROM入口点，使其跳转到payload中的patched_entrypoint
    // 并将原入口点地址写入payload的original_entrypoint字段（payload.c头部）
    // payload.c: __attribute__((section(".text"))) const uint32_t original_entrypoint = 0x080000c0;
    if (rom[3] != 0xea)
    {
        puts("Unexpected entrypoint instruction");
        press_any_key();
        return 1;
    }
    // 解析原ROM入口点（ARM跳转指令格式）
    unsigned long original_entrypoint_offset = rom[0];
    original_entrypoint_offset |= rom[1] << 8;
    original_entrypoint_offset |= rom[2] << 16;
    unsigned long original_entrypoint_address = 0x08000000 + 8 + (original_entrypoint_offset << 2);
    printf("Original offset was %lx\n", original_entrypoint_offset);
    // 写入payload的original_entrypoint字段
    header->original_entrypoint = original_entrypoint_address;

    // 计算payload中patched_entrypoint的实际ROM地址
    // payload.c: __attribute__((section(".text"))) const uint32_t patched_entrypoint_addr = (uint32_t)patched_entrypoint;
    struct PayloadHeader *payload_header_in_bin = (struct PayloadHeader*)payload_bin;
    unsigned long new_entrypoint_address = 0x08000000 + payload_base + payload_header_in_bin->patched_entrypoint_addr;
    // 修改ROM头部的入口跳转指令，使其跳转到payload的patched_entrypoint
    ((uint32_t*)rom)[0] = 0xea000000 | (new_entrypoint_address - 0x08000008) >> 2;

    // 写入新文件名
    argv[1][strlen(argv[1]) - 4] = '\0'; // 移除.gba扩展名
    char new_filename[FILENAME_MAX];
    sprintf(new_filename, "%s_rts_keypad.gba", argv[1]);

    // 写入补丁后的ROM到新文件
    if (!(outfile = fopen(new_filename, "wb")))
    {
        puts("Could not open output file");
        puts(strerror(errno));
        press_any_key();
        return 1;
    }

    fwrite(rom, 1, romsize, outfile);
    fflush(outfile);

    printf("Patched successfully. Changes written to %s\n", new_filename);
    printf("RTS save: L + R + Start\n");
    printf("RTS load: A + B + Select\n");
    printf("成功打上了RTS补丁。修改已写入 %s\n", new_filename);
    press_any_key();
    return 0;
}
