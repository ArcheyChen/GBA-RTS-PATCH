// Payload头部必须在文件开头，patcher需要
#include <stdint.h>

// 前向声明
void patched_entrypoint(void);

// Payload头部结构 - 必须在文件最开始,然后必须是强行改为text，否则无法获取相对偏移 
__attribute__((section(".text"))) const uint32_t original_entrypoint = 0x080000c0;
__attribute__((section(".text"))) const uint32_t save_size = 0x20000;
__attribute__((section(".text"))) const uint32_t patched_entrypoint_addr = (uint32_t)patched_entrypoint;

#define AGB_ROM  ((unsigned char*)0x8000000)
#define AGB_SRAM ((volatile unsigned char*)0xE000000)
#define SRAM_SIZE 64
#define AGB_SRAM_SIZE SRAM_SIZE*1024
#define SRAM_BANK_SEL (*(volatile unsigned short*) 0x09000000)

#define _FLASH_WRITE(pa, pd) { *(((unsigned short *)AGB_ROM)+((pa)/2)) = pd; __asm("nop"); }

// 定义获取相对地址的宏
#define GET_REL_ADDR(symbol, var) \
    asm volatile("adrl %0, " #symbol : "=r"(var))

#define GET_REL_VALUE(symbol, var) \
    asm volatile("ldr %0, " #symbol : "=r"(var))

// 前向声明  
void patched_entrypoint(void);
void flush_sram(void);
int run_thumb_from_ram(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);

// 前向声明函数
void load_from_flash(void);
void copy_flash_to_sram(uint32_t flash_addr, uint32_t size);

// C语言版本的按键中断处理程序  
__attribute__((target("arm"))) void keypad_irq_handler(void)
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
    if ((keypad_value & lr_start) == 0) {
        // 启用绿色交换 (GREEN_SWAP: 0x04000002)
        volatile uint16_t *green_swap_reg = (volatile uint16_t*)0x04000002;
        *green_swap_reg = 1;
        
        // 调用SRAM刷新函数 (存档)
        flush_sram();
        
        // 禁用绿色交换
        *green_swap_reg = 0;
        
        // 等待按键组合松开
        while (((*keypad_reg) & lr_start) == 0) {
            // 空循环等待
        }
    }
    // 检查是否按下L+R+SELECT (读档)
    else if ((keypad_value & lr_select) == 0) {
        // 启用绿色交换 (GREEN_SWAP: 0x04000002)
        volatile uint16_t *green_swap_reg = (volatile uint16_t*)0x04000002;
        *green_swap_reg = 1;

        // 调用读档函数
        load_from_flash();
        
        // 禁用绿色交换
        *green_swap_reg = 0;
        
        // 等待按键组合松开
        while (((*keypad_reg) & lr_select) == 0) {
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
    
    // 调用通用的Flash到SRAM复制函数
    copy_flash_to_sram(flash_src_addr, save_size_value);
    
    // 跳转到原始入口点
    asm volatile(
        "mov pc, %0\n"
        :
        : "r"(original_entry_addr)
        : "memory"
    );
}


  __attribute__((target("arm"))) void flush_sram(void)
  {
    // 硬件寄存器基地址
    volatile uint16_t *hw_base = (volatile uint16_t*)0x04000000;
    
    // 保存sound状态并禁用
    uint16_t sound_reg1 = hw_base[0x0080/2];  // SOUNDCNT_L
    uint16_t sound_reg2 = hw_base[0x0084/2];  // SOUNDCNT_H
    hw_base[0x0084/2] = 0;  // 禁用sound
    
    // 保存DMA状态并禁用
    uint16_t dma_regs[4];
    dma_regs[0] = hw_base[0x00BA/2];  // DMA0CNT_H
    hw_base[0x00BA/2] = 0;
    dma_regs[1] = hw_base[0x00C6/2];  // DMA1CNT_H
    hw_base[0x00C6/2] = 0;
    dma_regs[2] = hw_base[0x00D2/2];  // DMA2CNT_H
    hw_base[0x00D2/2] = 0;
    dma_regs[3] = hw_base[0x00DE/2];  // DMA3CNT_H
    hw_base[0x00DE/2] = 0;
    
    // 用C代码获取地址和值，避免GOT依赖
    uint32_t flash_sector_addr, save_size_value, flash_fn_table_addr, original_entry_addr;
    GET_REL_ADDR(flash_save_sector, flash_sector_addr);
    flash_sector_addr -= 0x08000000;  // 转换为flash内偏移
    GET_REL_VALUE(save_size, save_size_value);
    GET_REL_ADDR(flash_fn_table, flash_fn_table_addr);
    GET_REL_ADDR(original_entrypoint, original_entry_addr);
    
    // 第一部分：try_flash循环检测（C语言重写）
    uint32_t fn_table_after_identify = 0;
    int identify_result = 0;
    
    uint32_t *fn_ptr = (uint32_t*)flash_fn_table_addr;
    
    for (int i = 0; ; i++) {
        // 读取identify函数地址
        uint32_t identify_start = fn_ptr[i * 6];      // identify函数起始地址
        uint32_t identify_end = fn_ptr[i * 6 + 1];    // identify函数结束地址
        
        // 检查是否到达表尾
        if (identify_start == 0) {
            identify_result = 0;
            break;
        }
        
        // 调用identify函数
        identify_start += original_entry_addr;
        identify_end += original_entry_addr;
        identify_result = run_thumb_from_ram(0, 0, identify_start, identify_end);
        
        if (identify_result != 0) {
            // 找到匹配的flash，设置函数表位置到erase函数
            fn_table_after_identify = (uint32_t)&fn_ptr[i * 6 + 2];
            break;
        }
        // 如果没找到，继续下一个条目（for循环自动递增i）
    }
    
    // 如果找到匹配的flash，执行第二部分（C语言重写）
    if (identify_result != 0) {
        uint32_t *erase_prog_ptr = (uint32_t*)fn_table_after_identify;
        
        // 读取并调用erase函数
        uint32_t erase_start = erase_prog_ptr[0] + original_entry_addr;
        uint32_t erase_end = erase_prog_ptr[1] + original_entry_addr;
        run_thumb_from_ram(flash_sector_addr, save_size_value, erase_start, erase_end);
        
        // 读取并调用program函数
        uint32_t program_start = erase_prog_ptr[2] + original_entry_addr;
        uint32_t program_end = erase_prog_ptr[3] + original_entry_addr;
        run_thumb_from_ram(flash_sector_addr, save_size_value, program_start, program_end);
    }
    
    // 恢复DMA状态
    hw_base[0x00DE/2] = dma_regs[3];
    hw_base[0x00D2/2] = dma_regs[2];
    hw_base[0x00C6/2] = dma_regs[1];
    hw_base[0x00BA/2] = dma_regs[0];
    
    // 恢复sound状态
    hw_base[0x0084/2] = sound_reg2;
    hw_base[0x0080/2] = sound_reg1;
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

// 读档函数 - 将Flash中的存档恢复到SRAM
__attribute__((target("arm"))) void load_from_flash(void)
{
    // 硬件寄存器基地址
    volatile uint16_t *hw_base = (volatile uint16_t*)0x04000000;
    
    // 保存sound状态并禁用
    uint16_t sound_reg1 = hw_base[0x0080/2];  // SOUNDCNT_L
    uint16_t sound_reg2 = hw_base[0x0084/2];  // SOUNDCNT_H
    hw_base[0x0084/2] = 0;  // 禁用sound
    
    // 保存DMA状态并禁用
    uint16_t dma_regs[4];
    dma_regs[0] = hw_base[0x00BA/2];  // DMA0CNT_H
    hw_base[0x00BA/2] = 0;
    dma_regs[1] = hw_base[0x00C6/2];  // DMA1CNT_H
    hw_base[0x00C6/2] = 0;
    dma_regs[2] = hw_base[0x00D2/2];  // DMA2CNT_H
    hw_base[0x00D2/2] = 0;
    dma_regs[3] = hw_base[0x00DE/2];  // DMA3CNT_H
    hw_base[0x00DE/2] = 0;
    
    // 获取保存的大小信息
    uint32_t save_size_value;
    GET_REL_VALUE(save_size, save_size_value);
    
    // 获取Flash扇区地址
    uint32_t flash_sector_addr;
    GET_REL_ADDR(flash_save_sector, flash_sector_addr);
    
    // 调用复用的复制函数
    copy_flash_to_sram(flash_sector_addr, save_size_value);
    
    // 恢复DMA状态
    hw_base[0x00DE/2] = dma_regs[3];
    hw_base[0x00D2/2] = dma_regs[2];
    hw_base[0x00C6/2] = dma_regs[1];
    hw_base[0x00BA/2] = dma_regs[0];
    
    // 恢复sound状态
    hw_base[0x0084/2] = sound_reg2;
    hw_base[0x0080/2] = sound_reg1;
}
    
asm(R"(
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

// 在RAM中运行Thumb函数 - 混合C和汇编实现
__attribute__((target("arm"))) 
int run_thumb_from_ram(uint32_t arg0, uint32_t arg1, uint32_t func_start, uint32_t func_end)
{
    int result;
    
    asm volatile(
        "mrs r0, CPSR\n"          // 备份当前CPSR
        "mov r1, #0xDF\n"		//切换到系统模式
        "msr cpsr_cf, r1\n"
        "nop\n"                     // 确保CPSR更新完成
        "push {r0}\n"          // 保存CPSR到栈,注意，此时已经是系统栈

        "mov r4, sp\n"                    // 保存当前栈指针
        "bic %[start], %[start], #1\n"    // 清除Thumb位，得到实际函数地址
        
        "run_copy_loop:\n"    
        "ldr r5, [%[end], #-4]!\n"        // 从函数末尾向前复制
        "push {r5}\n"
        "cmp %[start], %[end]\n"
        "bne run_copy_loop\n"
        
        // 设置调用参数和Thumb位
        "mov r0, %[arg0]\n"               // 第一个参数
        "mov r1, %[arg1]\n"               // 第二个参数  
        "add r2, sp, #1\n"                // 栈上函数地址+1(Thumb位)
        "mov lr, pc\n"                    // 设置返回地址
        "bx r2\n"                         // 调用Thumb函数
        
        "mov %[result], r0\n"             // 保存返回值
        "mov sp, r4\n"                    // 恢复栈指针(清空拷贝过来的函数代码占的空间)

        "pop {r1}\n"                      // 恢复CPSR
        "msr cpsr_cf, r1\n"               // 恢复CPSR,即，返回之前的模式（IRQ）
        
        : [result] "=r" (result)
        : [arg0] "r" (arg0), [arg1] "r" (arg1), 
          [start] "r" (func_start), [end] "r" (func_end)
        : "r0", "r1", "r2", "r4", "r5", "lr", "memory"
    );
    
    return result;
}

int __attribute__((target("thumb"))) identify_flash_1()
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
asm("identify_flash_1_end:");

void __attribute__((target("thumb"))) erase_flash_1(unsigned sa, unsigned )
{
    // Erase flash sector
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
asm("erase_flash_1_end:");

void __attribute__((target("thumb"))) program_flash_1(unsigned sa, unsigned _save_size)
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
asm("program_flash_1_end:");

int __attribute__((target("thumb"))) identify_flash_2()
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
asm("identify_flash_2_end:");

void __attribute__((target("thumb"))) erase_flash_2(unsigned sa, unsigned )
{
    // Erase flash sector
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
asm("erase_flash_2_end:");

void __attribute__((target("thumb"))) program_flash_2(unsigned sa, unsigned _save_size)
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
asm("program_flash_2_end:");

int __attribute__((target("thumb"))) identify_flash_3()
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
asm("identify_flash_3_end:");

void __attribute__((target("thumb"))) erase_flash_3(unsigned sa, unsigned )
{
    // Erase flash sector
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
asm("erase_flash_3_end:");

void __attribute__((target("thumb"))) program_flash_3(unsigned sa, unsigned _save_size)
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
asm("program_flash_3_end:");

int __attribute__((target("thumb"))) identify_flash_4()
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
asm("identify_flash_4_end:");

void __attribute__((target("thumb"))) erase_flash_4(unsigned sa, unsigned )
{
    // Erase flash sector
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
asm("erase_flash_4_end:");

void __attribute__((target("thumb")))  program_flash_4(unsigned sa, unsigned )
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
asm("program_flash_4_end:");


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