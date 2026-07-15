# ESP32 PWM 音频

这是一个基于 PlatformIO 的 ESP32 工程，用于播放视频并通过低电平触发的无源蜂鸣器输出声音。

## 蜂鸣器接线

- `IN`（信号端）：默认连接 GPIO25
- `GND`：连接 ESP32 的 GND
- `VCC`：按照蜂鸣器模块标注的工作电压供电

如需使用其他 GPIO，请修改 `platformio.ini` 中的 `BUZZER_PIN`。蜂鸣器模块必须与 ESP32 共地。

如果使用的不是带驱动电路的蜂鸣器模块，而是裸无源蜂鸣器，并且工作电流超过 ESP32 GPIO 的允许范围，则必须通过三极管或 MOSFET 驱动，不能直接连接 GPIO。

## 驱动使用方法

程序在 `setup()` 中初始化全局 `buzzer` 对象。初始化完成后，信号端保持高电平，蜂鸣器不会在开机时自动鸣叫。驱动占用 LEDC 通道 0。

```cpp
buzzer.playTone(1000, 70); // 播放 1kHz 音调，音量为 70%
delay(200);
buzzer.stop();             // 停止播放，信号端恢复高电平
```

支持的音调频率范围为 20Hz～20kHz，音量范围为 0～100。`playTone()` 是非阻塞函数；调用后音调会持续播放，直到播放新的音调或调用 `stop()`。

## 驱动接口

- `begin()`：初始化 PWM，并将蜂鸣器置于静音状态
- `playTone(frequencyHz, volumePercent)`：开始播放指定频率和音量的音调
- `setVolume(volumePercent)`：调整当前音调的音量
- `stop()`：停止播放，并将信号端恢复为非触发电平
- `isPlaying()`：查询当前是否正在播放
