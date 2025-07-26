// Payload头部必须在文件开头，patcher需要
#include <stdint.h>

// 前向声明
void patched_entrypoint(void);

// Payload头部结构 - 必须在文件最开始
__attribute__((section(".text"))) const uint32_t original_entrypoint = 0x080000c0;
__attribute__((section(".text"))) const uint32_t save_size = 0x20000;
__attribute__((section(".text"))) const uint32_t patched_entrypoint_addr = (uint32_t)patched_entrypoint;

#define AGB_ROM  ((unsigned char*)0x8000000)
#define AGB_SRAM ((volatile unsigned char*)0xE000000)
#define SRAM_SIZE 64
#define AGB_SRAM_SIZE SRAM_SIZE*1024
#define SRAM_BANK_SEL (*(volatile unsigned short*) 0x09000000)

#define _FLASH_WRITE(pa, pd) { *(((unsigned short *)AGB_ROM)+((pa)/2)) = pd; __asm("nop"); }

// 前向声明
void patched_entrypoint(void);
extern void flush_sram(void);

// Flash函数声明
int identify_flash_1(void);
void erase_flash_1(unsigned sa, unsigned save_size);
void program_flash_1(unsigned sa, unsigned save_size);
int identify_flash_2(void);
void erase_flash_2(unsigned sa, unsigned save_size);
void program_flash_2(unsigned sa, unsigned save_size);
int identify_flash_3(void);
void erase_flash_3(unsigned sa, unsigned save_size);
void program_flash_3(unsigned sa, unsigned save_size);
int identify_flash_4(void);
void erase_flash_4(unsigned sa, unsigned save_size);
void program_flash_4(unsigned sa, unsigned save_size);

// Assembly标签外部声明
extern char identify_flash_1_end[];
extern char erase_flash_1_end[];
extern char program_flash_1_end[];
extern char identify_flash_2_end[];
extern char erase_flash_2_end[];
extern char program_flash_2_end[];
extern char identify_flash_3_end[];
extern char erase_flash_3_end[];
extern char program_flash_3_end[];
extern char identify_flash_4_end[];
extern char erase_flash_4_end[];
extern char program_flash_4_end[];


// C语言版本的手动SRAM刷新函数
void __attribute__((target("thumb"))) flush_sram_manual_entry(void) {
    // IME寄存器 - Interrupt Master Enable Register (0x04000208)
    volatile uint16_t *ime_reg = (volatile uint16_t*)0x04000208;
    
    // 保存当前中断状态并禁用中断
    uint16_t old_ime = *ime_reg;
    *ime_reg = 0;
    
    // 调用flush_sram函数 (编译器会自动处理Thumb→ARM模式切换)
    flush_sram();
    
    // 恢复中断状态
    *ime_reg = old_ime;
}

// patched_entrypoint - 用naked function改造 (ARM模式)
__attribute__((naked, target("arm"))) void patched_entrypoint(void)
{
    asm volatile(
        "mov r0, # 0x04000000\n"
        "adr r1, keypad_irq_handler\n"
        "str r1, [r0, # -4]\n"
        
        "adrl r0, flash_save_sector\n"
        "mov r1, # 0x0e000000\n"
        "ldr r2, save_size\n"
        "add r2, r1\n"
        "mov r3, # 0x09000000\n"
        "@ Lock 369in1 mapper\n"
        "mov r4, # 0x80\n"
        "strb r4, [r1, # 3]\n"
        
        "sram_init_loop:\n"
        "lsr r4, r1, # 16\n"
        "and r4, # 1\n"
        "strh r4, [r3]\n"
        "nop\n"
        "ldrb r4, [r0], # 1\n"
        "strb r4, [r1], # 1\n"
        "cmp r1, r2\n"
        "blo sram_init_loop\n"
        
        "@ Set bank to 0 for banking-unaware software\n"
        "mov r4, # 0\n"
        "strh r4, [r3]\n"
        
        "ldr pc, original_entrypoint\n"
        ::: "memory"
    );
}

asm(R"(

.arm
)");

// keypad_irq_handler - 用naked function改造 (ARM模式)
// IRQ handlers are called with 0x04000000 in r0 which is handy!
__attribute__((naked, target("arm"))) void keypad_irq_handler(void)
{
    asm volatile(
        "@ Check keypad register for L+R+START+SELECT\n"
        "@ May need to be changed to ldrh\n"
        "ldr r3, [r0, # 0x130]\n"
        "teq r3, # 0xf3\n"
        "ldrne pc, [r0, # - 12]\n"
        
        "@ Enable green swap\n"
        "mov r1, # 1\n"
        "strh r1, [r0, # 2]\n"
        
        "@ Switch to system mode to get lots of stack\n"
        "mov r3, # 0x9f\n"
        "msr cpsr, r3\n"
        
        "push {lr}\n"
        "bl flush_sram\n"
        "pop {lr}\n"
        
        "@ return to irq mode\n"
        "mov r3, # 0x92\n"
        "msr cpsr, r3\n"
        
        "@ Disable green swap\n"
        "mov r0, # 0x04000000\n"
        "strh r0, [r0, # 2]\n"
        
        "@ Wait until keypad register is no longer L+R+START+SELECT\n"
        "ldr r3, [r0, # 0x130]\n"
        "teq r3, # 0xf3\n"
        "beq (.-8)\n"
        
        "ldr pc, [r0, # - 12]\n"
        ::: "memory"
    );
}

asm(R"(



# Ensure interrupts are disabled and there is plenty of stack space before calling
flush_sram:
    mov r0, # 0x04000000
    # save sound state then disable it
    ldrh r2, [r0, # 0x0080]
    ldrh r3, [r0, # 0x0084]
    push {r2, r3}
    strh r0, [r0, # 0x0084]

    # save DMAs state then disable them
    ldrh r3, [r0, # 0x00BA]
    push {r3}
    strh r0, [r0, # 0x00BA]
    ldrh r3, [r0, # 0x00C6]
    push {r3}
    strh r0, [r0, # 0x00C6]
    ldrh r3, [r0, # 0x00d2]
    push {r3}
    strh r0, [r0, # 0x00d2]
    ldrh r3, [r0, # 0x00de]
    push {r3}
    strh r0, [r0, # 0x00de]

    push {lr}
    
    # Try flushing for various flash chips
    push {r4, r5, r6, r7}
    adrl r4, flash_save_sector
    sub r4, # 0x08000000
    ldr r5, save_size
    adr r6, flash_fn_table 
    adr r7, original_entrypoint 
    
try_flash:

    ldm r6!, {r2, r3}
    cmp r2, # 0
    beq flush_sram_done
    add r2, r7
    add r3, r7
    bl run_from_ram
    cmp r0, #0
    bne found_flash
    add r6, # 16
    b try_flash
    
found_flash:
    ldm r6!, {r2, r3}
    mov r0, r4
    mov r1, r5
    add r2, r7
    add r3, r7
    bl run_from_ram
    ldm r6!, {r2, r3}
    mov r0, r4
    mov r1, r5
    add r2, r7
    add r3, r7
    bl run_from_ram

flush_sram_done:
    pop {r4, r5, r6, r7}

    pop {lr}
    mov r0, #0x04000000

    # restore DMAs state
    pop {r3}
    strh r3, [r0, # 0x00de]
    pop {r3}
    strh r3, [r0, # 0x00d2]
    pop {r3}
    strh r3, [r0, # 0x00c6]
    pop {r3}
    strh r3, [r0, # 0x00ba]


    # restore sound state
    pop {r2, r3}
    strh r3, [r0, # 0x0084]
    strh r2, [r0, # 0x0080]

    bx lr
    
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

// 空闲中断处理函数 - 用naked function改造 (ARM模式)
__attribute__((naked, target("arm"))) void idle_irq_handler(void)
{
    asm volatile(
        "ldr pc, [r0, # -12]\n"
        ::: "memory"
    );
}

// 在RAM中运行函数 - 用naked function改造 (ARM模式)
__attribute__((naked, target("arm"))) void run_from_ram(void)
{
    asm volatile(
        "push {r4, r5, lr}\n"
        "mov r4, sp\n"
        "bic r2, # 1\n"
        
        "run_from_ram_loop:\n"    
        "ldr r5, [r3, # -4]!\n"
        "push {r5}\n"
        "cmp r2, r3\n"
        "bne run_from_ram_loop\n"
        
        "add r2, sp, # 1\n"
        "mov lr, pc\n"
        "bx r2\n"
        
        "mov sp, r4\n"
        "pop {r4, r5, lr}\n"
        "bx lr\n"
        ::: "memory"
    );
}

int identify_flash_1()
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

void erase_flash_1(unsigned sa, unsigned save_size)
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

void program_flash_1(unsigned sa, unsigned save_size)
{    
    // Write data
    SRAM_BANK_SEL = 0;
    for (int i=0; i<save_size; i+=2) {
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

int identify_flash_2()
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

void erase_flash_2(unsigned sa, unsigned save_size)
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

void program_flash_2(unsigned sa, unsigned save_size)
{
    // Write data
    SRAM_BANK_SEL = 0;
    for (int i=0; i<save_size; i+=2) {
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

int identify_flash_3()
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

void erase_flash_3(unsigned sa, unsigned save_size)
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

void program_flash_3(unsigned sa, unsigned save_size)
{
    // Write data
    SRAM_BANK_SEL = 0;
    for (int i=0; i<save_size; i+=2) {
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

int identify_flash_4()
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

void erase_flash_4(unsigned sa, unsigned save_size)
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

void program_flash_4(unsigned sa, unsigned save_size)
{
    // Write data
    int c = 0;
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