# Tracker STT → LoRa POC（KIT_PSE84_AI × Heltec Wireless Tracker V2）

目標：**STT → Device1（E84＋Heltec #1）LoRa Tx → Device2（Heltec #2）LoRa Rx → 文字顯示在 UART**。
教學文件：https://claude.ai/artifact/XigD8EUYkqmpceTVtaMAtW

```
                    ┌──────────────── Device1 ────────────────┐
[PC stt_bridge.py] <-USB(KitProg3, 921600)-> [E84 CM55 stt_link] -J14 UART 115200-> [Heltec #1]
  faster-whisper      AUDIO ->  <- TEXT        P16.0 RX / P16.1 TX (SCB10, 3.3 V)       |
                                                                                    LoRa 923.2 MHz SF9
                                                                                         v
                                                           [Heltec #2] -USB 115200-> 終端機顯示文字
                                                             Device2     "[RX] node 1 ... | 集合"
```

| 目錄 | 內容 | 狀態 |
| --- | --- | --- |
| `protocol/` | UART 訊框 `link_frame.[ch]`、LoRa 空中封包 `lora_packet.h`、主機測試 | 主機測試通過 |
| `heltec/lora_text_node/` | Arduino 韌體，兩台燒同一份，只改 `NODE_ID`；收到的文字以 UTF-8 文字行印在 USB 串口 | stub 語法檢查，未上板 |
| `pc/` | `send_text.py`、`stt_bridge.py`（`--typed`／`--pc-mic`／E84 錄音）、`rx_viewer.py` | Python 測試通過，與 C 編碼位元組一致 |
| `e84_cm55/` | `stt_poc_main.c`（取代 proj_cm55/main.c）、`stt_link.[ch]`、`mic_pdm.[ch]`（取自 PR #5）、`mic_config.h` | stub 語法檢查，未用 MTB 編譯 |

## 上板步驟

### 0. 硬體

| E84 AI Kit J14 | 訊號 | Heltec #1 |
| --- | --- | --- |
| pin 6 = P16.0 | E84 RX（SCB10） | GPIO17（TX） |
| pin 5 = P16.1 | E84 TX（SCB10） | GPIO6（RX） |
| pin 20 | GND | GND |

- J14 是 2×10 空焊墊，先焊排針。P16 是 3.3 V 埠，直接接 Heltec，**不需要電平轉換**。
- 兩台 Heltec 先接 LoRa 天線再上電。各板用自己的 USB 供電，不互接電源。

### 1. 只測 LoRa（Device1 的 Heltec ↔ Device2）

1. Arduino IDE：裝 ESP32 core、RadioLib、Adafruit ST7735 and ST7789、Adafruit GFX；板子選 Heltec Wireless Tracker（或 ESP32S3 Dev Module，USB CDC On Boot = Enabled）。
2. `lora_text_node.ino`：Heltec #1 設 `NODE_ID 1` 燒錄；Heltec #2 改 `NODE_ID 2` 燒錄。
3. Device2：任何串口終端機（Tera Term、PuTTY、Arduino Serial Monitor）開 Heltec #2 的埠，115200、UTF-8。開機看到 `[log] lora_text_node ready`。
4. Device1：`pip install pyserial`，`python pc/send_text.py <Heltec #1 埠>`，打「集合」Enter。
5. 通過標準：Device2 終端機出現 `[RX] node 1 seq 0 RSSI -xx dBm SNR x dB | 集合`，`send_text.py` 顯示 `ok (acked)`。

### 2. 建 E84 專案

1. 以這個 repo（分支 `claude/project-thread-v5i0my`）在 ModusToolbox 開啟；Library Manager 加入 BSP `KIT_PSE84_AI` 並設為 Active（`common.mk` 的 TARGET 會改成 KIT_PSE84_AI）。
2. 刪除 `proj_cm55/deps/display-dsi-waveshare-4-3-lcd.mtb`（這個 POC 不用顯示器），執行 `make getlibs`。
3. 把 `tracker_stt_lora_poc/e84_cm55/` 所有檔案複製到 `proj_cm55/`，刪掉原本的 `proj_cm55/main.c`，`stt_poc_main.c` 就是新的 main。`stt_link_mic_hook.c` 不要複製（那是給 PR #5 麥克風任務用的）。
4. Device Configurator（`bsps/TARGET_APP_KIT_PSE84_AI/config/design.modus`）：
   - Pins：P16.0、P16.1 取消 `CYBSP_DVP_CAM_D0`／`D1`。
   - Peripherals：SCB10 啟用 UART，115200 8N1，RX＝P16.0、TX＝P16.1，名稱填 `HELTEC_UART`。
   - Debug UART（`CYBSP_DEBUG_UART`）：baud 改 921600。
   - System：System Idle Power Mode 改 CPU Sleep（不要 Deep Sleep，PDM 與 UART 才不會停）。
   - 存檔讓它產生 `cycfg_*`。
5. Build → Program（J1 KitProg3）。用終端機 921600 開 KitProg3 埠，應看到 `*** Tracker STT -> LoRa POC (CM55) ***`，然後關掉終端機（埠要讓給 stt_bridge.py）。
6. 編譯錯誤或 SCB10 選不到 P16.0/P16.1 時，把訊息貼回 thread。

### 3. 打字經 E84 送出（驗證 E84 轉送）

1. Heltec #1 USB 不用接電腦（接也可以，會看到 `[TX]` 行）。
2. `python pc/stt_bridge.py <KitProg3 埠> --baud 921600 --typed`，打字 Enter。
3. 通過標準：Device2 終端機出現 `[RX] ... | <你打的字>`，stt_bridge 印出 `LoRa seq n: ok (acked)`。

### 4. STT → Device1 → Device2（先用電腦麥克風）

1. `pip install faster-whisper opencc-python-reimplemented numpy sounddevice`（第一次會下載模型，之後離線）。
2. `python pc/stt_bridge.py <KitProg3 埠> --baud 921600 --pc-mic`，Enter 開始講、Enter 結束。
3. 通過標準：Device2 終端機出現辨識出的繁中文字。**到這一步端到端目標就完成了。**

### 5. 改用 E84 板上麥克風

1. `python pc/stt_bridge.py <KitProg3 埠> --baud 921600`。
2. 按住 USER 鍵講話（最長 8 秒），放開。stt_bridge 會印出收到幾秒、峰值 dBFS、存下的 WAV，然後是 STT 結果。
3. 錄音有問題先聽 `recordings/*.wav`：太小聲調 `MIC_PDM_SW_GAIN_DB`，削波就調低。

## 測試

```
cd protocol && cc -std=c99 -Wall -Wextra -I. test_link_frame.c link_frame.c && ./a.out
cd pc && python -I test_linkframe.py
```

## 已知待確認

- Heltec V2 的 SX1262 腳位沿用 V1.1（NSS 8、SCK 9、MOSI 10、MISO 11、RST 12、BUSY 13、DIO1 14），前端 GPIO4/5/7 來自 espboards.dev，要對照 Heltec V2 線路圖。
- J14 腳號與 SCB10 對 P16.0/P16.1 來自 AI Kit User Guide 與第三方 GPIO 表，在 Device Configurator 確認。
- TFT 只顯示 ASCII；中文看 USB 終端機（UTF-8）。
- KitProg3 埠同時跑 printf 與二進位訊框；其他任務的 printf 若剛好插在訊框中間，那一個訊框會因 CRC 錯被丟掉。
