#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_bin.h"
#include "payload_header.h"

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

FILE *romfile;
FILE *outfile;
FILE *rtsfile;
uint32_t romsize;
uint8_t rom[0x02000000];
char signature[] = "<3 from Maniac";
#define RTS_SIZE (448 * 1024)  // 448KB


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
    // 检查参数数量，必须为2或3（程序名+ROM文件名+可选的RTS文件）
    if (argc != 2 && argc != 3)
    {
        puts("Usage: patcher <rom.gba> [save.rts]");
        puts("       rom.gba  - GBA ROM file to patch");
        puts("       save.rts - Optional 448KB RTS save file to embed");
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
    // 需要预留512KB空间供后续扩展使用
    const int reserved_space = 0x80000; // 512KB
    int payload_base;
    for (payload_base = romsize - reserved_space - payload_bin_len; payload_base >= 0; payload_base -= 0x40000)
    {
        int is_all_zeroes = 1;
        int is_all_ones = 1;
        for (int i = 0; i < reserved_space + payload_bin_len; ++i)
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
        if (romsize + reserved_space > 0x2000000)
        {
            puts("ROM already max size. Cannot expand. Cannot install payload");
            scanf("%*s");
            return 1;
        }
        else
        {
            puts("Expanding ROM");
            romsize += reserved_space;
            payload_base = romsize - reserved_space - payload_bin_len;
        }
    }

    printf("Installing payload at offset %x\n", payload_base);
    printf("Payload ROM address: 0x%08X\n", 0x08000000 + payload_base);
    printf("Payload size: %u bytes (0x%X)\n", payload_bin_len, payload_bin_len);
    
    // 拷贝payload_bin到ROM指定位置
    memcpy(rom + payload_base, payload_bin, payload_bin_len);

    // 设置payload中的save_size字段（见payload.c头部，决定SRAM保存区大小）
    // payload.c: __attribute__((section(".text"))) const uint32_t save_size = 0x20000;
    // 注意：虽然预留了512KB空间，但实际SRAM拷贝仍然只使用64KB
    struct PayloadHeader *header = (struct PayloadHeader*)&rom[payload_base];
    header->save_size = 64*1024;
    //TODO:每个游戏大小都应该不一样的。现在先写死了64KB（512Kb）
    
    // 计算并输出SRAM保存空间的基址（在payload之后）
    uint32_t sram_save_base = payload_base + payload_bin_len;
    printf("SRAM save space offset: 0x%X\n", sram_save_base);
    printf("SRAM save space ROM address: 0x%08X\n", 0x08000000 + sram_save_base);
    printf("Reserved space size: %u KB (0x%X bytes)\n", reserved_space / 1024, reserved_space);
    printf("Actual SRAM copy size: 64 KB (0x10000 bytes)\n");
    
    // 处理可选的RTS文件
    if (argc == 3)
    {
        // 检查RTS文件扩展名
        size_t rtsfilename_len = strlen(argv[2]);
        if (rtsfilename_len < 4 || strcasecmp(argv[2] + rtsfilename_len - 4, ".rts"))
        {
            puts("Second file does not have .rts extension.");
            scanf("%*s");
            return 1;
        }
        
        // 打开RTS文件
        if (!(rtsfile = fopen(argv[2], "rb")))
        {
            puts("Could not open RTS file");
            puts(strerror(errno));
            scanf("%*s");
            return 1;
        }
        
        // 检查文件大小必须是448KB
        fseek(rtsfile, 0, SEEK_END);
        long rtssize = ftell(rtsfile);
        if (rtssize != RTS_SIZE)
        {
            printf("RTS file size must be exactly 448KB (458752 bytes), but got %ld bytes\n", rtssize);
            fclose(rtsfile);
            scanf("%*s");
            return 1;
        }
        
        // 读取RTS文件内容到ROM的存档位置
        fseek(rtsfile, 0, SEEK_SET);
        if (fread(rom + sram_save_base, 1, RTS_SIZE, rtsfile) != RTS_SIZE)
        {
            puts("Failed to read RTS file");
            fclose(rtsfile);
            scanf("%*s");
            return 1;
        }
        fclose(rtsfile);
        
        printf("RTS file embedded successfully at offset 0x%X\n", sram_save_base);
        printf("RTS covers sectors 0-6 (448KB) of the 512KB save space\n");
    }

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
    header->original_entrypoint = original_entrypoint_address;

    // 计算payload中patched_entrypoint的实际ROM地址
    // payload.c: __attribute__((section(".text"))) const uint32_t patched_entrypoint_addr = (uint32_t)patched_entrypoint;
    struct PayloadHeader *payload_header_in_bin = (struct PayloadHeader*)payload_bin;
    unsigned long new_entrypoint_address = 0x08000000 + payload_base + payload_header_in_bin->patched_entrypoint_addr;
    // 修改ROM头部的入口跳转指令，使其跳转到payload的patched_entrypoint
    ((uint32_t*)rom)[0] = 0xea000000 | (new_entrypoint_address - 0x08000008) >> 2;

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
