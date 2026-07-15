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

## Bad Apple 旋律播放

工程内置了为微控制器压缩的 Bad Apple 合成音符数据。视频播放到第 46 帧后开始播放旋律，并以 30FPS、138BPM 的时间关系按视频帧推进，从而避免视频和声音逐渐失去同步。

无源蜂鸣器只能同时产生一个频率，因此程序会从原音轨最多四个同时发声的音符中选择最高音播放，并忽略打击乐轨。播放效果是可辨认的单声道方波旋律，不是官方录音。

旋律数据来自 MIT 授权的 [cnlohr/badderapple](https://github.com/cnlohr/badderapple) 项目，详细版权声明见 `THIRD_PARTY_NOTICES.md`。

## 纯音频模式

当前在 `platformio.ini` 中启用了纯音频测试：

```ini
-D AUDIO_ONLY_MODE=1
```

ESP32 启动后会跳过 OLED 初始化、SPIFFS 和视频解码，直接使用系统时间播放整首旋律，因此不需要连接屏幕或上传 `video.hs`。

需要恢复视频同步播放时，将配置改为：

```ini
-D AUDIO_ONLY_MODE=0
```

## 开机声音测试

驱动自检默认已经关闭。如需再次播放 C5、E5、G5 三个测试音，可在 `platformio.ini` 中设置：

```ini
-D BUZZER_SELF_TEST=1
```

串口监视器会输出 `Buzzer self-test: start` 和 `Buzzer self-test: done`，可用于确认测试代码已经执行。

## 驱动接口

- `begin()`：初始化 PWM，并将蜂鸣器置于静音状态
- `playTone(frequencyHz, volumePercent)`：开始播放指定频率和音量的音调
- `setVolume(volumePercent)`：调整当前音调的音量
- `stop()`：停止播放，并将信号端恢复为非触发电平
- `isPlaying()`：查询当前是否正在播放
