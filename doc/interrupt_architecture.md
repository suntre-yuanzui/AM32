# 中断处理架构详解

## 📋 中断优先级和分类

### 关键中断列表（按优先级排序）

| 中断名称 | 触发频率 | 优先级 | 功能 | 核心函数 |
|---------|---------|--------|------|---------|
| **比较器中断** | ~30kHz | 最高 | 过零检测 | `handleCompInterrupt()` |
| **换相定时器** | ~15kHz | 高 | 执行换相 | `handleCommutationTimerInterrupt()` |
| **控制循环** | 20kHz | 中 | 主控制 | `handleControlLoopInterrupt()` |
| **ADC DMA** | 10kHz | 中低 | 数据采集 | `handleAdcDmaInterrupt()` |
| **输入DMA** | 1-8kHz | 低 | 信号捕获 | `handleInputDmaInterrupt()` |

---

## 🎯 中断 #1: 比较器中断（过零检测）

### 硬件触发源
- 反电动势比较器输出变化
- 通过EXTI连接到中断控制器
- 悬空相电压穿越虚拟中点时触发

### 中断流程
```c
硬件比较器输出翻转
    ↓
ADC1_CMP_IRQHandler()  ← MCU特定中断向量
    ↓
handleCompInterrupt()  ← 通用中断逻辑
    ↓
isCompInterruptValid() ← 时序验证
    |
    ├─ 有效 → interruptRoutine()
    |           ↓
    |       记录过零时间
    |           ↓
    |       启动换相定时器
    |
    └─ 无效 → 清除中断，忽略
```

### 关键验证逻辑
```c
uint8_t isCompInterruptValid(void) {
    // 检查1: 必须在半个周期之后
    if (INTERVAL_TIMER_COUNT > (average_interval >> 1)) {
        return 1;  // 时间窗口正确
    }
    
    // 检查2: 如果时间太早，必须确认比较器输出
    if (getCompOutputLevel() == rising) {
        return 1;  // 比较器确认
    }
    
    return 0;  // 拒绝此中断
}
```

### 为什么需要验证？
1. **EMI干扰**: 电机开关噪声可能触发假过零
2. **时序保护**: 防止在换相后立即再次触发
3. **稳定性**: 只在BEMF稳定后检测

### 时序参数
- **最早触发**: `average_interval / 2` 之后
- **典型延迟**: 0-10μs（取决于filter_level）
- **中断执行时间**: ~5μs

---

## 🎯 中断 #2: 换相定时器中断

### 硬件触发源
- TMR16（或COM_TIMER）溢出
- 单次触发模式
- 由`interruptRoutine()`设置触发时间

### 中断流程
```c
定时器计数到waitTime
    ↓
TMR16_GLOBAL_IRQHandler()  ← MCU特定
    ↓
handleCommutationTimerInterrupt()
    ↓
clearCommutationTimerFlag()
    ↓
PeriodElapsedCallback()
    ↓
    ├─ 执行换相: commutate()
    |      ↓
    |   comStep(step)  ← 切换MOS管
    |      ↓
    |   changeCompInput()  ← 切换监测相
    |
    ├─ 更新间隔: commutation_interval
    |
    ├─ 计算超前角: advance
    |
    ├─ 计算等待时间: waitTime
    |
    └─ 使能比较器中断
```

### 时序计算详解
```c
// 1. 平滑换相间隔
commutation_interval = (old_interval + new_zc_time) / 2;

// 2. 计算超前角（单位：定时器tick）
// advance_level范围: 10-42 (对应5.6°-23.6°电角度)
advance = (commutation_interval * advance_level) / 64;

// 3. 等待时间 = 半个周期 - 超前角
// 理论上过零点在60°，等待30°后换相（总90°）
waitTime = (commutation_interval / 2) - advance;
```

### 电角度关系
```
前一次换相               过零点               本次换相
    |                     |                     |
    0°                   60°                   90°
    |---------------------|---------------------|
         60° (1个步)    ↑           ↑
                    detectZC    waitTime到期
                                (30° - advance)
```

### 中断执行时间
- **典型**: 8-12μs
- **包含**: 相位切换、比较器配置、时间计算

---

## 🎯 中断 #3: 控制循环中断（20kHz）

### 硬件触发源
- TMR14（或CTRL_LOOP_TIMER）
- 周期: 50μs (20kHz)
- 连续运行，不间断

### 中断流程
```c
每50μs触发
    ↓
TMR14_GLOBAL_IRQHandler()
    ↓
handleControlLoopInterrupt()
    ↓
tenKhzRoutine()  ← 主控制逻辑
    ↓
    ├─ 输入处理
    |   ├─ 读取遥控信号
    |   ├─ DSHOT命令处理
    |   └─ 输入映射/限制
    |
    ├─ 占空比控制
    |   ├─ 斜坡限制 (max_duty_cycle_change)
    |   ├─ 温度/电压保护
    |   └─ PWM更新
    |
    ├─ PID控制循环 (1kHz子循环)
    |   ├─ 速度控制 (drive_by_rpm)
    |   ├─ 电流限制 (current_limit)
    |   └─ 失速保护 (stall_protection)
    |
    ├─ 状态管理
    |   ├─ 武装/解锁
    |   ├─ 启动检测
    |   └─ 超时保护
    |
    └─ 遥测准备
        ├─ RPM计算
        ├─ 功率统计
        └─ 错误标志
```

### 子循环频率分配
```c
static uint16_t loop_counter = 0;

void tenKhzRoutine() {
    loop_counter++;
    
    // 每次执行 (20kHz)
    processInput();
    adjustDutyCycle();
    
    // 每2次执行 (10kHz)
    if (loop_counter % 2 == 0) {
        updatePWM();
    }
    
    // 每20次执行 (1kHz)
    if (loop_counter % 20 == 0) {
        runPIDLoops();
        processTelemetry();
    }
    
    // 每200次执行 (100Hz)
    if (loop_counter % 200 == 0) {
        checkTemperature();
        updateLED();
    }
}
```

### 关键功能：占空比斜坡
```c
// 防止占空比突变导致电流冲击
if (duty_cycle_setpoint > duty_cycle) {
    duty_cycle += max_duty_cycle_change;  // 逐步增加
} else {
    duty_cycle -= max_duty_cycle_change;  // 逐步减小
}

// max_duty_cycle_change 典型值: 1-5 (每50μs)
// 全油门时间: 2000 / 2 / 20000 ≈ 50ms
```

### 中断执行时间
- **最小**: 3μs（空闲）
- **典型**: 15μs（正常运行）
- **最大**: 35μs（含PID和遥测）

---

## 🎯 中断 #4: ADC DMA中断

### 硬件触发源
- ADC转换完成
- DMA自动传输到内存数组
- 转换频率: 10kHz

### 测量通道
```c
uint16_t ADC_raw_buffer[4];  // DMA目标缓冲区

ADC_CHANNEL_0: Battery Voltage (分压器)
ADC_CHANNEL_1: Phase Current (霍尔传感器)
ADC_CHANNEL_2: Temperature (内部温度传感器)
ADC_CHANNEL_3: Input Signal (可选，ADC输入模式)
```

### 中断流程
```c
ADC触发 → 多通道转换 → DMA传输完成
    ↓
DMA1_Channel1_IRQHandler()
    ↓
handleAdcDmaInterrupt()
    ↓
ADC_DMA_Callback()
    ↓
    ├─ 电压处理
    |   ADC_raw_volts → battery_voltage
    |   检查低压保护
    |
    ├─ 电流处理
    |   ADC_raw_current → actual_current
    |   平滑滤波 (50样本)
    |
    ├─ 温度处理
    |   ADC_raw_temp → degrees_celsius
    |   检查过温保护
    |
    └─ 输入处理（ADC模式）
        ADC_raw_input → newinput
```

### 数据转换公式
```c
// 电压转换 (mV)
battery_voltage = (ADC_raw_volts * 3300 * VOLTAGE_DIVIDER) / 4095;

// 电流转换 (mA) - 假设50mV/A霍尔传感器
actual_current = ((ADC_raw_current - 2048) * 3300 * 20) / 4095;

// 温度转换 (°C) - STM32内部传感器
degrees_celsius = ((ADC_raw_temp * 3300 / 4095) - 760) / 2.5 + 25;
```

### 滤波和平滑
```c
// 移动平均滤波 (50样本)
smoothed_current = (smoothed_current * 49 + actual_current) / 50;

// 或使用循环缓冲区
readings[readIndex] = ADC_raw_current;
readIndex = (readIndex + 1) % 50;
average = sum(readings) / 50;
```

---

## 🎯 中断 #5: 输入捕获DMA中断

### 硬件触发源
- 定时器输入捕获（TIM15或TIM3）
- DMA捕获每个边沿的定时器值
- 用于DSHOT和伺服PWM解码

### DSHOT协议时序
```
DSHOT600: 每bit 1.67μs
  '1': 1.25μs高 + 0.42μs低
  '0': 0.625μs高 + 1.04μs低

帧格式: [11bit油门][1bit遥测请求][4bit CRC]
```

### 中断流程
```c
输入信号边沿 → 定时器捕获 → DMA传输
    ↓
DMA传输完成 (16个边沿)
    ↓
DMA1_Channel5_4_IRQHandler()
    ↓
handleInputDmaInterrupt()
    ↓
transfercomplete()  ← 设置标志
    ↓
触发软件中断 (EXTI15)
    ↓
EXINT15_4_IRQHandler()
    ↓
handleDshotProcessInterrupt()
    ↓
processDshot()
    ↓
    ├─ 解码时序数据
    |   计算每bit宽度
    |
    ├─ 提取数据
    |   throttle = bits[0:10]
    |   telemetry_req = bits[11]
    |   crc = bits[12:15]
    |
    ├─ 验证CRC
    |   if (crc_valid) accept
    |
    └─ 更新输入
        newinput = throttle * 2
```

### 为什么使用两级中断？
1. **DMA中断**: 快速禁用DMA，避免覆盖数据
2. **软件中断**: 在低优先级处理复杂解码
3. **解耦**: DMA可以立即准备下一帧

### 伺服PWM模式
```c
// 只捕获上升沿到下降沿
// 典型: 1000-2000μs脉宽

if (!dshot && servoPwm) {
    pulse_width = dma_buffer[1] - dma_buffer[0];
    
    if (pulse_width > 1000 && pulse_width < 2000) {
        newinput = map(pulse_width, 1000, 2000, 0, 2047);
    }
}
```

---

## 🛡️ 中断安全和优先级管理

### 临界区保护
```c
// 换相时禁止DSHOT中断
__disable_irq();
if (!prop_brake_active) {
    comStep(step);  // 快速执行相位切换
}
__enable_irq();

// 或使用NVIC优先级分组
NVIC_SetPriority(CMP_IRQn, 0);      // 最高
NVIC_SetPriority(TMR16_IRQn, 1);    // 高
NVIC_SetPriority(TMR14_IRQn, 2);    // 中
NVIC_SetPriority(DMA1_CH1_IRQn, 3); // 中低
```

### 中断嵌套策略
```c
优先级0 (比较器):
  - 可以中断所有其他ISR
  - 执行时间 < 5μs
  - 只做时间记录

优先级1 (换相定时器):
  - 可以中断控制循环和DMA
  - 执行时间 < 12μs
  - 执行相位切换

优先级2 (控制循环):
  - 可以中断DMA
  - 执行时间 < 35μs
  - 主控制逻辑

优先级3 (DMA):
  - 最低优先级
  - 执行时间 < 8μs
  - 数据处理可延迟
```

### CPU负载估算
```c
每秒中断次数:
- 比较器: 30,000次 × 5μs = 150ms
- 换相定时器: 15,000次 × 12μs = 180ms
- 控制循环: 20,000次 × 15μs = 300ms
- ADC DMA: 10,000次 × 5μs = 50ms
- 输入DMA: 2,000次 × 3μs = 6ms

总计: 686ms / 1000ms = 68.6% CPU占用
剩余: 31.4% 用于主循环和后台任务
```

---

## 🔧 移植时的中断配置

### 1. 定义MCU特定的定时器
```c
// 在 targets.h 或 mcu_config.h 中
#define COM_TIMER              TMR16
#define CTRL_LOOP_TIMER        TMR14
#define INTERVAL_TIMER         TMR1
#define INPUT_CAPTURE_TIMER    TMR15

#define INTERVAL_TIMER_COUNT   (INTERVAL_TIMER->cval)
#define SET_INTERVAL_TIMER_COUNT(x)  (INTERVAL_TIMER->cval = x)
```

### 2. 实现清除标志函数
```c
// 在 interrupt_handlers.c 中根据MCU调整
void clearCompInterruptFlag(void) {
#ifdef ARTERY
    EXINT->intsts = EXTI_LINE;
#elif defined(STMICRO)
    EXTI->PR = EXTI_LINE;
#elif defined(GIGADEVICES)
    EXTI_PD = EXTI_LINE;
#endif
}
```

### 3. 配置NVIC优先级
```c
void setupInterruptPriorities(void) {
    NVIC_SetPriorityGrouping(3);  // 4 bits preemption
    
    NVIC_SetPriority(CMP_IRQn, 0);      // 最高
    NVIC_SetPriority(TMR16_IRQn, 1);
    NVIC_SetPriority(TMR14_IRQn, 2);
    NVIC_SetPriority(DMA1_CH1_IRQn, 3);
    NVIC_SetPriority(DMA1_CH5_IRQn, 3);
}
```

### 4. 调试工具
```c
// 添加性能监测
volatile uint32_t isr_count[5] = {0};
volatile uint32_t isr_max_time[5] = {0};

void handleCompInterrupt(void) {
    uint32_t start = DWT->CYCCNT;
    // ... 中断逻辑 ...
    uint32_t elapsed = DWT->CYCCNT - start;
    
    isr_count[0]++;
    if (elapsed > isr_max_time[0]) {
        isr_max_time[0] = elapsed;
    }
}
```

---

## ⚠️ 常见中断问题

### Q1: 频繁过零误触发
**症状**: 电机不稳定，RPM跳变  
**原因**: EMI干扰或filter_level太低  
**解决**: 
```c
filter_level = 8;  // 增加到8-10
bad_count_threshold = 5;  // 增加容错
```

### Q2: 换相定时器未触发
**症状**: 电机启动后停止  
**原因**: waitTime计算错误或定时器未使能  
**调试**:
```c
// 检查waitTime是否合理
if (waitTime < 10 || waitTime > 65535) {
    // 错误！
}
```

### Q3: 控制循环卡死
**症状**: LED不闪烁，无响应  
**原因**: 中断执行时间过长  
**解决**: 减少20kHz循环中的复杂计算

### Q4: DMA数据丢失
**症状**: ADC读数为0或固定值  
**原因**: DMA未正确配置或缓冲区覆盖  
**检查**:
```c
// 确保DMA循环模式
DMA_CHANNEL->ctrl_bit.cmode = TRUE;
```

---

## 📝 移植检查清单

- [ ] 定义所有定时器宏
- [ ] 实现clearXXXFlag()函数
- [ ] 配置NVIC优先级
- [ ] 验证中断向量表
- [ ] 测试各中断独立触发
- [ ] 检查中断嵌套行为
- [ ] 测量ISR执行时间
- [ ] 验证DMA配置
- [ ] 测试过零检测时序
- [ ] 确认换相同步性

完成这些后，中断系统应该能正常工作！
