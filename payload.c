
// Payload头部必须在文件开头，patcher需要
#include <stdint.h>
#include <stdbool.h>
#include "payload_header.h"

// 前向声明
void patched_entrypoint(void);

// Payload头部结构 - 必须在文件最开始,然后必须是强行改为text，否则无法获取相对偏移
__attribute__((section(".text"))) const struct PayloadHeader payload_header = {
    .original_entrypoint = 0x080000c0,
    .ctrl_flag = 0xF0,                 // 控制标志 留作备用，0xF0代表这是RTS
    .rts_size = 0x80000,               // RTS(包含存档)大小
    .save_size = 0x10000,              // 存档大小
    .wbuf_size = 0,              // 写缓冲区大小
    .patched_entrypoint_addr = (uint32_t)patched_entrypoint
};

#define LOAD_KEYS 0x308  
// L+R+START (存档)
#define SAVE_KEYS 0x304  
// L+R+SELECT (读档)

// keypad_process中手动保存的寄存器结构体
typedef struct {
    uint16_t dma_control[4];    // DMA0CNT_H, DMA1CNT_H, DMA2CNT_H, DMA3CNT_H
    uint16_t sound_regs[2];     // SOUNDCNT_L, SOUNDCNT_X
} __attribute__((packed)) rts_temp_regs_t;

// RTS存档标志字符串 - 必须放在.text段
__attribute__((section(".text"))) const char rts_flag_string[] = "Ausar'S-RTSFILE.";

// IO寄存器恢复列表 - 必须放在.text段（与EZODE一致）
__attribute__((section(".text"), aligned(2))) const uint16_t io_register_list[] = {
    // LCD控制寄存器
    0x0000,  // DISPCNT   - LCD Control
    0x0002,  // Green Swap - Undocumented
    0x0004,  // DISPSTAT  - General LCD Status
    0x0008,  // BG0CNT    - BG0 Control
    0x000A,  // BG1CNT    - BG1 Control
    0x000C,  // BG2CNT    - BG2 Control
    0x000E,  // BG3CNT    - BG3 Control
    
    // 窗口控制
    0x0048,  // WININ     - Inside of Window 0 and 1
    0x004A,  // WINOUT    - Inside of OBJ Window & Outside
    
    // 特效控制
    0x0050,  // BLDCNT    - Color Special Effects Selection
    0x0052,  // BLDALPHA  - Alpha Blending Coefficients
    
    // 音频寄存器
    0x0084,  // SOUNDCNT_X - Control Sound on/off (NR52)
    0x0060,  // SOUND1CNT_L - Channel 1 Sweep register
    0x0062,  // SOUND1CNT_H - Channel 1 Duty/Length/Envelope
    0x0068,  // SOUND2CNT_L - Channel 2 Duty/Length/Envelope
    0x0070,  // SOUND3CNT_L - Channel 3 Stop/Wave RAM select
    0x0072,  // SOUND3CNT_H - Channel 3 Length/Volume
    0x0078,  // SOUND4CNT_L - Channel 4 Length/Envelope
    0x0080,  // SOUNDCNT_L  - Control Stereo/Volume/Enable
    0x0082,  // SOUNDCNT_H  - Control Mixing/DMA Control
    0x0088,  // SOUNDBIAS   - Sound PWM Control
    
    // Wave RAM
    0x0090, 0x0092, 0x0094, 0x0096,  // WAVE_RAM bank 0
    0x0098, 0x009A, 0x009C, 0x009E,  // WAVE_RAM bank 1
    
    // DMA控制寄存器
    0x00B8,  // DMA0CNT_L - DMA 0 Word Count
    0x00C4,  // DMA1CNT_L - DMA 1 Word Count
    0x00D0,  // DMA2CNT_L - DMA 2 Word Count
    0x00DC,  // DMA3CNT_L - DMA 3 Word Count
    
    // 串行通信
    0x0120,  // SIODATA32/SIOMULTI0 - SIO Data
    0x0122,  // SIOMULTI1 - SIO Data 1
    0x0124,  // SIOMULTI2 - SIO Data 2
    0x0126,  // SIOMULTI3 - SIO Data 3
    0x0128,  // SIOCNT    - SIO Control Register
    0x012A,  // SIOMLT_SEND/SIODATA8 - SIO Data
    0x012C,  // 未使用，但包含在列表中
    0x0132,  // KEYCNT    - Key Interrupt Control
    0x0134,  // RCNT      - SIO Mode Select
    
    // JOY总线
    0x0140,  // JOYCNT    - SIO JOY Bus Control
    0x0150,  // JOY_RECV  - SIO JOY Bus Receive Data
    0x0154,  // JOY_TRANS - SIO JOY Bus Transmit Data
    
    // 中断和电源控制
    0x0200,  // IE        - Interrupt Enable Register
    0x0204,  // WAITCNT   - Game Pak Waitstate Control
    0x0208,  // IME       - Interrupt Master Enable Register
    
    /* ============================================================
     * 以下是额外添加的寄存器，EZODE原版没有包含
     * 这些可能会提高某些游戏的兼容性，但也可能引入问题
     * ============================================================ */
    
    // 背景滚动偏移 - 保持画面位置不变
    0x0010,  // BG0HOFS   - BG0 X-Offset
    0x0012,  // BG0VOFS   - BG0 Y-Offset
    0x0014,  // BG1HOFS   - BG1 X-Offset
    0x0016,  // BG1VOFS   - BG1 Y-Offset
    0x0018,  // BG2HOFS   - BG2 X-Offset
    0x001A,  // BG2VOFS   - BG2 Y-Offset
    0x001C,  // BG3HOFS   - BG3 X-Offset
    0x001E,  // BG3VOFS   - BG3 Y-Offset
    
    // 背景变换参数 - Mode 1/2 游戏需要
    0x0020,  // BG2PA     - BG2 Rotation/Scaling Parameter A
    0x0022,  // BG2PB     - BG2 Rotation/Scaling Parameter B
    0x0024,  // BG2PC     - BG2 Rotation/Scaling Parameter C
    0x0026,  // BG2PD     - BG2 Rotation/Scaling Parameter D
    // 注意: BG2X/Y (0x28/0x2C) 是32位寄存器，需要特殊处理
    0x0030,  // BG3PA     - BG3 Rotation/Scaling Parameter A
    0x0032,  // BG3PB     - BG3 Rotation/Scaling Parameter B
    0x0034,  // BG3PC     - BG3 Rotation/Scaling Parameter C
    0x0036,  // BG3PD     - BG3 Rotation/Scaling Parameter D
    // 注意: BG3X/Y (0x38/0x3C) 是32位寄存器，需要特殊处理
    
    // 窗口尺寸 - 保持窗口特效
    0x0040,  // WIN0H     - Window 0 Horizontal Dimensions
    0x0042,  // WIN1H     - Window 1 Horizontal Dimensions
    0x0044,  // WIN0V     - Window 0 Vertical Dimensions
    0x0046,  // WIN1V     - Window 1 Vertical Dimensions
    
    // 特效参数
    0x004C,  // MOSAIC    - Mosaic Size
    0x0054,  // BLDY      - Brightness (Fade-In/Out) Coefficient
    
    /* ============================================================
     * 额外添加部分结束
     * ============================================================ */
    
    0xFF00   // 结束标记
};

/* 未包含在恢复列表中的IO寄存器：
 * 
 * LCD寄存器：
 * 0x0006  VCOUNT    - Vertical Counter (只读，不需要恢复)
 * 0x0028  BG2X      - BG2 Reference Point X-Coordinate (4字节，需要特殊处理)
 * 0x002C  BG2Y      - BG2 Reference Point Y-Coordinate (4字节，需要特殊处理)
 * 0x0038  BG3X      - BG3 Reference Point X-Coordinate (4字节，需要特殊处理)
 * 0x003C  BG3Y      - BG3 Reference Point Y-Coordinate (4字节，需要特殊处理)
 * 
 * 音频寄存器：
 * 0x0064  SOUND1CNT_X - Channel 1 Frequency/Control
 * 0x006C  SOUND2CNT_H - Channel 2 Frequency/Control
 * 0x0074  SOUND3CNT_X - Channel 3 Frequency/Control
 * 0x007C  SOUND4CNT_H - Channel 4 Frequency/Control
 * 0x00A0  FIFO_A    - Channel A FIFO, Data 0-3 (只写)
 * 0x00A4  FIFO_B    - Channel B FIFO, Data 0-3 (只写)
 * 
 * DMA寄存器：
 * 0x00B0  DMA0SAD   - DMA 0 Source Address (4字节)
 * 0x00B4  DMA0DAD   - DMA 0 Destination Address (4字节)
 * 0x00BA  DMA0CNT_H - DMA 0 Control (在keypad_process中单独保存)
 * 0x00BC  DMA1SAD   - DMA 1 Source Address (4字节)
 * 0x00C0  DMA1DAD   - DMA 1 Destination Address (4字节)
 * 0x00C6  DMA1CNT_H - DMA 1 Control (在keypad_process中单独保存)
 * 0x00C8  DMA2SAD   - DMA 2 Source Address (4字节)
 * 0x00CC  DMA2DAD   - DMA 2 Destination Address (4字节)
 * 0x00D2  DMA2CNT_H - DMA 2 Control (在keypad_process中单独保存)
 * 0x00D4  DMA3SAD   - DMA 3 Source Address (4字节)
 * 0x00D8  DMA3DAD   - DMA 3 Destination Address (4字节)
 * 0x00DE  DMA3CNT_H - DMA 3 Control (在keypad_process中单独保存)
 * 
 * 定时器寄存器：
 * 0x0100  TM0CNT_L  - Timer 0 Counter/Reload
 * 0x0102  TM0CNT_H  - Timer 0 Control
 * 0x0104  TM1CNT_L  - Timer 1 Counter/Reload
 * 0x0106  TM1CNT_H  - Timer 1 Control
 * 0x0108  TM2CNT_L  - Timer 2 Counter/Reload
 * 0x010A  TM2CNT_H  - Timer 2 Control
 * 0x010C  TM3CNT_L  - Timer 3 Counter/Reload
 * 0x010E  TM3CNT_H  - Timer 3 Control
 * 
 * 按键输入：
 * 0x0130  KEYINPUT  - Key Status (只读)
 * 
 * 串行通信（部分）：
 * 0x0136  IR        - Infrared Register (仅原型机)
 * 0x0158  JOYSTAT   - SIO JOY Bus Receive Status (只读)
 * 
 * 中断和电源：
 * 0x0202  IF        - Interrupt Request Flags (在恢复时清零)
 * 0x0300  POSTFLG   - Post Boot Flag
 * 0x0301  HALTCNT   - Power Down Control (只写)
 * 0x0410  未知      - Undocumented
 * 0x0800  内部内存控制
 */

//默认临时存储地址 (EWRAM区域)
//0203FFFF - 0203FE00 = 0x1FF, 512字节的空闲区域，这是认为，至少，EWRAM会有这么多的可用空间
//注：现在cpu_regs地址通过函数参数传递，改为栈分配，不再使用固定地址

#define AGB_ROM  ((unsigned char*)0x8000000)
#define AGB_SRAM ((volatile unsigned char*)0xE000000)
#define SRAM_SIZE 64
#define AGB_SRAM_SIZE SRAM_SIZE*1024
#define SRAM_BANK_SEL (*(volatile unsigned short*) 0x09000000)


// 扇区定义 - 512KB SRAM分为8个64KB的Bank
// Bank 0: 原始游戏SRAM（不动）
// Bank 1-7: RTS数据
#define SECTOR_SIZE 0x10000    // 64KB per bank
#define TOTAL_SECTORS 8        // 8 banks total (512KB / 64KB)
#define EWRAM_START_SECTOR 1   // EWRAM从Bank 1开始，占用1-4
#define VRAM_FRONT_SECTOR 5    // VRAM前64KB保存在Bank 5
#define IWRAM_PALETTE_SECTOR 6 // IWRAM和调色板保存在Bank 6
#define VRAM_BACK_MISC_SECTOR 7 // VRAM后32KB、OAM、IO寄存器等保存在Bank 7

// 定义获取相对地址的宏
#define GET_REL_ADDR(symbol, var) \
    asm volatile("adrl %0, " #symbol : "=r"(var))

#define GET_REL_VALUE(symbol, var) \
    asm volatile("ldr %0, " #symbol : "=r"(var))


// 前向声明
void patched_entrypoint(void);
uint32_t init_before_game(void);
void rts_save(rts_temp_regs_t *rts_regs, volatile uint8_t *cpu_regs_addr);
void keypad_process(volatile uint8_t *cpu_regs_addr);

// 前向声明函数
void load_from_flash();
void save_ewram_to_flash(int flash_type_index);
void save_iwram_vram_back_to_flash(int flash_type_index);
void save_vram_front_to_flash(int flash_type_index);
void save_misc_to_flash(int flash_type_index, rts_temp_regs_t *rts_regs, volatile uint8_t *cpu_regs_addr);
bool check_rts_save_flag(void);

// 裸汇编入口函数 - 调用init_before_game然后跳转到原始入口点
__attribute__((naked, target("arm"))) void patched_entrypoint(void)
{
    asm volatile(
        "push {lr}\n"
        "bl init_before_game\n"          // 调用初始化函数
        "pop {lr}\n"
        "mov pc, r0\n"                   // 跳转到init函数告诉的原始入口点地址
    );
}

// 裸汇编中断处理函数 - 参考EZODE的RTS_irq实现
__attribute__((naked, target("arm"))) void keypad_irq_handler(void)
{
    asm volatile(
        "push {r0,lr}\n"
        "bl detect_keys\n"        // 检测按键
        "cmp r0, #1\n"
        "pop {r0,lr}\n"         //r0比较完成之后，就可以恢复最初的0x04000000了
        "beq call_handler\n"
        "ldr pc, [r0, #-(0x04000000-0x03FFFFF4)]\n" // 跳转到原始IRQ处理程序

        "call_handler:\n"
        // 第一步：立即保存SPSR，因为模式切换会改变它
        "mrs r0, SPSR\n"                 // r0 = SPSR (必须最先保存)
        "mrs r1, CPSR\n"                 // r1 = 当前CPSR (IRQ模式)
        
        // 第二步：切换到系统模式分配栈空间
        "mov r2, #0xDF\n"                // 系统模式
        "msr cpsr_cf, r2\n"
        "nop\n"
        "sub sp, sp, #52\n"              // 在系统栈上分配52字节
        "mov r2, sp\n"                   // r2 = 系统栈缓冲区地址
        
        // 第三步：切换回IRQ模式保存IRQ寄存器
        "msr cpsr_cf, r1\n"              // 恢复IRQ模式 
        "nop\n"
        
        
        // 第四步：保存IRQ模式寄存器到系统栈缓冲区
        "mov r12, r2\n"                  // r12 = 缓冲区地址
        "stmia r12!, {r0}\n"             // 保存SPSR(r0)
        "stmia r12!, {r4-r11,sp,lr}\n"   // 保存r4-r11, IRQ的sp,lr
        "\n"
        
        // 保存LR，因为BL调用会破坏它
        "push {lr}\n"

        // 第五步：再次切换到系统模式保存系统模式的SP和LR
        "mov r3, #0xDF\n"                // 系统模式
        "msr cpsr_cf, r3\n"
        "nop\n"
        "mov r0, sp\n"                   // r0 = 系统模式SP (注意：已经减了52)
        "add r0, r0, #52\n"              // 恢复原始系统SP值
        "add r3, r2, #0x2C\n"            // r3 = 缓冲区 + 0x2C偏移
        "stmia r3!, {r0, lr}\n"          // 保存系统模式原始SP和LR
        
        // 第六步：切换回IRQ模式调用keypad_process
        "msr cpsr_cf, r1\n"              // 切换回IRQ模式
        "nop\n"
        
        // 调用keypad_process
        "mov r0, r2\n"                   // r0 = 缓冲区地址作为参数
        "bl keypad_process\n"            // keypad_process会处理原始IRQ调用
        "\n"
        // 第七步：返回后恢复栈并返回BIOS
        "mrs r1, CPSR\n"                 // r1 = 当前CPSR (IRQ模式)
        "mov r3, #0xDF\n"                // 切换到系统模式恢复栈
        "msr cpsr_cf, r3\n"
        "nop\n"
        "add sp, sp, #52\n"              // 恢复系统栈指针
        "msr cpsr_cf, r1\n"              // 直接恢复原CPSR (IRQ模式)
        "nop\n"
        
        "pop {pc}\n"                     // 返回到BIOS
    );
}
/*此时临时缓冲区内容:（系统栈分配，52字节）

0x00 (4字节): IRQ模式下的SPSR寄存器
0x04 (32字节): IRQ模式下的r4-r11寄存器  
0x24 (4字节): IRQ模式下的sp寄存器
0x28 (4字节): IRQ模式下的lr寄存器
0x2C (4字节): 系统模式的SP（原始值）
0x30 (4字节): 系统模式的LR
总共使用: 4+32+4+4+4+4 = 52字节
缓冲区分配在系统模式栈上，避免IRQ栈溢出
音频寄存器在save_misc_to_flash中直接从硬件读取并写入SRAM
*/
__attribute__((target("arm"))) int detect_keys(void)
{
    uint16_t keypad_value = *((volatile uint16_t*)0x04000130);
    if ((keypad_value & LOAD_KEYS) == 0 || (keypad_value & SAVE_KEYS) == 0) {
        // 检测到L+R+START或L+R+SELECT按键组合
        return 1;
    } else {
        return 0;
    }
}

// C语言版本的按键中断处理程序  
__attribute__((target("arm"))) void keypad_process(volatile uint8_t *cpu_regs_addr)
{
    // 检查按键寄存器 (KEYINPUT: 0x04000130)
    volatile uint16_t *keypad_reg = (volatile uint16_t*)0x04000130;
    uint16_t keypad_value = (*keypad_reg);

    // 检查是否按下L+R+START (存档)
    bool need_save = (keypad_value & LOAD_KEYS) == 0;
    bool need_load = (keypad_value & SAVE_KEYS) == 0;
    if (need_save || need_load) {
        // 硬件寄存器基地址
        volatile uint16_t *hw_base = (volatile uint16_t*)0x04000000;
        
        // 使用结构体保存DMA和音频寄存器
        rts_temp_regs_t rts_temp_regs;
        rts_temp_regs.dma_control[0] = hw_base[0x00BA/2];  // DMA0CNT_H
        rts_temp_regs.dma_control[1] = hw_base[0x00C6/2];  // DMA1CNT_H
        rts_temp_regs.dma_control[2] = hw_base[0x00D2/2];  // DMA2CNT_H
        rts_temp_regs.dma_control[3] = hw_base[0x00DE/2];  // DMA3CNT_H
        rts_temp_regs.sound_regs[0] = hw_base[0x0080/2];   // SOUNDCNT_L
        rts_temp_regs.sound_regs[1] = hw_base[0x0084/2];   // SOUNDCNT_X
        
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
            rts_save(&rts_temp_regs, cpu_regs_addr);
        else
            load_from_flash();
            
        // 恢复DMA状态（从结构体）
        hw_base[0x00BA/2] = rts_temp_regs.dma_control[0];  // DMA0CNT_H
        hw_base[0x00C6/2] = rts_temp_regs.dma_control[1];  // DMA1CNT_H
        hw_base[0x00D2/2] = rts_temp_regs.dma_control[2];  // DMA2CNT_H
        hw_base[0x00DE/2] = rts_temp_regs.dma_control[3];  // DMA3CNT_H
        
        // 恢复sound状态
        hw_base[0x0084/2] = rts_temp_regs.sound_regs[1];
        hw_base[0x0080/2] = rts_temp_regs.sound_regs[0];
        
        // 恢复IME（中断主使能）- 与EZODE一致
        hw_base[0x0208/2] = ime_value;
        
        // 等待按键组合松开
        uint16_t wait_keys = need_save ? LOAD_KEYS : SAVE_KEYS;
        while (((*keypad_reg) & wait_keys) == 0) {
            // 空循环等待
        }
    }
    
    // 修正：原始IRQ处理程序地址应该是0x03FFFFF4 (0x04000000-12)
    volatile uint32_t *original_irq_handler = (volatile uint32_t*)0x03FFFFF4;
    // 跳转到原始中断处理程序
    void (*original_handler)(uint32_t) = (void (*)(uint32_t))(*original_irq_handler);
    original_handler(0x40000000);  // 传递IRQ寄存器地址 (0x04000000)
}


// C语言版本的游戏初始化函数，会通过返回值告知原始入口点
__attribute__((target("arm"))) uint32_t init_before_game(void)
{
    // 使用宏获取相对地址，避免GOT依赖
    uint32_t irq_handler_addr;
    GET_REL_ADDR(keypad_irq_handler, irq_handler_addr);

    struct PayloadHeader *header = (struct PayloadHeader*)&payload_header;
    GET_REL_ADDR(payload_header, header);
    
    // 设置按键中断处理程序到 [0x04000000-4] = 0x03FFFFFC
    volatile uint32_t *irq_vector = (volatile uint32_t*)0x03FFFFFC;
    *irq_vector = irq_handler_addr;
    
    // 锁定369in1 mapper
    *(volatile uint8_t*)(0x0E000000 + 3) = 0x80;
    
    // 从默认扇区恢复SRAM 如果是需要免电池存档那就需要
    // restore_sram_from_sector(SRAM_SAVE_SECTOR);
    
    // 返回原始入口点地址
    return header->original_entrypoint;
}


  __attribute__((target("arm"))) void rts_save(rts_temp_regs_t *rts_regs, volatile uint8_t *cpu_regs_addr)
  {
    volatile uint16_t *green_swap_reg = (volatile uint16_t*)0x04000002;
    *green_swap_reg = 1;

    struct PayloadHeader *header;
    GET_REL_ADDR(payload_header, header);

    // 改为使用SRAM Bank切换保存数据
    // Bank 0保持不动（原始游戏SRAM）

    // 保存EWRAM到Bank 1-4
    save_ewram_to_flash(0);  // flash_type_index参数现在无用，传0

    // 保存VRAM前64KB到Bank 5
    save_vram_front_to_flash(0);

    // 保存IWRAM和VRAM后半部分到Bank 6
    save_iwram_vram_back_to_flash(0);

    // 保存调色板、OAM、IO寄存器等到Bank 7
    save_misc_to_flash(0, rts_regs, cpu_regs_addr);

    // 确保恢复到Bank 0，让游戏能正常访问SRAM
    SRAM_BANK_SEL = 0;

    // 禁用绿色交换
    *green_swap_reg = 0;
  }


// 读档函数 - 直接从Flash中恢复到内存
__attribute__((target("arm"))) void load_from_flash()
{
    // 先检查RTS存档标志
    if (!check_rts_save_flag()) {
        // 标志检查失败，直接返回，不启用绿色交换
        return;
    }

    // 启用绿色交换 (GREEN_SWAP: 0x04000002)
    volatile uint16_t *green_swap_reg = (volatile uint16_t*)0x04000002;
    *green_swap_reg = 1;

    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;

    // 2. 大块内存恢复 - 使用SRAM Bank切换
    // 恢复EWRAM (256KB) - 从Bank 1-4
    // EWRAM支持32位访问，可以优化性能
    volatile uint32_t *ewram = (volatile uint32_t*)0x02000000;
    for (int bank = 0; bank < 4; bank++) {
        // 切换到对应的SRAM Bank
        SRAM_BANK_SEL = EWRAM_START_SECTOR + bank;

        int word_offset = bank * (SECTOR_SIZE / 4);
        // 从SRAM读取并组装成32位写入EWRAM（提高性能）
        for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
            uint32_t value = sram[i*4] |
                           (sram[i*4+1] << 8) |
                           (sram[i*4+2] << 16) |
                           (sram[i*4+3] << 24);
            ewram[word_offset + i] = value;
        }
    }

    // 恢复IWRAM和VRAM后半部分 - 从Bank 6
    SRAM_BANK_SEL = IWRAM_PALETTE_SECTOR;

    // IWRAM已经在ASM中恢复了，这里恢复VRAM后32KB
    // VRAM必须用U16写入，从SRAM读取U8后组装
    volatile uint16_t *vram_back = (volatile uint16_t*)0x06010000;
    volatile uint8_t *sram_vram_back = sram + 0x8000;
    for (uint32_t i = 0; i < 0x8000 / 2; i++) {
        uint16_t value = sram_vram_back[i*2] | (sram_vram_back[i*2+1] << 8);
        vram_back[i] = value;
    }

    // 3. VRAM恢复
    // 恢复VRAM前64KB - 从Bank 5
    SRAM_BANK_SEL = VRAM_FRONT_SECTOR;
    // VRAM必须用U16写入，从SRAM读取U8后组装
    volatile uint16_t *vram = (volatile uint16_t*)0x06000000;
    for (uint32_t i = 0; i < SECTOR_SIZE / 2; i++) {
        uint16_t value = sram[i*2] | (sram[i*2+1] << 8);
        vram[i] = value;
    }

    // 恢复调色板和OAM - 从Bank 7
    SRAM_BANK_SEL = VRAM_BACK_MISC_SECTOR;

    // 恢复调色板 (1KB) - Palette RAM需要16/32位写入
    // 从SRAM读取(u8)，组装后写入(u16)
    volatile uint16_t *palette = (volatile uint16_t*)0x05000000;
    for (uint32_t i = 0; i < 0x400 / 2; i++) {
        uint16_t value = sram[i*2] | (sram[i*2+1] << 8);
        palette[i] = value;
    }

    // 恢复OAM (1KB) - OAM需要16/32位写入
    // 从SRAM读取(u8)，组装后写入(u16)
    volatile uint16_t *oam = (volatile uint16_t*)0x07000000;
    volatile uint8_t *sram_oam = sram + 0x8000;
    for (uint32_t i = 0; i < 0x400 / 2; i++) {
        uint16_t value = sram_oam[i*2] | (sram_oam[i*2+1] << 8);
        oam[i] = value;
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    // 4. 零散数据恢复 - 从Bank 7恢复系统模式SP/LR、IO寄存器、cpu_regs等
    ////////////////////////////////////////////////////////////////////////////////////////

    // Bank 7已经设置好了，直接使用
    volatile uint8_t *sram_bank7 = sram;

    // 有选择地恢复IO寄存器 - 使用寄存器列表（与EZODE的restore2_IO一致，u16写入）
    volatile uint16_t *io_base = (volatile uint16_t*)0x04000000;
    volatile uint8_t *sram_io = sram_bank7 + 0x9000;
    
    // 获取寄存器列表地址
    uint32_t reg_list_addr;
    GET_REL_ADDR(io_register_list, reg_list_addr);
    const uint16_t *reg_list = (const uint16_t*)reg_list_addr;
    
    // 遍历寄存器列表，恢复指定的寄存器
    for (int i = 0; reg_list[i] != 0xFF00; i++) {
        uint16_t offset = reg_list[i];
        // 从SRAM读取2字节（必须u8访问后组装）
        uint16_t value = sram_io[offset] | (sram_io[offset+1] << 8);
        // 写入到IO寄存器（u16写入）
        io_base[offset / 2] = value;
    }
    
    // 从SRAM恢复DMA和音频寄存器
    // 注意：load_from_flash永远不会返回，所以必须在这里恢复所有寄存器
    volatile uint8_t *sram_rts_regs = sram_bank7 + 0x8400 + 0x40;
    
    // 恢复DMA控制寄存器到硬件（从rts_temp_regs_t结构读取）
    // dma_control在结构体偏移0位置，每个是uint16_t
    for (int i = 0; i < 4; i++) {
        uint16_t value = sram_rts_regs[i*2] | (sram_rts_regs[i*2+1] << 8);
        io_base[(0x00BA + i*0x0C) / 2] = value;  // DMA0-3CNT_H
    }

    // 恢复音频寄存器到硬件
    // sound_regs在结构体偏移8位置
    uint16_t sound_l = sram_rts_regs[8] | (sram_rts_regs[9] << 8);
    uint16_t sound_x = sram_rts_regs[10] | (sram_rts_regs[11] << 8);
    io_base[0x0080 / 2] = sound_l;   // SOUNDCNT_L
    io_base[0x0084 / 2] = sound_x;   // SOUNDCNT_X
    
    /* ============================================================
     * 额外添加：恢复32位背景变换寄存器
     * 这些是我们自己添加的，EZODE原版没有
     * 从已保存的IO寄存器数据中恢复BG2X/Y和BG3X/Y
     * ============================================================ */
    volatile uint32_t *io32_base = (volatile uint32_t*)0x04000000;

    // 恢复BG2X/Y和BG3X/Y (各4字节，需要从SRAM u8组装成u32)
    uint32_t bg2x = sram_io[0x28] | (sram_io[0x29] << 8) | (sram_io[0x2A] << 16) | (sram_io[0x2B] << 24);
    uint32_t bg2y = sram_io[0x2C] | (sram_io[0x2D] << 8) | (sram_io[0x2E] << 16) | (sram_io[0x2F] << 24);
    uint32_t bg3x = sram_io[0x38] | (sram_io[0x39] << 8) | (sram_io[0x3A] << 16) | (sram_io[0x3B] << 24);
    uint32_t bg3y = sram_io[0x3C] | (sram_io[0x3D] << 8) | (sram_io[0x3E] << 16) | (sram_io[0x3F] << 24);

    io32_base[0x0028/4] = bg2x;  // BG2X
    io32_base[0x002C/4] = bg2y;  // BG2Y
    io32_base[0x0038/4] = bg3x;  // BG3X
    io32_base[0x003C/4] = bg3y;  // BG3Y
    /* ============================================================
     * 额外添加部分结束
     * ============================================================ */
    
    // 清零中断标志寄存器 IF (Interrupt Request Flags / IRQ Acknowledge)
    io_base[0x0202 / 2] = 0;

    ////////////////////////////////////////////////////////////////////////////////////////
    // 关键步骤：将cpu_regs从SRAM复制到EWRAM末尾，便于后续32位访问
    ////////////////////////////////////////////////////////////////////////////////////////

    // 将52字节的cpu_regs从SRAM Bank 7复制到EWRAM末尾（0x0203FE00）
    // 这样后面可以用32位访问恢复寄存器
    volatile uint8_t *sram_cpu_regs = sram_bank7 + 0x8400;
    volatile uint8_t *ewram_buffer = (volatile uint8_t*)0x0203FE00;
    for (uint32_t i = 0; i < 52; i++) {
        ewram_buffer[i] = sram_cpu_regs[i];
    }

    // 恢复系统模式SP和LR寄存器 - 从EWRAM缓冲区读取（已经复制好了）
    // 这需要切换到系统模式，恢复寄存器，然后切换回来
    volatile uint32_t *ewram_cpu_regs = (volatile uint32_t*)0x0203FE00;
    asm volatile(
        "mrs r0, cpsr\n"                // 保存当前CPSR

        // 从EWRAM缓冲区读取SPSR（32位访问）
        "ldr r7, %[cpu_regs]\n"         // r7 = EWRAM缓冲区地址
        "ldr r2, [r7]\n"                // 直接读取SPSR（32位）
        "msr spsr_cxsf, r2\n"           // 恢复SPSR irq状态

        "mov r1,#0xDF\n"                // 切换到系统模式
        "msr cpsr_cf, r1\n"
        "nop\n"

        "add r7, r7, #0x2C\n"           // 跳到系统模式SP/LR位置
        "ldmia r7!, {r13-r14}\n"        // 直接恢复SP和LR（32位访问）

        "msr cpsr_cf, r0\n"             // 恢复CPSR,即，切换回到IRQ模式
        "nop\n"
        :
        : [cpu_regs] "m" (ewram_cpu_regs)
        : "r0", "r1", "r2", "r7", "memory"
    );
    
    ////////////////////////////////////////////////////////////////////////////////////////
    // 音频寄存器恢复 - 直接从Flash恢复到硬件寄存器
    ////////////////////////////////////////////////////////////////////////////////////////
    
    // 恢复音频寄存器 (0x4000060-0x4000090) - 从Bank 7的0x9060偏移
    // SRAM必须u8访问，音频寄存器需要u16写入
    volatile uint8_t *sram_audio = sram_bank7 + 0x9060;
    volatile uint16_t *audio_reg = (volatile uint16_t*)0x04000060;
    for (uint32_t i = 0; i < 0x30 / 2; i++) {  // 48字节 = 24个u16
        uint16_t value = sram_audio[i*2] | (sram_audio[i*2+1] << 8);
        audio_reg[i] = value;
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    // 恢复结束
    ////////////////////////////////////////////////////////////////////////////////////////

    // 禁用绿色交换
    *green_swap_reg = 0;

    // 切换到Bank 6来读取IWRAM数据
    SRAM_BANK_SEL = IWRAM_PALETTE_SECTOR;

    // 恢复IRQ模式的寄存器并跳转到原始中断处理程序
    asm volatile(

        // 恢复IWRAM - 从SRAM Bank 6读取（Bank已经设置好）
        "ldr r12, %[cpu_regs]\n"            // r12 = EWRAM缓冲区地址（用于后续恢复寄存器）
        "mov r2, #0x0E000000\n"             // r2 = SRAM基地址
        "mov r3, #0x03000000\n"             // r3 = IWRAM地址
        "mov r4, #0x8000\n"                 // r4 = 32KB计数器

        "iwram_copy_loop:\n"                // 循环标签
        "ldrb r5, [r2], #1\n"               // 从SRAM读取1字节，并递增r2
        "strb r5, [r3], #1\n"               // 写入IWRAM，并递增r3
        "subs r4, r4, #1\n"                 // 递减计数器
        "bne iwram_copy_loop\n"             // 如果不为0，继续循环

        // 恢复Bank 0，让游戏能正常访问SRAM
        "mov r5, #0x09000000\n"
        "mov r6, #0\n"
        "strh r6, [r5]\n"                   // SRAM_BANK_SEL = 0

        // 从EWRAM缓冲区恢复寄存器（已经复制到0x0203FE00）
        // r12已经指向EWRAM缓冲区
        "add r12, r12, #4\n"                // 跳过SPSR（从+4开始）
        "ldmia r12!, {r4-r11,sp,lr}\n"      // 直接恢复r4-r11,sp,lr（32位访问）

        // r3需要单独处理（ldmia不能包含r3因为r12在r3之后）
        "ldr r12, %[cpu_regs]\n"            // 重新加载EWRAM缓冲区地址
        "ldr r3, [r12, #4]\n"               // 恢复r3（位于偏移4）
        "\n"
        "mov r0, #0x04000000\n"
        "ldr pc, [r0, #-(0x04000000-0x03FFFFF4)]\n"  // 跳转到原始IRQ处理程序
        :
        : [cpu_regs] "m" (ewram_cpu_regs)
        : "memory"
    );
}

// 检查RTS存档标志是否有效
__attribute__((target("arm"))) bool check_rts_save_flag(void)
{
    // 切换到Bank 7检查RTS标志
    SRAM_BANK_SEL = VRAM_BACK_MISC_SECTOR;

    // SRAM Bank 7的0xFFF0偏移处
    volatile uint8_t *sram_flag = (volatile uint8_t*)(0x0E000000 + 0xFFF0);

    // 获取RTS标志字符串地址
    uint32_t expected_flag_addr;
    GET_REL_ADDR(rts_flag_string, expected_flag_addr);
    const char *expected_flag = (const char*)expected_flag_addr;

    // 从SRAM读取并比较标志（SRAM必须u8访问）
    for (uint32_t i = 0; i < 16; i++) {
        if (sram_flag[i] != expected_flag[i]) {
            // 恢复Bank 0后返回
            SRAM_BANK_SEL = 0;
            return false;
        }
    }

    // 恢复Bank 0后返回true
    SRAM_BANK_SEL = 0;
    return true;
}

// 保存EWRAM到SRAM Bank 1-4
__attribute__((target("arm"))) void save_ewram_to_flash(int flash_type_index __attribute__((unused)))
{
    volatile uint8_t *ewram = (volatile uint8_t*)0x02000000;
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;

    // EWRAM是256KB，分4个64KB Bank保存
    for (int bank = 0; bank < 4; bank++) {
        // 切换到对应的SRAM Bank (1-4)
        SRAM_BANK_SEL = EWRAM_START_SECTOR + bank;

        // 复制64KB从EWRAM到SRAM（8bit操作）
        int bank_start = bank * SECTOR_SIZE;
        for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
            sram[i] = ewram[bank_start + i];
        }
    }
}

// 保存IWRAM和VRAM后半部分到SRAM Bank 6
__attribute__((target("arm"))) void save_iwram_vram_back_to_flash(int flash_type_index __attribute__((unused)))
{
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;

    // 切换到Bank 6
    SRAM_BANK_SEL = IWRAM_PALETTE_SECTOR;

    // 复制IWRAM (32KB) 到SRAM的0x0000偏移
    volatile uint8_t *iwram = (volatile uint8_t*)0x03000000;
    for (uint32_t i = 0; i < 0x8000; i++) {
        sram[i] = iwram[i];
    }

    // 复制VRAM后32KB (0x06010000) 到SRAM的0x8000偏移
    volatile uint8_t *vram_back = (volatile uint8_t*)0x06010000;
    volatile uint8_t *sram_vram = sram + 0x8000;
    for (uint32_t i = 0; i < 0x8000; i++) {
        sram_vram[i] = vram_back[i];
    }
}

// 保存VRAM前64KB到SRAM Bank 5
__attribute__((target("arm"))) void save_vram_front_to_flash(int flash_type_index __attribute__((unused)))
{
    volatile uint8_t *vram = (volatile uint8_t*)0x06000000;
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;

    // 切换到Bank 5
    SRAM_BANK_SEL = VRAM_FRONT_SECTOR;

    // 复制VRAM前64KB到SRAM（8bit操作）
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
        sram[i] = vram[i];
    }
}

// 保存VRAM后半部分、OAM、IO寄存器等到SRAM Bank 7
__attribute__((target("arm"))) void save_misc_to_flash(int flash_type_index __attribute__((unused)), rts_temp_regs_t *rts_regs, volatile uint8_t *cpu_regs_addr)
{
    volatile uint8_t *sram = (volatile uint8_t*)0x0E000000;

    // 切换到Bank 7
    SRAM_BANK_SEL = VRAM_BACK_MISC_SECTOR;

    // 1. 复制调色板 (1KB) 到SRAM的0x0000偏移
    volatile uint8_t *palette = (volatile uint8_t*)0x05000000;
    for (uint32_t i = 0; i < 0x400; i++) {
        sram[i] = palette[i];
    }

    // 2. 复制OAM (1KB) 到SRAM的0x8000偏移（保持原位置）
    volatile uint8_t *oam = (volatile uint8_t*)0x07000000;
    volatile uint8_t *sram_oam = sram + 0x8000;
    for (uint32_t i = 0; i < 0x400; i++) {
        sram_oam[i] = oam[i];
    }

    // 3. 复制cpu_regs到SRAM的0x8400偏移，然后保存rts_temp_regs结构体
    volatile uint8_t *sram_spend = sram + 0x8400;
    for (uint32_t i = 0; i < 52; i++) {  // 大小是52字节
        sram_spend[i] = cpu_regs_addr[i];
    }

    // 在cpu_regs+0x40位置直接保存rts_temp_regs结构体
    volatile uint8_t *sram_rts_regs = (volatile uint8_t*)(sram_spend + 0x40);
    uint8_t *rts_regs_src = (uint8_t*)rts_regs;
    for (uint32_t i = 0; i < sizeof(rts_temp_regs_t); i++) {
        sram_rts_regs[i] = rts_regs_src[i];
    }
    // *sram_rts_regs = *rts_regs;  // 不能直接复制整个结构体，因为会调用memcpy

    // 4. 复制I/O寄存器0x04000000-0x04000060到SRAM的0x9000偏移
    volatile uint8_t *io_base = (volatile uint8_t*)0x04000000;
    volatile uint8_t *sram_io1 = sram + 0x9000;
    for (uint32_t i = 0; i < 0x60; i++) {
        sram_io1[i] = io_base[i];
    }

    // 5. 直接复制音频寄存器 (0x4000060-0x4000090) 到SRAM的0x9060偏移
    volatile uint8_t *audio_reg = (volatile uint8_t*)0x04000060;
    volatile uint8_t *sram_audio = sram + 0x9060;
    for (uint32_t i = 0; i < 0x30; i++) {  // 48字节
        sram_audio[i] = audio_reg[i];
    }

    // 6. 用原始的sound寄存器值覆盖SRAM中的对应位置
    // SOUNDCNT_L在0x04000080，相对于0x04000060的偏移是0x20
    // SOUNDCNT_X在0x04000084，相对于0x04000060的偏移是0x24
    volatile uint16_t *sram_audio_16 = (volatile uint16_t*)(sram + 0x9060);
    sram_audio_16[0x20/2] = rts_regs->sound_regs[0];  // SOUNDCNT_L
    sram_audio_16[0x24/2] = rts_regs->sound_regs[1];  // SOUNDCNT_X

    // 7. 复制I/O寄存器0x04000090-0x040003FE到SRAM的0x9090偏移
    volatile uint8_t *io_base2 = (volatile uint8_t*)0x04000090;
    volatile uint8_t *sram_io2 = sram + 0x9090;
    for (uint32_t i = 0; i < 0x370; i++) {
        sram_io2[i] = io_base2[i];
    }

    // 9. 写入RTS标志字符串到SRAM的0xFFF0偏移
    // 使用GET_REL_ADDR获取字符串地址
    uint32_t flag_addr;
    GET_REL_ADDR(rts_flag_string, flag_addr);
    const char *flag_ptr = (const char*)flag_addr;
    volatile uint8_t *sram_flag = sram + 0xFFF0;
    for (uint32_t i = 0; i < 16; i++) {
        sram_flag[i] = flag_ptr[i];
    }
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