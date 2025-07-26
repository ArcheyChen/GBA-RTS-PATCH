#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_bin.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

FILE *romfile;
FILE *outfile;
uint32_t romsize;
uint8_t rom[0x02000000];
char signature[] = "<3 from Maniac";

enum payload_offsets {
    ORIGINAL_ENTRYPOINT_ADDR,
    SAVE_SIZE,
    PATCHED_ENTRYPOINT
};


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
    // 检查参数数量，必须为2（程序名+ROM文件名）
    if (argc != 2)
    {
        puts("Wrong number of args. Try dragging and dropping your ROM onto the .exe file in the file browser.");
        scanf("%*s");
        return 1;
    }

    // 初始化ROM缓冲区为0x00ff（理论上应该是0xFF，实际写法有点奇怪，但不影响）
    memset(rom, 0x00ff, sizeof rom);

    // 检查文件名后缀，必须为.gba
    size_t romfilename_len = strlen(argv[1]);
    if (romfilename_len < 4 || strcasecmp(argv[1] + romfilename_len - 4, ".gba"))
    {
        puts("File does not have .gba extension.");
        scanf("%*s");
        return 1;
    }

    // 打开ROM文件
    if (!(romfile = fopen(argv[1], "rb")))
    {
        puts("Could not open input file");
        puts(strerror(errno));
        scanf("%*s");
        return 1;
    }

    // 读取ROM到内存
    fseek(romfile, 0, SEEK_END);
    romsize = ftell(romfile);

    if (romsize > sizeof rom)
    {
        puts("ROM too large - not a GBA ROM?");
        scanf("%*s");
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
        scanf("%*s");
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
        scanf("%*s");
        return 1;
    }

    // 查找插入payload的位置：要求在某个256KB扇区前有一段全0或全0xFF的空间
    int payload_base;
    for (payload_base = romsize - 0x40000 - payload_bin_len; payload_base >= 0; payload_base -= 0x40000)
    {
        int is_all_zeroes = 1;
        int is_all_ones = 1;
        for (int i = 0; i < 0x40000 + payload_bin_len; ++i)
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
        if (romsize + 0x80000 > 0x2000000)
        {
            puts("ROM alraedy max size. Cannot expand. Cannot install payload");
            scanf("%*s");
            return 1;
        }
        else
        {
            puts("Expanding ROM");
            romsize += 0x80000;
            payload_base = romsize - 0x40000 - payload_bin_len;
        }
    }

    printf("Installing payload at offset %x\n", payload_base);
    // 拷贝payload_bin到ROM指定位置
    memcpy(rom + payload_base, payload_bin, payload_bin_len);

    // 设置payload中的save_size字段（见payload.c头部，决定SRAM保存区大小）
    // payload.c: __attribute__((section(".text"))) const uint32_t save_size = 0x20000;
    // 这里将其覆盖为0x8000（32KB，常见SRAM大小）
    SAVE_SIZE[(uint32_t*) &rom[payload_base]] = 0x8000;

    // 修改ROM入口点，使其跳转到payload中的patched_entrypoint
    // 并将原入口点地址写入payload的original_entrypoint字段（payload.c头部）
    // payload.c: __attribute__((section(".text"))) const uint32_t original_entrypoint = 0x080000c0;
    if (rom[3] != 0xea)
    {
        puts("Unexpected entrypoint instruction");
        scanf("%*s");
        return 1;
    }
    // 解析原ROM入口点（ARM跳转指令格式）
    unsigned long original_entrypoint_offset = rom[0];
    original_entrypoint_offset |= rom[1] << 8;
    original_entrypoint_offset |= rom[2] << 16;
    unsigned long original_entrypoint_address = 0x08000000 + 8 + (original_entrypoint_offset << 2);
    printf("Original offset was %lx\n", original_entrypoint_offset);
    // 写入payload的original_entrypoint字段
    ORIGINAL_ENTRYPOINT_ADDR[(uint32_t*) &rom[payload_base]] = original_entrypoint_address;

    // 计算payload中patched_entrypoint的实际ROM地址
    // payload.c: __attribute__((section(".text"))) const uint32_t patched_entrypoint_addr = (uint32_t)patched_entrypoint;
    unsigned long new_entrypoint_address = 0x08000000 + payload_base + PATCHED_ENTRYPOINT[(uint32_t*) payload_bin];
    // 修改ROM头部的入口跳转指令，使其跳转到payload的patched_entrypoint
    0[(uint32_t*) rom] = 0xea000000 | (new_entrypoint_address - 0x08000008) >> 2;

    // 写入新文件名，后缀为_keypad.gba
    char *suffix = "_keypad.gba";
    size_t suffix_length = strlen(suffix);
    char new_filename[FILENAME_MAX];
    strncpy(new_filename, argv[1], FILENAME_MAX);
    strncpy(new_filename + romfilename_len - 4, suffix, strlen(suffix));

    // 写入补丁后的ROM到新文件
    if (!(outfile = fopen(new_filename, "wb")))
    {
        puts("Could not open output file");
        puts(strerror(errno));
        scanf("%*s");
        return 1;
    }

    fwrite(rom, 1, romsize, outfile);
    fflush(outfile);

    printf("Patched successfully (keypad mode). Changes written to %s\n", new_filename);
    // scanf("%*s");
    return 0;
}
