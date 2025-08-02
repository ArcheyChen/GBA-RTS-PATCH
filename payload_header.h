#ifndef PAYLOAD_HEADER_H
#define PAYLOAD_HEADER_H

#include <stdint.h>

// Payload头部结构体定义
// 注意：这个结构体必须在payload.c文件的最开始位置
// 并且必须强制放在.text段，以确保位置固定
// 使用packed属性确保紧密打包，避免任何对齐问题
struct __attribute__((packed)) PayloadHeader {
    uint32_t original_entrypoint;      // 游戏原始入口点地址 (默认: 0x080000c0)
    uint32_t save_size;                // 存档大小 (默认: 0x20000 = 128KB)
                                       // 可被patcher覆盖为实际值（如64KB）
    uint32_t patched_entrypoint_addr;  // 补丁入口点函数地址
};

#endif // PAYLOAD_HEADER_H