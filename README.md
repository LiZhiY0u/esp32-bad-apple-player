# ESP32 Bad Apple 音画播放器

这是一个基于 PlatformIO 的 ESP32 项目：在 128×64 SSD1306 OLED 上播放《Bad Apple!!》黑白视频，并通过低电平触发的无源蜂鸣器模块同步播放实时合成的旋律。

项目默认使用经过音准和切换优化的 LEDC 方波驱动。视频帧负责推进旋律时间线，避免音频和画面在长时间播放后逐渐漂移。仓库中也保留了实验性的四声部 I2S-PDM 合成器，便于后续继续研究音质优化。

> 本项目播放的是根据音符数据实时合成的蜂鸣器旋律，不包含《Bad Apple!!》原版录音。

## 功能特点

- SSD1306 OLED 播放 128×64 黑白压缩视频
- 低电平触发无源蜂鸣器输出合成旋律
- 视频第 46 帧启动音频，并以视频帧持续推进音频时间线
- LEDC 10.8 位硬件分频，B2～G5 范围理论最大音高误差约 0.034 音分
- 约 4 ms 硬件淡入、淡出，减轻音符切换时的爆音
- 支持纯音频、蜂鸣器自检、音画独立运行和音画同步模式
- 保留实验性的四声部 I2S-PDM 合成器

## 硬件

- ESP32 开发板
- 128×64 SSD1306 I2C OLED，默认地址 `0x3C`
- 低电平触发无源蜂鸣器模块
- USB 数据线和若干杜邦线

### 接线

| 模块 | 模块引脚 | ESP32 引脚 | 说明 |
| --- | --- | --- | --- |
| OLED | `VCC` | `3V3` | 请同时确认所用模块的额定电压 |
| OLED | `GND` | `GND` | 共地 |
| OLED | `SDA` | `GPIO21` | 默认 I2C SDA |
| OLED | `SCL` | `GPIO22` | 默认 I2C SCL |
| OLED | `RST` | `GPIO16` | 仅带独立复位脚的模块需要连接 |
| 蜂鸣器 | `IN` | `GPIO25` | 默认低电平有效 |
| 蜂鸣器 | `GND` | `GND` | 必须与 ESP32 共地 |
| 蜂鸣器 | `VCC` | 模块额定电源 | 按模块丝印或说明书供电 |

低电平触发只是模块的有效电平，持续接 `GND` 或 `3V3` 不会产生音调是正常现象。无源蜂鸣器需要连续变化的方波才能发声。如果使用没有驱动电路的裸无源蜂鸣器，并且所需电流超过 GPIO 允许范围，应增加三极管或 MOSFET，不能直接由 GPIO 大电流驱动。

## 快速开始

先安装 [Visual Studio Code](https://code.visualstudio.com/) 和 [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)，然后克隆并进入项目：

```shell
git clone https://github.com/LiZhiY0u/esp32-bad-apple-player.git
cd esp32-bad-apple-player
```

编译并烧录固件：

```shell
pio run
pio run -t upload
```

视频文件位于 `data/video.hs`，还需要单独上传 SPIFFS 文件系统：

```shell
pio run -t uploadfs
```

查看串口日志：

```shell
pio device monitor -b 115200
```

如果自动识别串口失败，可以在命令后添加 `--upload-port COM4`，并把 `COM4` 换成实际端口。正常完成一次播放后，串口会输出 `Done.`。

## 配置

主要选项位于 `platformio.ini`：

```ini
-D BUZZER_PIN=25
-D BUZZER_SELF_TEST=0
-D BUZZER_VOLUME=50
-D AUDIO_ONLY_MODE=0
-D AUDIO_OUTPUT_PDM=0
-D AUDIO_VIDEO_SYNC=1
```

| 选项 | 默认值 | 作用 |
| --- | ---: | --- |
| `BUZZER_PIN` | `25` | 蜂鸣器信号输出引脚 |
| `BUZZER_SELF_TEST` | `0` | 设为 `1` 时，启动后先播放 C5、E5、G5 测试音 |
| `BUZZER_VOLUME` | `50` | 音量参数，范围 0～100 |
| `AUDIO_ONLY_MODE` | `0` | 设为 `1` 时不初始化屏幕和 SPIFFS，只播放旋律 |
| `AUDIO_OUTPUT_PDM` | `0` | `0` 为默认 LEDC；`1` 为实验性 I2S-PDM |
| `AUDIO_VIDEO_SYNC` | `1` | `1` 为音画同步；`0` 为音频和视频独立运行 |

### 单独测试蜂鸣器

不接屏幕时，可以先修改为：

```ini
-D BUZZER_SELF_TEST=1
-D AUDIO_ONLY_MODE=1
-D AUDIO_OUTPUT_PDM=0
```

启动后会先播放三个测试音，随后进入纯音频旋律播放。测试完成后建议把 `BUZZER_SELF_TEST` 改回 `0`。

## 音频实现

### 默认 LEDC 方案

`PassiveBuzzer` 使用低电平脉冲驱动模块，并提供非阻塞的连续音调输出。旋律最多包含四个同时活动的声部；受单个蜂鸣器能力限制，默认方案选择当前最高音播放并忽略打击乐。

驱动按照十二平均律计算 MIDI 音符频率，并直接配置 ESP32 LEDC 的整数与小数分频器。独立播放模式使用 FreeRTOS 任务和微秒绝对时间推进 138 BPM 节拍；同步模式则由 30 FPS 视频帧推进旋律，从而避免两个独立时钟长期运行产生漂移。

`BUZZER_VOLUME` 调整的是低电平脉冲宽度，并不等同于扬声器的线性功率控制。不同蜂鸣器模块的实际响度变化可能不同。

### 实验性 PDM 方案

设置 `AUDIO_OUTPUT_PDM=1` 后，程序会通过 ESP32 I2S0 将 24 kHz、16 位 PCM 转换为 6.144 MHz 单线 PDM，并支持四声部 DDS、正弦插值、包络和噪声打击乐。

该模式不适合直接驱动多数廉价低电平触发蜂鸣器模块：模块上的三极管通常是为普通音频 PWM 设计的，不一定能可靠处理 MHz 级开关信号。建议仅配合合适的低通滤波、功放和扬声器实验；若模块明显发热，应立即断电并恢复 `AUDIO_OUTPUT_PDM=0`。

## 项目结构

```text
.
├─ data/                     # SPIFFS 视频数据
│  └─ video.hs
├─ src/
│  ├─ audio/                 # LEDC 播放任务与实验性 PDM 合成器
│  ├─ buzzer/                # 低电平无源蜂鸣器驱动
│  ├─ music/                 # 旋律解码和压缩音符数据
│  ├─ oled/                  # SSD1306 OLED 驱动
│  └─ main.cpp               # 视频解码、显示和音画同步入口
├─ platformio.ini            # PlatformIO 构建配置
└─ THIRD_PARTY_NOTICES.md    # 第三方软件与数据声明
```

## 致谢与许可

视频播放部分基于 [hackffm/ESP32_BadApple](https://github.com/hackffm/ESP32_BadApple)，旋律数据生成参考 [cnlohr/badderapple](https://github.com/cnlohr/badderapple)。OLED 驱动和 Heatshrink 解码器也分别沿用其上游许可。

项目代码按 [MIT License](LICENSE) 发布。第三方版权和许可信息见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
