# 六步换相核心代码移植指南

## 📁 文件说明

已提取的核心模块文件：

### 换相控制模块
- **`Inc/commutation.h`** - 换相模块头文件
- **`Src/commutation.c`** - 换相模块实现（6个核心函数）

### 中断处理模块
- **`Inc/interrupt_handlers.h`** - 中断处理头文件
- **`Src/interrupt_handlers.c`** - 中断处理实现（5个中断类型）

## 🔧 核心函数

### 1. `commutate()`
**功能**: 执行六步换相序列  
**调用时机**: 由定时器中断在计算的时机触发  
**关键操作**:
- 更新步进计数器 (step 1-6)
- 切换三相MOS管状态 `comStep(step)`
- 更换比较器监测的悬空相 `changeCompInput()`

### 2. `getBemfState()`
**功能**: 轮询模式下检测反电动势状态  
**调用时机**: 在主循环中持续调用（低速/启动时）  
**关键操作**:
- 读取悬空相的电平状态
- 累积有效检测次数到 `bemfcounter`
- 过滤噪声干扰

### 3. `interruptRoutine()`
**功能**: 中断模式下处理过零事件  
**调用时机**: 比较器检测到过零时触发  
**关键操作**:
- 多次采样过滤 (filter_level次)
- 记录过零时间戳
- 启动换相定时器

### 4. `PeriodElapsedCallback()`
**功能**: 换相定时器回调  
**调用时机**: 等待时间到达后触发  
**关键操作**:
- 执行换相 `commutate()`
- 计算下次换相间隔
- 应用超前角补偿

### 5. `startMotor()`
**功能**: 启动电机  
**调用时机**: 检测到有效输入信号后  
**关键操作**:
- 执行第一次换相
- 设置初始换相间隔
- 使能过零检测中断

### 6. `zcfoundroutine()`
**功能**: 轮询模式过零处理  
**调用时机**: 低速运行时在主循环调用  
**关键操作**:
- 设置换相定时器
- 判断是否切换到中断模式

## 🔌 移植步骤

### Step 1: 硬件抽象层适配

需要实现以下硬件相关函数（在 `comparator.c` 和 `phaseouts.c` 中）:

```c
/* 比较器控制 */
uint8_t getCompOutputLevel(void);     // 读取比较器输出
void changeCompInput(void);            // 切换比较器输入源
void enableCompInterrupts(void);       // 使能比较器中断
void maskPhaseInterrupts(void);        // 屏蔽比较器中断

/* 相位输出控制 */
void comStep(int step);                // 执行六步换相
void phaseAPWM/FLOAT/LOW(void);        // A相控制
void phaseBPWM/FLOAT/LOW(void);        // B相控制
void phaseCPWM/FLOAT/LOW(void);        // C相控制

/* 定时器控制宏 */
#define SET_INTERVAL_TIMER_COUNT(x)    // 设置间隔定时器值
#define INTERVAL_TIMER_COUNT           // 读取间隔定时器当前值
#define SET_AND_ENABLE_COM_INT(x)      // 设置并使能换相定时器
#define DISABLE_COM_TIMER_INT()        // 禁用换相定时器中断
```

### Step 2: 变量声明

在主程序中声明以下全局变量:

```c
/* 状态变量 */
char step = 1;                    // 当前换相步骤 (1-6)
char forward = 1;                 // 旋转方向 (1=正转, 0=反转)
char rising = 1;                  // 期望检测的边沿 (1=上升, 0=下降)
char running = 0;                 // 电机运行标志
char old_routine = 1;             // 模式标志 (1=轮询, 0=中断)

/* 过零检测 */
uint8_t bemfcounter = 0;          // 反电动势有效计数
uint8_t bad_count = 0;            // 错误计数
uint8_t filter_level = 5;         // 过滤级别

/* 时间变量 */
uint16_t lastzctime = 0;          // 上次过零时间
uint16_t thiszctime = 0;          // 本次过零时间
uint16_t waitTime = 0;            // 换相等待时间
uint16_t advance = 0;             // 超前角(时间单位)

/* 间隔统计 */
uint32_t commutation_interval = 0;      // 当前换相间隔
uint32_t average_interval = 0;          // 平均换相间隔
uint32_t zero_crosses = 0;              // 过零次数
uint16_t commutation_intervals[6];      // 6步间隔记录
```

### Step 3: 中断处理配置

已提取到 `interrupt_handlers.c`，只需在MCU特定的 `xxx_it.c` 中调用：

#### 比较器中断 (过零检测):
```c
void ADC1_CMP_IRQHandler(void) {
    handleCompInterrupt();  // 自动处理时序验证和过零检测
}
```

#### 换相定时器中断:
```c
void TMR16_GLOBAL_IRQHandler(void) {
    handleCommutationTimerInterrupt();  // 执行换相
}
```

#### 控制循环定时器中断 (20kHz):
```c
void TMR14_GLOBAL_IRQHandler(void) {
    handleControlLoopInterrupt();  // 主控制循环
}
```

#### ADC DMA中断:
```c
void DMA1_Channel1_IRQHandler(void) {
    if (dma_flag_get(DMA1_FDT1_FLAG) == SET) {
        DMA1->clr = DMA1_GL1_FLAG;
        handleAdcDmaInterrupt();  // 处理ADC数据
    }
}
```

#### 输入捕获DMA中断 (DSHOT/PWM):
```c
void DMA1_Channel5_4_IRQHandler(void) {
    if (dma_flag_get(DMA1_FDT5_FLAG) == SET) {
        DMA1->clr = DMA1_GL5_FLAG;
        handleInputDmaInterrupt();  // 处理输入信号
    }
}
```

### Step 4: 主循环集成

```c
while(1) {
    // 计算平均换相间隔
    e_com_time = ((commutation_intervals[0] + ... + 
                   commutation_intervals[5]) + 4) >> 1;
    
    // 低速轮询模式
    if (old_routine) {
        getBemfState();
        i和中断架构

### 启动阶段 (Polling Mode):
```
主循环 → getBemfState() → bemfcounter++ → zcfoundroutine() 
  → 设置定时器 → 等待 → commutate() → 下一步
```

### 高速阶段 (Interrupt Mode):
```
硬件比较器 → ADC1_CMP_IRQHandler()
               ↓
           handleCompInterrupt()
               ↓
           isCompInterruptValid() (时序验证)
               ↓
           interruptRoutine() (记录时间)
               ↓
           启动换相定时器(waitTime)
               ↓
           TMR16_GLOBAL_IRQHandler()
               ↓
           handleCommutationTimerInterrupt()
               ↓
           PeriodElapsedCallback()
               ↓
           commutate() → comStep(step)
               ↓
           enableCompInterrupts() → 等待下次过零
```

### 控制循环 (20kHz 持续运行):
```
TMR14 (50μs周期) → TMR14_GLOBAL_IRQHandler()
                      ↓
                  handleControlLoopInterrupt()
                      ↓
                  tenKhzRoutine()
                      ↓
    ┌─────────────────┼─────────────────┐
    ↓                 ↓                 ↓
输入处理        占空比调节          PID控制
遥控信号        斜坡限制           速度/电流
DSHOT解码       PWM更新            过载保护
```

### DMA中断流程:
```
ADC转换 → DMA传输 → DMA1_CH1_IRQ → handleAdcDmaInterrupt()
                                      ↓
                                  ADC_DMA_Callback()
                                      ↓
                                  更新: 电压/电流/温度

输入捕获 → DMA传输 → DMA1_CH5_IRQ → handleInputDmaInterrupt()
                                       ↓
                                   transfercomplete()
                                       ↓
                                   触发DSHOT处理中断

```c
/* 超前角设置 */
uint8_t temp_advance = 15;              // 固定超前角 (15/64 ≈ 14°)
uint8_t auto_advance_level = 20;        // 自动超前角

/* 过零检测参数 */
uint8_t min_bemf_counts_up = 8;         // 上升沿最小计数
uint8_t min_bemf_counts_down = 8;       // 下降沿最小计数
uint8_t bad_count_threshold = 4;        // 错误计数阈值

/* 模式切换阈值 */
uint32_t polling_mode_changeover = 2000; // 轮询→中断切换阈值
```

## 📊 时序图

```
启动阶段 (Polling Mode):
  主循环 → getBemfState() → bemfcounter++ → zcfoundroutine() 
    → 设置定时器 → 等待 → commutate() → 下一步

高速阶段 (Interrupt Mode):
  比较器中断 → interruptRoutine() → 记录时间 → 启动定时器
    → PeriodElapsedCallback() → commutate() → 使能中断 → 等待下次过零
```

## 🚀 快速测试

1. **验证换相序列**: 
   ```c
   for(int i=1; i<=6; i++) {
       step = i;
       comStep(step);
       delayMillis(100);  // 应看到电机缓慢旋转
   }
   ```

2. **验证过零检测**:
   ```c
   while(1) {
       getBemfState();
       if(bemfcounter > 5) break;  // 检测到5次有效过零
   }
   ```

3. **完整启动测试**:
   ```c
   startMotor();
   // 观察电机是否能自行加速并切换到中断模式
   ```

## ⚠️ 常见问题

**Q: 电机启动后立即失步**  
A: 检查 `polling_mode_changeover` 值是否太小，尝试增大到5000

**Q: 高速时噪音大/震动**  
A: 调整 `advance` 超前角，或增加 `filter_level`

**Q: 低速时转不动**  
A: 降低 `min_bemf_counts`，增加启动功率 `startup_max_duty_cycle`

## 📝 版本信息

- **提取日期**: 2026-01-15
- **源代码**: AM32 v2.16
- **适用MCU**: STM32F0/G0/G4/L4, AT32F4, GD32E2/F3
