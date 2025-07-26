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

asm(R"(

.arm
patched_entrypoint:
    mov r0, # 0x04000000
    adr r1, keypad_irq_handler
    str r1, [r0, # -4]

    adrl r0, flash_save_sector
    mov r1, # 0x0e000000
    ldr r2, save_size
    add r2, r1
    mov r3, # 0x09000000
    # Lock 369in1 mapper
    mov r4, # 0x80
    strb r4, [r1, # 3]
sram_init_loop:
    lsr r4, r1, # 16
    and r4, # 1
    strh r4, [r3]
    nop
    ldrb r4, [r0], # 1
    strb r4, [r1], # 1
    cmp r1, r2
    blo sram_init_loop
    
    # Set bank to 0 for banking-unaware software
    mov r4, # 0
    strh r4, [r3]

    ldr pc, original_entrypoint

.thumb

.arm
# IRQ handlers are called with 0x04000000 in r0 which is handy!
keypad_irq_handler:
    # Check keypad register for L+R+START+SELECT
    # May need to be changed to ldrh
    ldr r3, [r0, # 0x130]
    teq r3, # 0xf3
    ldrne pc, [r0, # - 12]
    
    # Enable green swap
    mov r1, # 1
    strh r1, [r0, # 2]
    
    # Switch to system mode to get lots of stack
    mov r3, # 0x9f
    msr cpsr, r3
    
    push {lr}
    bl flush_sram
    pop {lr}

    # return to irq mode
    mov r3, # 0x92
    msr cpsr, r3
    
    # Disable green swap
    mov r0, # 0x04000000
    strh r0, [r0, # 2]
    
    # Wait until keypad register is no longer L+R+START+SELECT
    ldr r3, [r0, # 0x130]
    teq r3, # 0xf3
    beq (.-8)
    
    ldr pc, [r0, # - 12]



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

run_from_ram:
    push {r4, r5, lr}
    mov r4, sp
    bic r2, # 1
    
run_from_ram_loop:    
    ldr r5, [r3, # -4]!
    push {r5}
    cmp r2, r3
    bne run_from_ram_loop
    
    add r2, sp, # 1
    mov lr, pc
    bx r2
    
    mov sp, r4
    pop {r4, r5, lr}
    bx lr
)");

// 空闲中断处理函数 - 用naked function改造
__attribute__((naked)) void idle_irq_handler(void)
{
    asm volatile(
        ".arm\n"
        "ldr pc, [r0, # -12]\n"
        ".thumb\n"
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