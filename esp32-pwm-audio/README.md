# ESP32 蜂鸣器音乐播放器

这是一个基于 PlatformIO 的 ESP32 工程，可播放 Bad Apple 黑白视频，并通过低电平触发的无源蜂鸣器模块输出合成音乐。当前默认是不依赖屏幕和 SPIFFS 的纯音频 LEDC 模式。

## 接线

- 蜂鸣器模块 `IN`：GPIO25
- 蜂鸣器模块 `GND`：ESP32 `GND`
- 蜂鸣器模块 `VCC`：按照模块标注的电压供电

蜂鸣器模块必须与 ESP32 共地。如果使用的是没有驱动电路的裸无源蜂鸣器，并且电流超过 GPIO 的允许范围，必须增加三极管或 MOSFET，不能直接用 GPIO 驱动。

## 当前音频方案

默认启用 `AUDIO_OUTPUT_PDM=0`，使用与低电平触发无源蜂鸣器模块更匹配的 LEDC 方波。该模式从旋律当前最多四个音符中选择最高音播放，声音简单，但音符清楚且不会让模块处理 MHz 级开关信号。

LEDC 方案已经做了以下优化：

- 直接按 12 平均律计算音符频率，并设置 LEDC 10.8 位硬件分频器
- B2～G5 范围内理论最大音高误差约 0.034 音分
- 音符开始和结束使用约 4 ms 的 LEDC 硬件淡入淡出，减轻切换爆音
- 使用独立 FreeRTOS 任务和微秒绝对时间推进 138 BPM 节拍，不依赖 `loop()` 轮询

## 实验性 PDM 合成器

设置 `AUDIO_OUTPUT_PDM=1` 后，程序会使用 ESP32 的 I2S0 硬件，把 24 kHz、16 位 PCM 采样转换成 6.144 MHz 的单线 PDM 信号，再从 GPIO25 输出。

合成器包含：

- 32 位 DDS，相比整数赫兹 LEDC 音调更准确
- 最多四个声部同时发声
- 64 点正弦表加线性插值
- 约 4 ms 起音和 12 ms 释音包络，减少音符切换爆音
- LFSR 噪声打击乐
- DMA 独立音频任务，播放时序不受 `loop()` 和屏幕刷新影响

这仍然是根据音符数据实时合成的版本，不是原版录音。

> 注意：PDM 引脚会以很高的频率翻转。部分廉价低电平触发模块的三极管只适合普通音频 PWM，不一定适合 MHz 级开关。第一次测试建议把 `BUZZER_VOLUME` 设为 30；程序会随音量降低 PDM 的低电平平均占比。播放几十秒后检查模块是否异常发热，如果明显发热，请立即断电并把 `AUDIO_OUTPUT_PDM` 改回 `0`。

## 配置

在 `platformio.ini` 中修改：

```ini
-D BUZZER_PIN=25
-D BUZZER_VOLUME=60
-D AUDIO_ONLY_MODE=1
-D AUDIO_OUTPUT_PDM=0
```

- `BUZZER_PIN`：音频输出引脚
- `BUZZER_VOLUME`：音量，范围 0～100
- `AUDIO_ONLY_MODE=1`：只播放音乐，不初始化屏幕或 SPIFFS
- `AUDIO_ONLY_MODE=0`：播放视频，并在第 46 帧启动音乐
- `AUDIO_OUTPUT_PDM=0`：适合当前蜂鸣器模块的单声部 LEDC 方波驱动
- `AUDIO_OUTPUT_PDM=1`：实验性的四声部 I2S-PDM 合成器，建议配合合适的滤波、功放和扬声器

当前没有连接屏幕时应保留 `AUDIO_ONLY_MODE=1`。

## LEDC 驱动

当前使用 `PassiveBuzzer` 驱动。该模式只播放当前最高音，忽略打击乐，但输出频率较低，对常见低电平触发蜂鸣器模块更合适。

单独测试 LEDC 驱动时还可以设置：

```ini
-D BUZZER_SELF_TEST=1
-D AUDIO_OUTPUT_PDM=0
```

`PassiveBuzzer::playTone()` 是非阻塞函数，音调会持续到播放新音调或调用 `stop()`：

```cpp
buzzer.playTone(1000, 70);
delay(200);
buzzer.stop();
```

## 编译和烧录

在 `esp32-pwm-audio` 目录执行：

```shell
pio run
pio run -t upload
pio device monitor -b 115200
```

纯音频模式启动后，串口应输出：

```text
Audio-only mode: playing Bad Apple melody
```

## 数据来源

Bad Apple 压缩旋律数据来自 MIT 授权的 [cnlohr/badderapple](https://github.com/cnlohr/badderapple) 项目，详细声明见 `THIRD_PARTY_NOTICES.md`。
