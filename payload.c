// Payload头部必须在文件开头，patcher需要
#include <stdint.h>
#include <stdbool.h>

// 前向声明
void patched_entrypoint(void);

// Payload头部结构 - 必须在文件最开始,然后必须是强行改为text，否则无法获取相对偏移 
__attribute__((section(".text"))) const uint32_t original_entrypoint = 0x080000c0;//游戏原始的入口点地址，有两个用法，一个是取值，那就是能获取游戏入口点。第二个用法则是取地址，这个地址刚好就是payload的头部地址，可以计算函数表的偏移。
__attribute__((section(".text"))) const uint32_t save_size = 0x20000;//可能会被patcher.c覆盖，目前覆盖值是64KB
__attribute__((section(".text"))) const uint32_t patched_entrypoint_addr = (uint32_t)patched_entrypoint;

// RTS存档标志字符串 - 必须放在.text段
__attribute__((section(".text"))) const char rts_flag_string[] = "EZ-OmegaRTCFILE.";

// IO寄存器恢复列表 - 必须放在.text段（与EZODE一致）
__attribute__((section(".text"), aligned(2))) const uint16_t io_register_list[] = {
    0x0000, 0x0002, 0x0004, 0x0008, 0x000A, 0x000C, 0x000E, 0x0048,
    0x004A, 0x0050, 0x0052,
    0x0084,
    0x0060, 0x0062, 0x0068, 0x0070, 0x0072, 0x0078,
    0x0080, 0x0082, 0x0088,                         // 0x008C, 0x008E,
    0x0090, 0x0092, 0x0094, 0x0096, 0x0098, 0x009A, 0x009C, 0x009E,
    0x00B8, 0x00C4, 0x00D0, 0x00DC,  // DMA
    0x0120, 0x0122, 0x0124, 0x0126, 0x0128, 0x012A, 0x012C, 0x0132, 0x0134,
    0x0140, 0x0150, 0x0154, 0x0200, 0x0204, 0x0208,
    0xFF00  // 结束标记
};

//默认临时存储地址 (EWRAM区域)
//0203FFFF - 0203FE00 = 0x1FF, 512字节的空闲区域，这是认为，至少，EWRAM会有这么多的可用空间
#define SPEND_0x80_ADDR 0x0203FE00
//注：C语言必须用define的方式获取，不能用下面定义的，不然会炸，不知道为什么
asm("spend_0x80:\n"
    ".text\n"
	".word 0x0203FE00");

#define AGB_ROM  ((unsigned char*)0x8000000)
#define AGB_SRAM ((volatile unsigned char*)0xE000000)
#define SRAM_SIZE 64
#define AGB_SRAM_SIZE SRAM_SIZE*1024
#define SRAM_BANK_SEL (*(volatile unsigned short*) 0x09000000)

#define _FLASH_WRITE(pa, pd) { *(((unsigned short *)AGB_ROM)+((pa)/2)) = pd; __asm("nop"); }

// 扇区定义 - 512KB分为8个64KB的扇区
#define SECTOR_SIZE 0x10000    // 64KB per sector
#define TOTAL_SECTORS 8        // 8 sectors total (512KB / 64KB)
#define SRAM_SAVE_SECTOR 7     // 默认使用扇区7保存SRAM
#define EWRAM_START_SECTOR 0   // EWRAM从扇区0开始，占用0-3
#define IWRAM_PALETTE_SECTOR 4 // IWRAM和调色板保存在扇区4
#define VRAM_FRONT_SECTOR 5    // VRAM前64KB保存在扇区5
#define VRAM_BACK_MISC_SECTOR 6 // VRAM后32KB、OAM、IO寄存器等保存在扇区6

// 定义获取相对地址的宏
#define GET_REL_ADDR(symbol, var) \
    asm volatile("adrl %0, " #symbol : "=r"(var))

#define GET_REL_VALUE(symbol, var) \
    asm volatile("ldr %0, " #symbol : "=r"(var))

// Flash函数表结构体
typedef struct {
    uint32_t identify_start;
    uint32_t identify_end;
    uint32_t erase_start;
    uint32_t erase_end;
    uint32_t program_start;
    uint32_t program_end;
} flash_functions_t;

// 前向声明  
void patched_entrypoint(void);
void rts_save(void);
int run_arm_from_ram(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);
void keypad_process(void);

// 前向声明函数
void load_from_flash(void);
void copy_flash_to_sram(uint32_t flash_addr, uint32_t size);
void restore_sram_from_sector(int sector_num);
void erase_all_sectors(int flash_type_index);
void write_sram_to_sector(int sector_num, int flash_type_index);
void save_ewram_to_flash(int flash_type_index);
void save_iwram_palette_to_flash(int flash_type_index);
void save_vram_front_to_flash(int flash_type_index);
void save_vram_back_misc_to_flash(int flash_type_index);
bool check_rts_save_flag(void);

// 汇编入口函数 - 保存寄存器并调用keypad_process
asm(
"    .arm\n"
"    .align\n"
"keypad_irq_handler:\n"
"    adrl r12, spend_0x80\n"
"    ldr r12, [r12]\n"
"    stmia r12!, {r4-r11,sp,lr}\n"
"    mrs r2, SPSR\n"
"    stmia r12!, {r2}\n"
"    \n"
"    push {lr}\n"
"    bl keypad_process\n"
"    pop {pc}\n"
);
/*此时临时缓冲区内容:（与EZODE完全一致）

0x00-0x27 (40字节): IRQ模式下的r4-r11,sp,lr寄存器
0x28-0x2B (4字节): IRQ模式下的SPSR寄存器*/

// C语言版本的按键中断处理程序  
__attribute__((target("arm"))) void keypad_process(void)
{
    // 修正：原始IRQ处理程序地址应该是0x03FFFFF4 (0x04000000-12)
    volatile uint32_t *original_irq_handler = (volatile uint32_t*)0x03FFFFF4;
    
    // 检查按键寄存器 (KEYINPUT: 0x04000130)
    volatile uint16_t *keypad_reg = (volatile uint16_t*)0x04000130;
    uint16_t keypad_value = *keypad_reg;
    
    // 按键组合定义 (GBA按键反相: 按下=0, 未按下=1)
    // L=0x200, R=0x100, START=0x08, SELECT=0x04
    uint16_t lr_start = 0x308;   // L+R+START (存档)
    uint16_t lr_select = 0x304;  // L+R+SELECT (读档)
    
    // 检查是否按下L+R+START (存档)
    bool need_save = (keypad_value & lr_start) == 0;
    bool need_load = (keypad_value & lr_select) == 0;
    if (need_save || need_load) {
        // 硬件寄存器基地址
        volatile uint16_t *hw_base = (volatile uint16_t*)0x04000000;
        
        // 获取spend_0x80地址并设置当前位置
        volatile uint16_t *spend_addr = (volatile uint16_t*)(SPEND_0x80_ADDR + 0x40);
        
        // 保存音频寄存器 (0x4000060-0x4000090) 到 spend_0x80+0x40
        volatile uint16_t *audio_reg = &hw_base[0x0060/2];
        for (int i = 0; i < 24; i++) {  // 48字节 = 24个16位寄存器
            *spend_addr++ = *audio_reg++;
        }
        
        // 保存DMA控制寄存器到spend_0x80+0x72开始
        *spend_addr++ = hw_base[0x00BA/2];  // DMA0CNT_H
        *spend_addr++ = hw_base[0x00C6/2];  // DMA1CNT_H
        *spend_addr++ = hw_base[0x00D2/2];  // DMA2CNT_H
        *spend_addr++ = hw_base[0x00DE/2];  // DMA3CNT_H
        
        // 保存SOUNDCNT_L和SOUNDCNT_X到栈（与EZODE一致）
        uint16_t sound_reg1 = hw_base[0x0080/2];  // SOUNDCNT_L
        uint16_t sound_reg2 = hw_base[0x0084/2];  // SOUNDCNT_X
        
        // 保存并禁用IME（中断主使能）- 与EZODE一致
        uint16_t ime_value = hw_base[0x0208/2];  // 保存IME状态
        hw_base[0x0208/2] = 0;  // 禁用IME
        
        // 禁用音频和DMA
        hw_base[0x0084/2] = 0;  // 禁用sound
        hw_base[0x00BA/2] = 0;  // 禁用DMA0
        hw_base[0x00C6/2] = 0;  // 禁用DMA1
        hw_base[0x00D2/2] = 0;  // 禁用DMA2
        hw_base[0x00DE/2] = 0;  // 禁用DMA3
        
        // 调用存档或读档函数
        if(need_save)
            rts_save();
        else
            load_from_flash();
            
        // 恢复DMA状态（从spend_0x80+0x72读取）
        spend_addr = (volatile uint16_t*)(SPEND_0x80_ADDR + 0x72);
        hw_base[0x00BA/2] = *spend_addr++;  // DMA0CNT_H
        hw_base[0x00C6/2] = *spend_addr++;  // DMA1CNT_H
        hw_base[0x00D2/2] = *spend_addr++;  // DMA2CNT_H
        hw_base[0x00DE/2] = *spend_addr++;  // DMA3CNT_H
        
        // 恢复sound状态
        hw_base[0x0084/2] = sound_reg2;
        hw_base[0x0080/2] = sound_reg1;
        
        // 恢复IME（中断主使能）- 与EZODE一致
        hw_base[0x0208/2] = ime_value;
        
        // 等待按键组合松开
        uint16_t wait_keys = need_save ? lr_start : lr_select;
        while (((*keypad_reg) & wait_keys) == 0) {
            // 空循环等待
        }
    }
    
    // 跳转到原始中断处理程序
    void (*original_handler)(void) = (void (*)(void))(*original_irq_handler);
    original_handler();
}


// C语言版本的补丁入口点（位置无关代码）
__attribute__((target("arm"))) void patched_entrypoint(void)
{
    uint32_t irq_handler_addr, flash_src_addr, save_size_value, original_entry_addr;
    
    // 使用宏获取相对地址，避免GOT依赖
    GET_REL_ADDR(keypad_irq_handler, irq_handler_addr);
    GET_REL_ADDR(flash_save_sector, flash_src_addr);
    GET_REL_VALUE(save_size, save_size_value);
    GET_REL_VALUE(original_entrypoint, original_entry_addr);
    
    // 设置按键中断处理程序到 [0x04000000-4] = 0x03FFFFFC
    volatile uint32_t *irq_vector = (volatile uint32_t*)0x03FFFFFC;
    *irq_vector = irq_handler_addr;
    
    // 锁定369in1 mapper
    *(volatile uint8_t*)(0x0E000000 + 3) = 0x80;
    
    // 从默认扇区恢复SRAM
    // restore_sram_from_sector(SRAM_SAVE_SECTOR);
    
    // 跳转到原始入口点
    asm volatile(
        "mov pc, %0\n"
        :
        : "r"(original_entry_addr)
        : "memory"
    );
}


  __attribute__((target("arm"))) void rts_save(void)
  {
    volatile uint16_t *green_swap_reg = (volatile uint16_t*)0x04000002;
    *green_swap_reg = 1;
    
    // 用C代码获取地址和值，避免GOT依赖
    uint32_t flash_sector_addr, save_size_value, original_entry_addr;
    GET_REL_ADDR(flash_save_sector, flash_sector_addr);
    flash_sector_addr -= 0x08000000;  // 转换为flash内偏移
    GET_REL_VALUE(save_size, save_size_value);
    GET_REL_ADDR(original_entrypoint, original_entry_addr);
    
    // 第一部分：try_flash循环检测（使用结构体）
    int flash_type_index = -1;
    int identify_result = 0;
    
    flash_functions_t *flash_funcs;
    GET_REL_ADDR(flash_fn_table, flash_funcs);
    
    for (int i = 0; ; i++) {
        // 检查是否到达表尾
        if (flash_funcs[i].identify_start == 0) {
            identify_result = 0;
            break;
        }
        
        // 调用identify函数
        uint32_t identify_start = flash_funcs[i].identify_start + original_entry_addr;
        uint32_t identify_end = flash_funcs[i].identify_end + original_entry_addr;
        identify_result = run_arm_from_ram(0, 0, identify_start, identify_end);
        
        if (identify_result != 0) {
            // 找到匹配的flash，记录索引
            flash_type_index = i;
            break;
        }
        // 如果没找到，继续下一个条目（for循环自动递增i）
    }
    
    // 如果找到匹配的flash，执行擦除和写入
    if (identify_result != 0 && flash_type_index >= 0) {
        // 先擦除整个512KB
        erase_all_sectors(flash_type_index);
        
        // 保存原始SRAM到扇区7
        write_sram_to_sector(SRAM_SAVE_SECTOR, flash_type_index);
        
        // 保存EWRAM到扇区0-3
        save_ewram_to_flash(flash_type_index);
        
        // 保存IWRAM和调色板到扇区4
        save_iwram_palette_to_flash(flash_type_index);
        
        // 保存VRAM前64KB到扇区5
        save_vram_front_to_flash(flash_type_index);
        
        // 保存VRAM后半部分、OAM、IO寄存器等到扇区6
        save_vram_back_misc_to_flash(flash_type_index);
        
        // 恢复SRAM为原状
        // restore_sram_from_sector(SRAM_SAVE_SECTOR);
    }
    
    // 禁用绿色交换
    *green_swap_reg = 0;
  }

// 通用的Flash到SRAM复制函数 - 复用patched_entrypoint中的逻辑
__attribute__((target("arm"))) void copy_flash_to_sram(uint32_t flash_addr, uint32_t size)
{
    volatile uint8_t *flash_src = (volatile uint8_t*)flash_addr;
    volatile uint8_t *sram_dst = (volatile uint8_t*)0x0E000000;
    volatile uint8_t *sram_end = sram_dst + size;
    volatile uint16_t *bank_reg = (volatile uint16_t*)0x09000000;
    
    // SRAM初始化循环 - 从patched_entrypoint复用的逻辑
    while (sram_dst < sram_end) {
        // 计算当前bank (根据SRAM地址的bit 16)
        uint16_t current_bank = ((uint32_t)sram_dst >> 16) & 1;
        *bank_reg = current_bank;
        
        // 复制一个字节
        *sram_dst = *flash_src;
        
        // 递增指针
        flash_src++;
        sram_dst++;
    }
    
    // 设置bank为0（为不支持bank的软件）
    *bank_reg = 0;
}

// 读档函数 - 直接从Flash中恢复到内存
__attribute__((target("arm"))) void load_from_flash(void)
{
    // 先检查RTS存档标志
    if (!check_rts_save_flag()) {
        // 标志检查失败，直接返回，不启用绿色交换
        return;
    }
    
    // 启用绿色交换 (GREEN_SWAP: 0x04000002)
    volatile uint16_t *green_swap_reg = (volatile uint16_t*)0x04000002;
    *green_swap_reg = 1;
    
    // 获取Flash基地址
    uint32_t flash_base_addr;
    GET_REL_ADDR(flash_save_sector, flash_base_addr);
    
    // 2. 大块内存恢复 - 使用u32拷贝（参考EZODE的ReadSram）
    // 恢复EWRAM (256KB) - 从扇区0-3
    volatile uint32_t *ewram = (volatile uint32_t*)0x02000000;
    for (int sector = 0; sector < 4; sector++) {
        volatile uint32_t *flash_src = (volatile uint32_t*)(flash_base_addr + (EWRAM_START_SECTOR + sector) * SECTOR_SIZE);
        int word_offset = sector * (SECTOR_SIZE / 4);
        // 使用u32拷贝 (EZODE的ReadSram使用u32)
        for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
            ewram[word_offset + i] = flash_src[i];
        }
    }
    
    // 恢复IWRAM和调色板 - 从扇区4
    volatile uint32_t *flash_sector4 = (volatile uint32_t*)(flash_base_addr + IWRAM_PALETTE_SECTOR * SECTOR_SIZE);
    
    // 恢复IWRAM (32KB) - 使用u32拷贝
    volatile uint32_t *iwram = (volatile uint32_t*)0x03000000;
    for (uint32_t i = 0; i < 0x8000 / 4; i++) {
        iwram[i] = flash_sector4[i];
    }
    
    // 恢复调色板 (1KB) - 使用u32拷贝
    volatile uint32_t *palette = (volatile uint32_t*)0x05000000;
    volatile uint32_t *flash_palette = flash_sector4 + (0x8000 / 4);
    for (uint32_t i = 0; i < 0x400 / 4; i++) {
        palette[i] = flash_palette[i];
    }
    
    // 3. VRAM恢复
    // 恢复VRAM前64KB - 从扇区5 - 使用u32拷贝
    volatile uint32_t *vram = (volatile uint32_t*)0x06000000;
    volatile uint32_t *flash_sector5 = (volatile uint32_t*)(flash_base_addr + VRAM_FRONT_SECTOR * SECTOR_SIZE);
    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        vram[i] = flash_sector5[i];
    }
    
    // 恢复VRAM后32KB和OAM - 从扇区6
    volatile uint32_t *flash_sector6 = (volatile uint32_t*)(flash_base_addr + VRAM_BACK_MISC_SECTOR * SECTOR_SIZE);
    volatile uint32_t *vram_back = (volatile uint32_t*)0x06010000;
    for (uint32_t i = 0; i < 0x8000 / 4; i++) {
        vram_back[i] = flash_sector6[i];
    }
    
    // 恢复OAM (1KB) - 使用u32拷贝
    volatile uint32_t *oam = (volatile uint32_t*)0x07000000;
    volatile uint32_t *flash_oam = flash_sector6 + (0x8000 / 4);
    for (uint32_t i = 0; i < 0x400 / 4; i++) {
        oam[i] = flash_oam[i];
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////
    // 4. 零散数据恢复 - 从扇区6恢复系统模式SP/LR、IO寄存器、spend_0x80等
    ////////////////////////////////////////////////////////////////////////////////////////
    
    // 计算Flash扇区6中各数据的基地址
    volatile uint8_t *flash_sector6_u8 = (volatile uint8_t*)(flash_base_addr + VRAM_BACK_MISC_SECTOR * SECTOR_SIZE);
    
    // 有选择地恢复IO寄存器 - 使用寄存器列表（与EZODE的restore2_IO一致，u16写入）
    volatile uint16_t *io_base = (volatile uint16_t*)0x04000000;
    volatile uint8_t *flash_io = flash_sector6_u8 + 0x9000;
    
    // 获取寄存器列表地址
    uint32_t reg_list_addr;
    GET_REL_ADDR(io_register_list, reg_list_addr);
    const uint16_t *reg_list = (const uint16_t*)reg_list_addr;
    
    // 遍历寄存器列表，恢复指定的寄存器
    for (int i = 0; reg_list[i] != 0xFF00; i++) {
        uint16_t offset = reg_list[i];
        // 从Flash读取2字节（Flash支持直接读取）
        uint16_t value = *(volatile uint16_t*)(flash_io + offset);
        // 写入到IO寄存器（u16写入）
        io_base[offset / 2] = value;
    }
    
    // 额外恢复DMA1控制寄存器 - 从扇区6的0x8500+0x34偏移
    volatile uint16_t *flash_dma = (volatile uint16_t*)(flash_sector6_u8 + 0x8500 + 0x34);
    io_base[0x00C6 / 2] = *flash_dma;  // DMA1CNT_H
    
    // 清零中断标志寄存器 IF (Interrupt Request Flags / IRQ Acknowledge)
    io_base[0x0202 / 2] = 0;
    
    // 恢复spend_0x80区域（0x80字节）- 从扇区6的0x8400偏移 - 使用u32拷贝
    volatile uint32_t *spend = (volatile uint32_t*)SPEND_0x80_ADDR;
    volatile uint32_t *flash_spend = (volatile uint32_t*)(flash_sector6_u8 + 0x8400);
    for (uint32_t i = 0; i < 0x80 / 4; i++) {
        spend[i] = flash_spend[i];
    }
    
    // 恢复系统模式SP和LR寄存器
    // 这需要切换到系统模式，恢复寄存器，然后切换回来
    asm volatile(
        "mrs r0, cpsr\n"                // 保存当前CPSR
        "adrl r7, spend_0x80\n"         // 
        "ldr r7, [r7]\n"               // 读取spend_0x80地址
        "add r7, #0x28\n"          // 偏移到保存SPSR的地址

        "ldmia r7!,{r2}\n"      //现在r7 = 0x28+4 = 0x2C
        "msr spsr_cxsf, r2\n"          // 恢复SPSR irq状态

        "mov r1,#0xDF\n"          // 切换到系统模式
        "msr cpsr_cf, r1\n"
        "nop\n"
        
        "add r7,#0x4\n"          // 现在r7 = 0x2C+4 = 0x30
        "ldmia r7!,{r13-r14}\n" // 恢复r13-r14,即SP和LR寄存器
        
        "msr cpsr_cf, r0\n"          // 恢复CPSR,即，切换回到IRQ模式
        "nop\n"

        :
        : 
        : "r0", "r1", "r2", "r7", "memory"
    );
    
    ////////////////////////////////////////////////////////////////////////////////////////
    // 恢复结束
    ////////////////////////////////////////////////////////////////////////////////////////
    
    // // 从扇区7恢复原始SRAM - 使用run_arm_from_ram从RAM执行
    // // 计算扇区7地址
    // uint32_t sram_sector_addr = flash_base_addr + SRAM_SAVE_SECTOR * SECTOR_SIZE;
    
    // // 调用flash_copy_to_sram函数（在RAM中执行）
    // // 获取flash_copy_to_sram的起始和结束地址
    // uint32_t copy_fn_start, copy_fn_end;
    // GET_REL_ADDR(flash_copy_to_sram_fn_start, copy_fn_start);
    // GET_REL_ADDR(flash_copy_to_sram_fn_end, copy_fn_end);
    
    // // 调用run_arm_from_ram来从Flash恢复SRAM
    // run_arm_from_ram(sram_sector_addr, SECTOR_SIZE, copy_fn_start, copy_fn_end);
    
    // 禁用绿色交换
    *green_swap_reg = 0;
    
    // 恢复IRQ模式的寄存器并跳转到原始中断处理程序
    // 与EZODE实现一致
    asm volatile(
        "adrl r12, spend_0x80\n"
        "ldr r12, [r12]\n"
        "ldmia r12!, {r4-r11,sp,lr}\n"
        "\n"
        "mov r0, #0x04000000\n"
        "ldr pc, [r0, #-(0x04000000-0x03FFFFF4)]\n"  // 跳转到原始IRQ处理程序
        :
        :
        : "r0", "r12", "memory"
    );
}
    
asm(R"(
.align 4
# Flash函数表 - 对应C结构体 flash_fn_table_c[]
flash_fn_table:
# Flash type 1 - flash_functions_t[0]
.word identify_flash_1
.word identify_flash_1_end
.word erase_flash_1
.word erase_flash_1_end
.word program_flash_1
.word program_flash_1_end
# Flash type 4 - flash_functions_t[1]
.word identify_flash_4
.word identify_flash_4_end
.word erase_flash_4
.word erase_flash_4_end
.word program_flash_4
.word program_flash_4_end
# Flash type 2 - flash_functions_t[2]
.word identify_flash_2
.word identify_flash_2_end
.word erase_flash_2
.word erase_flash_2_end
.word program_flash_2
.word program_flash_2_end
# Flash type 3 - flash_functions_t[3]
.word identify_flash_3
.word identify_flash_3_end
.word erase_flash_3
.word erase_flash_3_end
.word program_flash_3
.word program_flash_3_end
# 终止条目 - 12 bytes of zeros - flash_functions_t[4]
.zero 12

)");

// 在RAM中运行ARM函数 - 混合C和汇编实现
__attribute__((target("arm"))) 
int run_arm_from_ram(uint32_t arg0, uint32_t arg1, uint32_t func_start, uint32_t func_end)
{
    int result;
    
    asm volatile(
        "mrs r0, CPSR\n"          // 备份当前CPSR
        "mov r1, #0xDF\n"		//切换到系统模式
        "msr cpsr_cf, r1\n"
        "nop\n"                     // 确保CPSR更新完成
        "push {r0,lr}\n"          // 保存CPSR到栈,注意，此时已经是系统栈

        "mov r4, sp\n"                    // 保存当前栈指针
        
        "run_copy_loop:\n"    
        "ldr r5, [%[end], #-4]!\n"        // 从函数末尾向前复制
        "push {r5}\n"
        "cmp %[start], %[end]\n"
        "bne run_copy_loop\n"
        
        // 设置调用参数
        "mov r0, %[arg0]\n"               // 第一个参数
        "mov r1, %[arg1]\n"               // 第二个参数  
        "mov lr, pc\n"                    // 设置返回地址（PC+8）
        "mov pc, sp\n"                    // 调用ARM函数（直接从栈上执行）
        
        "mov %[result], r0\n"             // 保存返回值
        "mov sp, r4\n"                    // 恢复栈指针(清空拷贝过来的函数代码占的空间)

        "pop {r1,lr}\n"                      // 恢复CPSR
        "msr cpsr_cf, r1\n"               // 恢复CPSR,即，返回之前的模式（IRQ）
        
        : [result] "=r" (result)
        : [arg0] "r" (arg0), [arg1] "r" (arg1), 
          [start] "r" (func_start), [end] "r" (func_end)
        : "r0", "r1", "r2", "r4", "r5", "lr", "memory"
    );
    
    return result;
}


int __attribute__((target("arm"))) identify_flash_1()
{
    unsigned rom_data, data;
	//stop_dma_interrupts();
	rom_data = *(unsigned *)AGB_ROM;
	
	// Type 1 or 4
	_FLASH_WRITE(0, 0xFF);
	_FLASH_WRITE(0, 0x90);
	data = *(unsigned *)AGB_ROM;
	_FLASH_WRITE(0, 0xFF);
	if (rom_data != data) {
		// Check if the chip is responding to this command
		// which then needs a different write command later
		_FLASH_WRITE(0x59, 0x42);
		data = *(unsigned char *)(AGB_ROM+0xB2);
		_FLASH_WRITE(0x59, 0x96);
		_FLASH_WRITE(0, 0xFF);
		if (data != 0x96) {
			//resume_interrupts();
            	
            for (volatile int i = 0; i < 1024; ++i)
                __asm("nop");
            
            
			return 0;
		}
		//resume_interrupts();
		return 1;
	}
    return 0;
}
asm(".align 4\n"
    "identify_flash_1_end:");

void __attribute__((target("arm"))) erase_flash_1(unsigned sa, unsigned size)
{
    // Erase flash sectors based on size
    // Flash type 1 typically has 64KB sectors
    unsigned sector_size = 0x10000; // 64KB
    unsigned end_addr = sa + size;
    
    for (; sa < end_addr; sa += sector_size) {
        _FLASH_WRITE(sa, 0xFF);
        _FLASH_WRITE(sa, 0x60);
        _FLASH_WRITE(sa, 0xD0);
        _FLASH_WRITE(sa, 0x20);
        _FLASH_WRITE(sa, 0xD0);
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0x80) {
                break;
            }
        }
        _FLASH_WRITE(sa, 0xFF);
    }
}
asm(".align 4\n"
    "erase_flash_1_end:");

void __attribute__((target("arm"))) program_flash_1(unsigned sa, unsigned _save_size)
{
    // Write data
    SRAM_BANK_SEL = 0;
    for (unsigned i=0; i<_save_size; i+=2) {
        if (i == AGB_SRAM_SIZE)
            SRAM_BANK_SEL = 1;
        _FLASH_WRITE(sa+i, 0x40);
        _FLASH_WRITE(sa+i, (*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)));
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0x80) {
                break;
            }
        }
    }
    _FLASH_WRITE(sa, 0xFF);
}

asm(".align 4\n"
    "program_flash_1_end:");

int __attribute__((target("arm"))) identify_flash_2()
{
    unsigned rom_data, data;
	//stop_dma_interrupts();
	rom_data = *(unsigned *)AGB_ROM;
    
    _FLASH_WRITE(0, 0xF0);
	_FLASH_WRITE(0xAAA, 0xA9);
	_FLASH_WRITE(0x555, 0x56);
	_FLASH_WRITE(0xAAA, 0x90);
	data = *(unsigned *)AGB_ROM;
	_FLASH_WRITE(0, 0xF0);
	if (rom_data != data) {
		//resume_interrupts();
		return 1;
	}
    return 0;
}
asm(".align 4\n"
    "identify_flash_2_end:");

void __attribute__((target("arm"))) erase_flash_2(unsigned sa, unsigned size)
{
    // Erase flash sectors based on size
    // Flash type 2 typically has 64KB sectors
    unsigned sector_size = 0x10000; // 64KB
    unsigned end_addr = sa + size;
    
    for (; sa < end_addr; sa += sector_size) {
        _FLASH_WRITE(sa, 0xF0);
        _FLASH_WRITE(0xAAA, 0xA9);
        _FLASH_WRITE(0x555, 0x56);
        _FLASH_WRITE(0xAAA, 0x80);
        _FLASH_WRITE(0xAAA, 0xA9);
        _FLASH_WRITE(0x555, 0x56);
        _FLASH_WRITE(sa, 0x30);
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0xFFFF) {
                break;
            }
        }
        _FLASH_WRITE(sa, 0xF0);
    }
}
asm(".align 4\n"
    "erase_flash_2_end:");

void __attribute__((target("arm"))) program_flash_2(unsigned sa, unsigned _save_size)
{
    // Write data
    SRAM_BANK_SEL = 0;
    for (unsigned i=0; i<_save_size; i+=2) {
        if (i == AGB_SRAM_SIZE)
            SRAM_BANK_SEL = 1;
        _FLASH_WRITE(0xAAA, 0xA9);
        _FLASH_WRITE(0x555, 0x56);
        _FLASH_WRITE(0xAAA, 0xA0);
        _FLASH_WRITE(sa+i, (*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)));
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+((sa+i)/2)) == ((*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)))) {
                break;
            }
        }
    }
    _FLASH_WRITE(sa, 0xF0);
}
asm(".align 4\n"
    "program_flash_2_end:");

int __attribute__((target("arm"))) identify_flash_3()
{
    unsigned rom_data, data;
	//stop_dma_interrupts();
	rom_data = *(unsigned *)AGB_ROM;
    
    _FLASH_WRITE(0, 0xF0);
	_FLASH_WRITE(0xAAA, 0xAA);
	_FLASH_WRITE(0x555, 0x55);
	_FLASH_WRITE(0xAAA, 0x90);
	data = *(unsigned *)AGB_ROM;
	_FLASH_WRITE(0, 0xF0);
	if (rom_data != data) {
		//resume_interrupts();
        return 1;
	}
    return 0;
}
asm(".align 4\n"
    "identify_flash_3_end:");

void __attribute__((target("arm"))) erase_flash_3(unsigned sa, unsigned size)
{
    // Erase flash sectors based on size
    // Flash type 3 typically has 64KB sectors
    unsigned sector_size = 0x10000; // 64KB
    unsigned end_addr = sa + size;
    
    for (; sa < end_addr; sa += sector_size) {
        _FLASH_WRITE(sa, 0xF0);
        _FLASH_WRITE(0xAAA, 0xAA);
        _FLASH_WRITE(0x555, 0x55);
        _FLASH_WRITE(0xAAA, 0x80);
        _FLASH_WRITE(0xAAA, 0xAA);
        _FLASH_WRITE(0x555, 0x55);
        _FLASH_WRITE(sa, 0x30);
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+(sa/2)) == 0xFFFF) {
                break;
            }
        }
        _FLASH_WRITE(sa, 0xF0);
    }
}
asm(".align 4\n"
    "erase_flash_3_end:");

void __attribute__((target("arm"))) program_flash_3(unsigned sa, unsigned _save_size)
{
    // Write data
    SRAM_BANK_SEL = 0;
    for (unsigned i=0; i<_save_size; i+=2) {
        if (i == AGB_SRAM_SIZE)
            SRAM_BANK_SEL = 1;
        _FLASH_WRITE(0xAAA, 0xAA);
        _FLASH_WRITE(0x555, 0x55);
        _FLASH_WRITE(0xAAA, 0xA0);
        _FLASH_WRITE(sa+i, (*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)));
        while (1) {
            __asm("nop");
            if (*(((unsigned short *)AGB_ROM)+((sa+i)/2)) == ((*(unsigned char *)(AGB_SRAM+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+i)))) {
                break;
            }
        }
    }
    _FLASH_WRITE(sa, 0xF0);   
}
asm(".align 4\n"
    "program_flash_3_end:");

int __attribute__((target("arm"))) identify_flash_4()
{
    unsigned rom_data, data;
	//stop_dma_interrupts();
	rom_data = *(unsigned *)AGB_ROM;
	
	// Type 1 or 4
	_FLASH_WRITE(0, 0xFF);
	_FLASH_WRITE(0, 0x90);
	data = *(unsigned *)AGB_ROM;
	_FLASH_WRITE(0, 0xFF);
	if (rom_data != data) {
		// Check if the chip is responding to this command
		// which then needs a different write command later
		_FLASH_WRITE(0x59, 0x42);
		data = *(unsigned char *)(AGB_ROM+0xB2);
		_FLASH_WRITE(0x59, 0x96);
		_FLASH_WRITE(0, 0xFF);
		if (data != 0x96) {
			//resume_interrupts();
            
            for (volatile int i = 0; i < 1024; ++i)
                __asm("nop");
            
			return 1;
		}
	}
    return 0;
}
asm(".align 4\n"
    "identify_flash_4_end:");

void __attribute__((target("arm"))) erase_flash_4(unsigned sa, unsigned size)
{
    // Erase flash sectors based on size
    // Flash type 4 typically has 64KB sectors
    unsigned sector_size = 0x10000; // 64KB
    unsigned end_addr = sa + size;
    
    for (; sa < end_addr; sa += sector_size) {
        _FLASH_WRITE(sa, 0xFF);
        _FLASH_WRITE(sa, 0x60);
        _FLASH_WRITE(sa, 0xD0);
        _FLASH_WRITE(sa, 0x20);
        _FLASH_WRITE(sa, 0xD0);
        while (1) {
            __asm("nop");
            if ((*(((unsigned short *)AGB_ROM)+(sa/2)) & 0x80) == 0x80) {
                break;
            }
        }
        _FLASH_WRITE(sa, 0xFF);
        
        for (volatile int i = 0; i < 1024; ++i)
            __asm("nop");
    }
}
asm(".align 4\n"
    "erase_flash_4_end:");

void __attribute__((target("arm")))  program_flash_4(unsigned sa, unsigned )
{
    // Write data
    unsigned c = 0;
    SRAM_BANK_SEL = 0;
    while (c < save_size) {
        if (c == AGB_SRAM_SIZE)
            SRAM_BANK_SEL = 1;
        _FLASH_WRITE(sa+c, 0xEA);
        while (1) {
            __asm("nop");
            if ((*(((unsigned short *)AGB_ROM)+((sa+c)/2)) & 0x80) == 0x80) {
                break;
            }
        }
        _FLASH_WRITE(sa+c, 0x1FF);
        for (int i=0; i<1024; i+=2) {
            _FLASH_WRITE(sa+c+i, (*(unsigned char *)(AGB_SRAM+c+i+1)) << 8 | (*(unsigned char *)(AGB_SRAM+c+i)));
        }
        _FLASH_WRITE(sa+c, 0xD0);
        while (1) {
            __asm("nop");
            if ((*(((unsigned short *)AGB_ROM)+((sa+c)/2)) & 0x80) == 0x80) {
                break;
            }
        }
        _FLASH_WRITE(sa+c, 0xFF);
        c += 1024;
    }
    
    	
    for (volatile int i = 0; i < 1024; ++i)
        __asm("nop");
}
asm(".align 4\n"
    "program_flash_4_end:");

// 扇区操作封装函数
// 擦除整个512KB空间（调用一次即可）
__attribute__((target("arm"))) void erase_all_sectors(int flash_type_index)
{
    // 获取必要的地址
    uint32_t flash_sector_addr, original_entry_addr;
    GET_REL_ADDR(flash_save_sector, flash_sector_addr);
    flash_sector_addr -= 0x08000000;  // 转换为flash内偏移
    GET_REL_ADDR(original_entrypoint, original_entry_addr);
    
    // 获取函数表（使用结构体）
    flash_functions_t *flash_funcs;
    GET_REL_ADDR(flash_fn_table, flash_funcs);
    
    // 获取erase函数地址
    uint32_t erase_start = flash_funcs[flash_type_index].erase_start + original_entry_addr;
    uint32_t erase_end = flash_funcs[flash_type_index].erase_end + original_entry_addr;
    
    // 擦除512KB
    const uint32_t erase_size = 0x80000; // 512KB
    run_arm_from_ram(flash_sector_addr, erase_size, erase_start, erase_end);
}

__attribute__((target("arm"))) uint32_t get_sector_addr(int sector_idx){
    
    // 获取必要的地址
    uint32_t flash_sector_addr;
    GET_REL_ADDR(flash_save_sector, flash_sector_addr);
    uint32_t sector_addr = flash_sector_addr + (sector_idx * SECTOR_SIZE) - 0x08000000;
    return sector_addr;
}
// 写入SRAM到指定的64KB扇区（必须先调用erase_all_sectors）
__attribute__((target("arm"))) void write_sram_to_sector(int sector_idx, int flash_type_index)
{
    uint32_t original_entry_addr;
    GET_REL_ADDR(original_entrypoint, original_entry_addr);
    
    // 获取函数表（使用结构体）
    flash_functions_t *flash_funcs;
    GET_REL_ADDR(flash_fn_table, flash_funcs);
    
    // 获取program函数地址
    uint32_t program_start = flash_funcs[flash_type_index].program_start + original_entry_addr;
    uint32_t program_end = flash_funcs[flash_type_index].program_end + original_entry_addr;
    
    uint32_t sector_addr = get_sector_addr(sector_idx);
    // 写入64KB数据
    run_arm_from_ram(sector_addr, SECTOR_SIZE, program_start, program_end);
}

// 从指定扇区恢复SRAM（64KB）
__attribute__((target("arm"))) void restore_sram_from_sector(int sector_num)
{
    // 验证扇区号
    if (sector_num >= TOTAL_SECTORS) {
        return;
    }
    
    // 计算扇区地址
    uint32_t flash_sector_addr;
    GET_REL_ADDR(flash_save_sector, flash_sector_addr);
    uint32_t sector_flash_addr = flash_sector_addr + (sector_num * SECTOR_SIZE);
    
    // 复制64KB数据
    copy_flash_to_sram(sector_flash_addr, SECTOR_SIZE);
}

// 检查RTS存档标志是否有效
__attribute__((target("arm"))) bool check_rts_save_flag(void)
{
    // 获取spend_0x80地址
    volatile uint8_t *spend = (volatile uint8_t*)SPEND_0x80_ADDR;
    
    // 计算扇区6的flash地址 + 0xFFF0偏移
    uint32_t flash_sector_addr;
    GET_REL_ADDR(flash_save_sector, flash_sector_addr);
    uint32_t flag_flash_addr = flash_sector_addr + (VRAM_BACK_MISC_SECTOR * SECTOR_SIZE) + 0xFFF0;
    
    // 从flash复制16字节标志到spend_0x80
    volatile uint8_t *flash_flag = (volatile uint8_t*)flag_flash_addr;
    for (uint32_t i = 0; i < 16; i++) {
        spend[i] = flash_flag[i];
    }
    
    // 获取RTS标志字符串地址
    uint32_t expected_flag_addr;
    GET_REL_ADDR(rts_flag_string, expected_flag_addr);
    const char *expected_flag = (const char*)expected_flag_addr;
    
    // 检查标志是否匹配
    for (uint32_t i = 0; i < 16; i++) {
        if (spend[i] != expected_flag[i]) {
            return false;
        }
    }
    
    return true;
}

// 保存EWRAM到Flash扇区0-3
__attribute__((target("arm"))) void save_ewram_to_flash(int flash_type_index)
{
    volatile uint8_t *ewram = (volatile uint8_t*)0x02000000;
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;
    
    // EWRAM是256KB，分4个64KB扇区保存
    for (int sector = 0; sector < 4; sector++) {
        // 复制64KB从EWRAM到SRAM（8bit操作）
        int sector_start = sector * SECTOR_SIZE;
        for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
            sram[i] = ewram[sector_start + i];
        }
        // 写入到对应扇区
        write_sram_to_sector(EWRAM_START_SECTOR + sector, flash_type_index);
    }
}

// 保存IWRAM和调色板到Flash扇区4
__attribute__((target("arm"))) void save_iwram_palette_to_flash(int flash_type_index)
{
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;
    
    // 先清空SRAM（因为IWRAM+调色板不会占满64KB）
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
        sram[i] = 0;
    }
    
    // 复制IWRAM (32KB) 到SRAM的0x0000偏移
    volatile uint8_t *iwram = (volatile uint8_t*)0x03000000;
    for (uint32_t i = 0; i < 0x8000; i++) {
        sram[i] = iwram[i];
    }
    
    // 复制调色板 (1KB) 到SRAM的0x8000偏移（与EZODE一致）
    volatile uint8_t *palette = (volatile uint8_t*)0x05000000;
    volatile uint8_t *sram_palette = sram + 0x8000;
    for (uint32_t i = 0; i < 0x400; i++) {
        sram_palette[i] = palette[i];
    }
    
    // 写入到扇区4
    write_sram_to_sector(IWRAM_PALETTE_SECTOR, flash_type_index);
}

// 保存VRAM前64KB到Flash扇区5
__attribute__((target("arm"))) void save_vram_front_to_flash(int flash_type_index)
{
    volatile uint8_t *vram = (volatile uint8_t*)0x06000000;
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;
    
    // 复制VRAM前64KB到SRAM（8bit操作）
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
        sram[i] = vram[i];
    }
    
    // 写入到扇区5
    write_sram_to_sector(VRAM_FRONT_SECTOR, flash_type_index);
}

// 保存VRAM后半部分、OAM、IO寄存器等到Flash扇区6
__attribute__((target("arm"))) void save_vram_back_misc_to_flash(int flash_type_index)
{
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;
    
    // 先清空SRAM
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
        sram[i] = 0;
    }
    
    // 1. 复制VRAM后32KB (0x06010000) 到SRAM的0x0000偏移
    volatile uint8_t *vram_back = (volatile uint8_t*)0x06010000;
    for (uint32_t i = 0; i < 0x8000; i++) {
        sram[i] = vram_back[i];
    }
    
    // 2. 复制OAM (1KB) 到SRAM的0x8000偏移
    volatile uint8_t *oam = (volatile uint8_t*)0x07000000;
    volatile uint8_t *sram_oam = sram + 0x8000;
    for (uint32_t i = 0; i < 0x400; i++) {
        sram_oam[i] = oam[i];
    }
    
    // 3. 保存系统模式的SP和LR到spend_0x80+0x30
    // 必须在一个asm块中完成整个操作
    asm volatile(
        "mrs r0, CPSR\n"            // 备份当前CPSR
        "adrl r7, spend_0x80\n"     
        "ldr r7,[r7]\n"         // 获取spend_0x80的地址
        "add r7,#0x30\n"         // 指向spend_0x80+0x30
        "mov r1, #0xDF\n"           // 系统模式
        "msr cpsr_cf, r1\n"
        "nop\n"
        "mov r6, sp\n"              // 获取系统模式SP
        "stmia r7!, {r6, lr}\n"     // 保存SP和LR到spend_0x80+0x30
        "msr cpsr_cf, r0\n"         // 恢复IRQ模式
        "nop\n"
        : : : "r0", "r1", "r6", "r7", "memory"
    );
    
    // 4. 复制spend_0x80的内容(128字节)到SRAM的0x8400偏移
    volatile uint8_t *spend_src = (volatile uint8_t*)SPEND_0x80_ADDR;
    volatile uint8_t *sram_spend = sram + 0x8400;
    for (uint32_t i = 0; i < 0x80; i++) {
        sram_spend[i] = spend_src[i];
    }
    
    // 5. 复制I/O寄存器0x04000000-0x04000060到SRAM的0x9000偏移
    volatile uint8_t *io_base = (volatile uint8_t*)0x04000000;
    volatile uint8_t *sram_io1 = sram + 0x9000;
    for (uint32_t i = 0; i < 0x60; i++) {
        sram_io1[i] = io_base[i];
    }
    
    // 6. 复制spend_0x80+0x40的内容(0x30字节)到SRAM的0x9060偏移
    volatile uint8_t *spend_audio = (volatile uint8_t*)(SPEND_0x80_ADDR + 0x40);
    volatile uint8_t *sram_audio = sram + 0x9060;
    for (uint32_t i = 0; i < 0x30; i++) {
        sram_audio[i] = spend_audio[i];
    }
    
    // 7. 复制I/O寄存器0x04000090-0x040003FE到SRAM的0x9090偏移
    volatile uint8_t *io_base2 = (volatile uint8_t*)0x04000090;
    volatile uint8_t *sram_io2 = sram + 0x9090;
    for (uint32_t i = 0; i < 0x370; i++) {
        sram_io2[i] = io_base2[i];
    }
    
    // 8. 写入RTS标志字符串到SRAM的0xFFF0偏移
    // 使用GET_REL_ADDR获取字符串地址
    uint32_t flag_addr;
    GET_REL_ADDR(rts_flag_string, flag_addr);
    const char *flag_ptr = (const char*)flag_addr;
    volatile uint8_t *sram_flag = sram + 0xFFF0;
    for (uint32_t i = 0; i < 16; i++) {
        sram_flag[i] = flag_ptr[i];
    }
    
    // 写入到扇区6
    write_sram_to_sector(VRAM_BACK_MISC_SECTOR, flash_type_index);
}

asm(R"(
# The following footer must come last.
.balign 4
.ascii "<3 from Maniac"
# Size of payload
.hword (.+2)
.balign 4
    flash_save_sector:
.end

)");