# GBA RTS实时存档补丁技术文档

## 概述

本文档详细介绍了GBA RTS(Real-Time Save)实时存档补丁系统的技术实现原理。该系统通过在GBA ROM中注入自定义代码，实现了无电池化存档功能，允许玩家在游戏运行时通过特定按键组合进行实时存档和读档。

## 核心架构

### 主要组件
- **patcher.c**: 补丁程序，负责将payload注入ROM并修改中断向量
- **payload.c**: 核心功能代码，包含中断处理、存档读档逻辑
- **payload_header.h**: 定义payload头部结构体，用于patcher和payload之间的通信

## 技术原理详解

### 1. 中断劫持机制

#### 1.1 GBA中断系统背景
GBA使用ARM7TDMI处理器，中断处理机制如下：
- 中断向量表位于内存固定位置
- IRQ中断处理程序地址通过`0x03FFFFFC`指向
- 原始游戏通常将IRQ处理程序地址设置在`0x037F00FC`
- 0x3FFFFFC 是 0x3007FFC的镜像地址

附GBATEK上的资料：
```
Default memory usage at 03007FXX (and mirrored to 03FFFFXX)
  Addr.    Size Expl.
  3007FFCh 4    Pointer to user IRQ handler (32bit ARM code)
  3007FF8h 2    Interrupt Check Flag (for IntrWait/VBlankIntrWait functions)
  3007FF4h 4    Allocated Area
  3007FF0h 4    Pointer to Sound Buffer
  3007FE0h 16   Allocated Area
  3007FA0h 64   Default area for SP_svc Supervisor Stack (4 words/time)
  3007F00h 160  Default area for SP_irq Interrupt Stack (6 words/time)
Memory below 7F00h is free for User Stack and user data. The three stack pointers are initially initialized at the TOP of the respective areas:
  SP_svc=03007FE0h
  SP_irq=03007FA0h
  SP_usr=03007F00h
The user may redefine these addresses and move stacks into other locations, however, the addresses for system data at 7FE0h-7FFFh are fixed.
```
#### 1.2 中断劫持过程

**第一步：理解内存布局与空间利用**

根据GBATEK文档，GBA在`0x03007FF4h`位置有4字节的"Allocated Area"，这是系统预留的可用空间。patcher巧妙地利用了这个空间来实现中断劫持。

**第二步：地址重定向策略 (patcher.c:171-190)**
```c
// 查找并替换ROM中所有对IRQ handler地址的引用
uint8_t old_irq_addr[4] = { 0xfc, 0x7f, 0x00, 0x03 };  // 0x037F00FC
uint8_t new_irq_addr[4] = { 0xf4, 0x7f, 0x00, 0x03 };  // 0x037F00F4
```

**重定向原理：**
- 游戏ROM中通常包含对`0x037F00FC` (IRQ处理程序指针位置)的引用
- patcher将这些引用全部修改为`0x037F00F4`
- `0x037F00F4`正好对应GBATEK文档中的"Allocated Area"，可以安全使用
- 这样就把游戏的IRQ处理程序指向了系统预留的空白区域

**第三步：中断向量劫持 (payload.c:427-429)**
```c
// 设置自定义IRQ处理程序到系统IRQ向量
volatile uint32_t *irq_vector = (volatile uint32_t*)0x03FFFFFC;
*irq_vector = irq_handler_addr;  // 指向keypad_irq_handler
```

**完整劫持机制：**
```
正常情况：
  IRQ发生 → 0x03FFFFFC → [0x037F00FC] → 游戏IRQ处理程序

劫持后：
  IRQ发生 → 0x03FFFFFC → keypad_irq_handler → 
                          ↓ (按键检测)
           按键匹配 → RTS处理 → 恢复游戏状态
           按键不匹配 → [0x037F00F4] → 原始游戏IRQ处理程序
```

**关键技术优势：**
- 利用系统预留的"Allocated Area"，避免覆盖关键系统数据
- 不破坏IRQ Stack(0x3007F00h)、Sound Buffer指针(0x3007FF0h)等系统区域
- 原始IRQ处理程序被重定向到安全的预留空间，保持完全兼容性
- 实现了对游戏透明的中断劫持

### 2. Payload注入机制

#### 2.1 注入位置选择 (patcher.c:192-232)
```c
const int reserved_space = 0x80000; // 预留512KB空间
for (payload_base = romsize - reserved_space - payload_bin_len; payload_base >= 0; payload_base -= 0x40000)
{
    // 在256KB边界对齐的位置寻找全0或全0xFF的空间
    if (is_all_zeroes || is_all_ones) {
        break;
    }
}
```

算法特点：
- 从ROM末尾向前搜索，确保不覆盖游戏数据
- 必须256KB边界对齐
- 预留512KB用于RTS存档数据

#### 2.2 Payload头部结构 (payload_header.h:10-15)
```c
struct __attribute__((packed)) PayloadHeader {
    uint32_t original_entrypoint;      // 原始游戏入口点
    uint32_t save_size;                // SRAM大小配置
    uint32_t patched_entrypoint_addr;  // 补丁入口点
};
```

该结构体在payload开头，作为patcher和payload的通信接口。

### 3. 游戏入口劫持

#### 3.1 入口点修改 (patcher.c:302-325)
```c
// 解析原ROM入口点（ARM跳转指令）
unsigned long original_entrypoint_address = 0x08000000 + 8 + (original_entrypoint_offset << 2);
header->original_entrypoint = original_entrypoint_address;

// 修改ROM头部跳转指令，跳转到payload
((uint32_t*)rom)[0] = 0xea000000 | (new_entrypoint_address - 0x08000008) >> 2;
```

**工作流程：**
1. 备份原始入口点地址到payload头部
2. 修改ROM头部的跳转指令，使游戏启动时跳转到`patched_entrypoint`
3. `patched_entrypoint`调用`init_before_game`进行初始化，然后跳转到原游戏入口

### 4. 按键检测与存档系统

#### 4.1 按键组合定义 (payload.c:16-19)
```c
#define SAVE_KEYS 0x304  // L+R+START (存档)
#define LOAD_KEYS 0x308  // L+R+SELECT (读档)
```

#### 4.2 中断处理流程 (payload.c:261-324)

**RTS中断处理的核心挑战**

RTS实时存档本质上类似于操作系统的上下文切换，需要完整保存和恢复游戏的执行状态。但在GBA的IRQ处理中，我们面临几个关键约束：

**BIOS IRQ处理机制 (来自GBATEK):**
```asm
00000018  b      128h                ;IRQ vector: jump to actual BIOS handler
00000128  stmfd  r13!,r0-r3,r12,r14  ;BIOS自动保存 r0-r3,r12,r14 到SP_irq
0000012C  mov    r0,4000000h         ;ptr+4 to 03FFFFFC
00000130  add    r14,r15,0h          ;设置返回地址
00000134  ldr    r15,[r0,-4h]        ;跳转到用户IRQ处理程序
00000138  ldmfd  r13!,r0-r3,r12,r14  ;BIOS自动恢复寄存器
0000013C  subs   r15,r14,4h          ;IRQ返回
```

**关键分析：**
- BIOS已自动保存`r0-r3, r12, r14`，我们需要保存剩余的`r4-r11, r13(sp)`
- IRQ模式只有160字节栈空间，空间极其紧张
- 必须立即保存IRQ模式的`SPSR`寄存器，因为模式切换会破坏它
- 系统模式的`SP`和`LR`寄存器需要单独保存

**寄存器保存布局（52字节缓冲区）：**
```
0x00 (4字节): IRQ模式下的SPSR寄存器    - 最关键，必须首先保存
0x04 (32字节): IRQ模式下的r4-r11寄存器 - BIOS未保存的寄存器
0x24 (4字节): IRQ模式下的sp寄存器     - IRQ栈指针  
0x28 (4字节): IRQ模式下的lr寄存器     - IRQ返回地址
0x2C (4字节): 系统模式的SP（原始值）   - 游戏栈指针
0x30 (4字节): 系统模式的LR           - 游戏返回地址
```

**keypad_irq_handler详细实现分析：**

```asm
// === 第一阶段：按键检测与分支 ===
push {r0,lr}
bl detect_keys              // 检测是否按下L+R+START/SELECT
cmp r0, #1
pop {r0,lr}                 // 恢复r0寄存器（指向0x04000000）
beq call_handler            // 如果检测到目标按键，执行RTS处理

// 非目标按键：直接跳转到原始IRQ处理程序，永不返回
ldr pc, [r0, #-(0x04000000-0x037FFFFF4)]  // 跳转到[0x037FFFFF4]

// === 第二阶段：RTS处理的状态保存 ===
call_handler:
// 步骤1：立即保存SPSR（最关键！）
mrs r0, SPSR                // r0 = SPSR (必须最先保存，模式切换会破坏)
mrs r1, CPSR                // r1 = 当前CPSR (IRQ模式)

// 步骤2：切换到系统模式分配缓冲区
mov r2, #0xDF               // 系统模式标志
msr cpsr_cf, r2             // 切换到系统模式
nop                         // 确保模式切换完成
sub sp, sp, #52             // 在系统栈分配52字节（避免IRQ栈溢出）
mov r2, sp                  // r2 = 缓冲区基地址

// 步骤3：切换回IRQ模式保存IRQ寄存器
msr cpsr_cf, r1             // 恢复IRQ模式
nop

// 步骤4：保存寄存器到系统栈缓冲区
mov r12, r2                 // r12 = 缓冲区地址
stmia r12!, {r0}            // 保存SPSR(r0)
stmia r12!, {r4-r11,sp,lr}  // 保存r4-r11, IRQ的sp,lr

push {lr}                   // 保存LR用于后续BL调用

// 步骤5：保存系统模式寄存器
mov r3, #0xDF               // 切换到系统模式
msr cpsr_cf, r3
nop
mov r0, sp                  // r0 = 当前系统SP（已减52）
add r0, r0, #52             // 恢复原始系统SP值
add r3, r2, #0x2C           // r3 = 缓冲区 + 0x2C偏移
stmia r3!, {r0, lr}         // 保存系统模式原始SP和LR

// 步骤6：调用C语言处理函数
msr cpsr_cf, r1             // 切换回IRQ模式
nop
mov r0, r2                  // r0 = 缓冲区地址作为参数
bl keypad_process           // 可返回的函数调用

// 步骤7：恢复栈并返回BIOS
mrs r1, CPSR                // 重新获取CPSR
mov r3, #0xDF               // 切换到系统模式
msr cpsr_cf, r3
nop
add sp, sp, #52             // 恢复系统栈指针
msr cpsr_cf, r1             // 恢复IRQ模式
nop
pop {pc}                    // 返回到BIOS
```

**两种执行路径的技术差异：**

1. **非RTS路径** (`ldr pc`)：
   - 直接跳转，永不返回
   - 类似给原始中断处理程序加了一个"检测header"
   - 原始处理程序直接返回BIOS

2. **RTS路径** (`bl keypad_process`)：
   - 可返回的函数调用
   - 需要精确的栈管理和寄存器恢复
   - 在系统栈分配临时缓冲区，避免IRQ栈溢出

**关键技术优势：**
- 利用系统模式栈避免IRQ栈空间限制
- 精确的模式切换时序，确保寄存器完整性
- 支持嵌套中断调用
- 完全透明的状态保存/恢复机制
```

**关键设计点：**
- 使用系统模式栈分配临时缓冲区，避免IRQ栈溢出
- 精确的寄存器保存/恢复顺序
- 支持嵌套中断调用

### 5. Flash存储管理

#### 5.1 扇区布局设计 (payload.c:204-211)
```c
#define SECTOR_SIZE 0x10000        // 64KB per sector  
#define TOTAL_SECTORS 8            // 总共8个扇区(512KB)
#define EWRAM_START_SECTOR 0       // EWRAM: 扇区0-3 (256KB)
#define VRAM_FRONT_SECTOR 4        // VRAM前64KB: 扇区4
#define IWRAM_PALETTE_SECTOR 5     // IWRAM+调色板: 扇区5  
#define VRAM_BACK_MISC_SECTOR 6    // VRAM后32KB+其他: 扇区6
#define SRAM_SAVE_SECTOR 7         // 原始SRAM: 扇区7
```

#### 5.2 Run From RAM机制 (payload.c:762-801)

**核心技术挑战：**

在GBA系统中，Flash编程操作面临严重的总线冲突问题：

1. **ROM编程模式限制**：当Flash ROM进入编程模式后，整个ROM区域变为不可读取状态
2. **总线冲突**：SRAM读取时会阻塞ROM总线，导致ROM无法访问
3. **代码执行困境**：Flash操作代码本身位于ROM中，无法在Flash编程时正常执行

**解决方案：动态代码拷贝到栈**

```c
// run_arm_from_ram函数实现 (payload.c:762-801)
int run_arm_from_ram(uint32_t arg0, uint32_t arg1, uint32_t func_start, uint32_t func_end)
{
    asm volatile(
        "mrs r0, CPSR\n"              // 保存当前CPSR
        "mov r1, #0xDF\n"             // 切换到系统模式（获取大栈空间）
        "msr cpsr_cf, r1\n"
        "nop\n"
        "push {r0,lr}\n"              // 保存CPSR到系统栈
        
        "mov r4, sp\n"                // 保存当前栈指针
        
        // === 代码拷贝循环：从函数末尾向栈顶拷贝 ===
        "run_copy_loop:\n"
        "ldr r5, [%[end], #-4]!\n"    // 从函数末尾向前读取4字节
        "push {r5}\n"                 // 压入栈中
        "cmp %[start], %[end]\n"      // 比较是否拷贝完成
        "bne run_copy_loop\n"         // 继续拷贝
        
        // === 在栈上执行Flash操作函数 ===
        "mov r0, %[arg0]\n"           // 设置参数1
        "mov r1, %[arg1]\n"           // 设置参数2
        "mov lr, pc\n"                // 设置返回地址
        "mov pc, sp\n"                // 跳转到栈上的代码执行
        
        // === 清理栈空间并返回 ===
        "mov %[result], r0\n"         // 保存返回值
        "mov sp, r4\n"                // 恢复栈指针（清理拷贝的代码）
        
        "pop {r1,lr}\n"               // 恢复CPSR和LR
        "msr cpsr_cf, r1\n"           // 切换回IRQ模式
        
        : [result] "=r" (result)
        : [arg0] "r" (arg0), [arg1] "r" (arg1), 
          [start] "r" (func_start), [end] "r" (func_end)
        : "r0", "r1", "r2", "r4", "r5", "lr", "memory"
    );
    return result;
}
```

**函数边界确定机制：**
```c
// Flash函数定义模式
int identify_flash_1() {
    // Flash识别代码...
    return result;
}
asm(".align 4\n"
    "identify_flash_1_end:");  // 汇编标签标记函数结束

// 函数表存储开始和结束地址
flash_fn_table:
.word identify_flash_1          // 函数开始地址
.word identify_flash_1_end      // 函数结束地址（汇编标签）
```

**栈空间管理策略：**

- **IRQ模式栈限制**：只有160字节，无法容纳Flash操作代码
- **系统模式切换**：临时切换到系统模式获取大栈空间
- **逆向拷贝**：从函数末尾向栈顶拷贝，确保栈指针指向代码起始位置

**重要约束条件：**

在栈上运行的Flash操作函数有严格限制：
1. **不能调用其他函数**：因为函数地址在运行时动态确定
2. **不能访问全局变量**：全局变量位于ROM中，Flash编程时不可访问
3. **必须是自包含代码**：所有需要的数据必须通过参数传递

**理论扩展可能性：**
```c
// 理论上可行但未验证的方法：
// 将所有依赖函数包含在_end标签内，使用adrl获取相对地址
void flash_operation() {
    // 主要Flash操作代码
    // 可能通过adrl调用内嵌的辅助函数
}
// helper_function_1() { ... }
// helper_function_2() { ... }
asm("flash_operation_end:");
```

#### 5.3 多类型Flash支持 (payload.c:724-758)
```c
flash_fn_table:
# Flash type 1
.word identify_flash_1, identify_flash_1_end
.word erase_flash_1, erase_flash_1_end  
.word program_flash_1, program_flash_1_end
# Flash type 2-4...
```

系统支持4种不同的Flash芯片类型，每种类型都有独立的识别、擦除和编程函数，通过函数表实现统一接口。所有Flash操作函数都通过run_from_ram机制在栈上执行，确保ROM编程时的代码可访问性。

### 6. 存档数据结构

#### 6.1 完整系统状态保存
**rts_save函数**按以下顺序保存：

1. **SRAM原始数据** → 扇区7 (64KB)
2. **EWRAM** → 扇区0-3 (256KB) 
3. **VRAM前64KB** → 扇区4
4. **IWRAM + VRAM后32KB** → 扇区5
5. **调色板 + OAM + IO寄存器 + CPU寄存器状态** → 扇区6

#### 6.2 IO寄存器保存与恢复策略 (payload.c:31-134, 1304-1329, 601-651)

**保存策略：完整保存所有IO寄存器**
```c
// 完整保存IO寄存器范围（按字节保存，支持16bit和32bit寄存器）
// 1. 保存 0x04000000-0x04000060 (96字节)
for (uint32_t i = 0; i < 0x60; i++) {
    sram_io1[i] = io_base[i];
}

// 2. 保存 0x04000060-0x04000090 (48字节音频寄存器)  
for (uint32_t i = 0; i < 0x30; i++) {
    sram_audio[i] = audio_reg[i];
}

// 3. 保存 0x04000090-0x040003FE (880字节其他寄存器)
for (uint32_t i = 0; i < 0x370; i++) {
    sram_io2[i] = io_base2[i];
}
```

**恢复策略：选择性恢复关键寄存器**
```c
// 使用精心筛选的寄存器列表进行恢复
const uint16_t io_register_list[] = {
    0x0000,  // DISPCNT - LCD控制
    0x0008,  // BG0CNT - 背景控制  
    0x0080,  // SOUNDCNT_L - 音频控制
    // ... 69个精心选择的寄存器
    0xFF00   // 结束标记
};

// 16bit寄存器恢复（大部分IO寄存器）
for (int i = 0; reg_list[i] != 0xFF00; i++) {
    uint16_t offset = reg_list[i];
    uint16_t value = *(volatile uint16_t*)(flash_io + offset);
    io_base[offset / 2] = value;  // 按16bit写入
}

// 32bit寄存器恢复（背景变换寄存器）
volatile uint32_t *io32_base = (volatile uint32_t*)0x04000000;
io32_base[0x0028/4] = flash_io32[0x0028/4];  // BG2X (32bit)
io32_base[0x002C/4] = flash_io32[0x002C/4];  // BG2Y (32bit)
io32_base[0x0038/4] = flash_io32[0x0038/4];  // BG3X (32bit)
io32_base[0x003C/4] = flash_io32[0x003C/4];  // BG3Y (32bit)
```

**技术设计原理：**

1. **保存阶段**：完整保存所有IO区域，确保不丢失任何状态信息
   - 按字节保存，兼容16bit和32bit寄存器
   - 覆盖整个IO寄存器映射空间

2. **恢复阶段**：选择性恢复，避免副作用
   - 跳过只读寄存器（如VCOUNT, KEYINPUT等）
   - 跳过有写入副作用的寄存器（如定时器寄存器）
   - 根据寄存器类型使用正确的位宽写入：
     - 16bit寄存器：使用`io_base[offset/2]`写入
     - 32bit寄存器：使用`io32_base[offset/4]`写入

3. **兼容性保证**：
   - 16bit寄存器占大多数，按标准方式恢复
   - 32bit背景变换寄存器单独处理，确保Mode 1/2游戏正常运行
   - 音频和DMA寄存器通过专门的结构体恢复，保持时序正确

### 7. 读档恢复机制

#### 7.1 数据恢复顺序 (payload.c:532-722)
**load_from_flash函数**执行：

1. **验证RTS标志** - 检查扇区6末尾的"Ausar'S-RTSFILE."标志
2. **启用绿色交换** - 设置显示寄存器以指示读档操作
3. **按序恢复内存区域**:
   - EWRAM (256KB) ← 扇区0-3
   - VRAM (96KB) ← 扇区4-5
   - 调色板/OAM ← 扇区6 
4. **恢复IO寄存器** - 根据io_register_list恢复
5. **恢复CPU寄存器状态** - 包括SP/LR寄存器
6. **IWRAM最后恢复** - 纯汇编实现，避免栈破坏
7. **直接跳转到原始IRQ处理程序** - 无缝恢复游戏执行

#### 7.2 IWRAM栈保护机制的特殊处理 (payload.c:698-721)

**核心挑战：栈完整性保护**

IWRAM是GBA系统栈的存储位置，其恢复涉及复杂的栈完整性问题：

**栈增长特性分析：**
```
IWRAM布局 (0x03000000 - 0x03007FFF, 32KB):
├─ 0x03000000 ──┬─ 用户数据区
│               │
│               │  ↑ 栈向下增长
│               │  │
│               ├─ 当前栈顶位置（动态）
│               │
│               │  [函数调用栈空间]
│               │  [局部变量]
│               │  [返回地址]
│               │
└─ 0x03007FFF ──┴─ 栈底（固定）
```

**保存阶段：无特殊处理需求**
```c
// save_iwram_vram_back_to_flash (payload.c:1235-1255)
volatile uint8_t *iwram = (volatile uint8_t*)0x03000000;
for (uint32_t i = 0; i < 0x8000; i++) {
    sram[i] = iwram[i];  // 直接保存整个32KB IWRAM
}
```

**保存时机分析：**
- 恢复时间点：BIOS调用IRQ处理程序的时刻
- 栈单向增长：从高地址向低地址增长
- 函数调用影响：只会使用"保存时刻栈顶"以下的空间
- 安全性保证：保存时刻以上的内存区域不会被后续函数调用破坏

**恢复阶段：必须最后执行且纯汇编实现**

**问题根源：**
1. **栈变量冲突**：C函数的局部变量分配在栈上，恢复IWRAM会覆盖这些变量
2. **返回地址破坏**：函数的返回地址也在栈上，可能被覆盖
3. **栈位置不确定**：进入IRQ时原始栈位置动态变化

**解决方案：纯汇编IWRAM恢复** (payload.c:698-721)
```asm
// === 最后阶段：恢复IWRAM并跳转 ===
// 恢复IWRAM - 纯寄存器实现，避免使用栈变量
"ldr r12, %[cpu_regs]\n"
"ldr r2, %[sector_addr]\n"           // r2 = flash扇区4基地址
"mov r3, #0x03000000\n"             // r3 = IWRAM基地址
"mov r4, #0x8000\n"                 // r4 = 32KB计数器

"iwram_copy_loop:\n"                // 循环标签
"ldr r5, [r2], #4\n"                // 从flash读取4字节，并递增r2
"str r5, [r3], #4\n"                // 写入IWRAM，并递增r3
"subs r4, r4, #4\n"                 // 递减计数器
"bne iwram_copy_loop\n"             // 如果不为0，继续循环

// === 立即恢复CPU寄存器并跳转，避免栈破坏 ===
"ldmia r12!, {r3-r11,sp,lr}\n"      // 恢复IRQ模式寄存器
"mov r0, #0x04000000\n"
"ldr pc, [r0, #-(0x04000000-0x03FFFFF4)]\n"  // 直接跳转到原始IRQ处理程序
```

**关键技术要点：**

1. **纯寄存器操作**：
   - 完全使用寄存器进行IWRAM拷贝
   - 避免任何栈变量或函数调用
   - 确保不破坏栈上数据

2. **恢复顺序的关键性**：
   - IWRAM必须最后恢复（包含栈数据）
   - 恢复后立即跳转，避免继续使用可能被破坏的栈

3. **无返回设计**：
   - `load_from_flash`函数永远不返回
   - 恢复完成后直接跳转到原始IRQ处理程序
   - 原始IRQ处理完成后返回BIOS，回到游戏恢复点

4. **栈完整性保证**：
   - 恢复的是"保存时刻"的完整栈状态
   - 包括所有栈变量、返回地址、寄存器状态
   - 游戏从中断发生的精确时刻无缝继续执行

### 8. 内存映射与地址管理

#### 8.1 GBA内存布局
```
0x08000000 - ROM空间 (最大32MB)
0x02000000 - EWRAM (256KB) 
0x03000000 - IWRAM (32KB)
0x05000000 - 调色板RAM (1KB)
0x06000000 - VRAM (96KB)
0x07000000 - OAM (1KB)
0x0E000000 - SRAM (64KB, 可扩展)
```

#### 8.2 Flash映射策略
Payload通过相对地址访问避免链接器依赖：
```c
#define GET_REL_ADDR(symbol, var) \
    asm volatile("adrl %0, " #symbol : "=r"(var))
```

## 安全特性与兼容性

### 1. 游戏兼容性保证
- 只在检测到特定按键时激活
- 完整保存/恢复所有关键系统状态  
- 支持嵌套中断调用
- 自动检测并等待按键释放

### 2. 数据完整性
- RTS标志验证存档有效性
- Flash类型自动识别
- 分扇区独立操作，降低损坏风险

### 3. 向后兼容
- 保持原始SRAM功能
- 不修改游戏核心逻辑
- 支持已有的存档文件

## Payload编程约束与技术细节

### 1. 位置无关代码 (Position Independent Code)

**核心挑战：补丁注入位置不确定**

由于补丁可能被注入到ROM的任意合适位置，必须确保代码在任何地址都能正常执行：

**编译选项 (build.sh):**
```bash
# 启用位置无关代码生成
arm-none-eabi-gcc -mcpu=arm7tdmi -fPIC -fpie -fno-pic-data-text-rel
```

**函数寻址：**
- 函数调用通过相对地址自动解决
- PC相对寻址确保位置无关性

### 2. 全局变量访问约束

**问题根源：GOT表依赖**
- 直接访问全局变量会生成GOT (Global Offset Table)
- GOT依赖固定的链接地址，不适用于动态注入的补丁

**解决方案：相对地址访问宏**
```c
// 定义相对地址获取宏
#define GET_REL_ADDR(symbol, var) \
    asm volatile("adrl %0, " #symbol : "=r"(var))

#define GET_REL_VALUE(symbol, var) \
    asm volatile("ldr %0, " #symbol : "=r"(var))

// 使用示例
struct PayloadHeader *header;
GET_REL_ADDR(payload_header, header);  // 获取结构体地址

uint32_t flag_addr;
GET_REL_ADDR(rts_flag_string, flag_addr);  // 获取字符串地址
const char *flag_ptr = (const char*)flag_addr;
```

**全局变量定义约束：**
```c
// 必须是const类型（ROM只读）
// 必须手动指定.text段才能使用adrl索引
__attribute__((section(".text"))) const struct PayloadHeader payload_header = {
    .original_entrypoint = 0x080000c0,
    .save_size = 0x20000,
    .patched_entrypoint_addr = (uint32_t)patched_entrypoint
};

__attribute__((section(".text"))) const char rts_flag_string[] = "Ausar'S-RTSFILE.";
```

### 3. Run From RAM的地址解析机制

**静态地址转换问题：**
- 函数表存储的是静态绝对地址（链接时确定）
- 补丁注入后，实际地址 = 静态地址 + 注入偏移

**巧妙的解决方案：**
```c
// 链接器脚本：文件头放在0x0位置
// 静态绝对地址 = 相对文件头的偏移地址

// 运行时地址计算
struct PayloadHeader *header;
GET_REL_ADDR(payload_header, header);  // 获取当前补丁基地址

flash_functions_t *flash_funcs;
GET_REL_ADDR(flash_fn_table, flash_funcs);

// 计算实际函数地址
uint32_t actual_func_addr = flash_funcs[i].identify_start + (uint32_t)header;
//                         ↑ 静态偏移地址        ↑ 运行时基地址
```

**技术原理：**
1. 链接器脚本将payload header置于文件开头（偏移0x0）
2. 函数表中的地址实际是相对文件头的偏移
3. 运行时获取header实际地址，即为补丁注入的基地址
4. 静态偏移 + 运行时基地址 = 实际函数地址

### 4. ARM vs Thumb模式约束

**必须全ARM模式编译：**
```bash
# 编译选项强制ARM模式
arm-none-eabi-gcc -marm -mno-thumb-interwork
```

**技术限制分析：**

1. **ADRL指令限制**：
   ```c
   // ADRL只能在ARM模式使用
   asm volatile("adrl %0, " #symbol : "=r"(var))  // ARM ✓, Thumb ✗
   ```

2. **模式切换的地址依赖**：
   ```c
   // ARM/Thumb混合编译时，GCC生成的interwork代码包含绝对地址
   // 补丁位置变化时，这些绝对地址会失效
   
   // 错误示例（会产生绝对地址）：
   void thumb_function() __attribute__((target("thumb")));  // ✗
   void arm_function() __attribute__((target("arm")));      // ✗ 混合使用
   
   // 正确做法：全ARM模式
   void arm_function() __attribute__((target("arm")));      // ✓ 纯ARM
   ```

3. **代码大小vs稳定性权衡**：
   - Thumb模式可节省约30%空间
   - 但会引入绝对地址依赖，破坏位置无关性
   - 选择ARM模式确保稳定性

### 5. 内联汇编最佳实践

**寄存器使用策略：**
```c
// 明确指定寄存器破坏列表
asm volatile(
    "mov r0, %[input]\n"
    "bl some_function\n"
    : [output] "=r" (result)
    : [input] "r" (input_value) 
    : "r0", "r1", "r2", "lr", "memory"  // 明确列出所有被破坏的寄存器
);
```

**内存屏障：**
```c
// 涉及硬件寄存器访问时添加memory屏障
asm volatile("" : : : "memory");
```

## 构建与部署

### 构建流程
1. `payload.c` → `payload.bin` (纯ARM二进制代码，位置无关)
2. `payload.bin` → `payload_bin.h` (C数组格式)
3. `patcher.c` + `payload_bin.h` → 可执行的patcher

### 使用方法
```bash
./patcher game.gba [save.rts]
# 输出: game_keypad.gba
```

**按键操作：**
- L+R+START: 实时存档
- L+R+SELECT: 实时读档

## 技术创新点

1. **无依赖设计**: payload使用纯ARM汇编和相对地址，避免链接器依赖
2. **多Flash兼容**: 支持4种Flash芯片的统一抽象层
3. **精确状态保存**: 完整保存GBA系统状态，确保读档后游戏无缝继续
4. **中断嵌套支持**: 支持在RTS操作期间的其他中断处理
5. **自适应ROM扩展**: 自动识别ROM大小并适配注入位置

这个系统展示了底层系统编程的精妙之处，通过深度理解GBA硬件架构实现了强大的功能扩展。