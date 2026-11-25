# Tracker STT → LoRa POC（KIT_PSE84_AI × Heltec Wireless Tracker V2）

按住 E84 的 USER 鍵講話 → 文字經 LoRa 送到另一台 Heltec 顯示。
教學文件：https://claude.ai/artifact/XigD8EUYkqmpceTVtaMAtW

```
[E84 PDM mic] --PTT--> [E84 CM55 stt_link] --USB UART (AUDIO)--> [PC stt_bridge.py: faster-whisper]
                              ^  |                                         |
                              |  +<--------------- TEXT -------------------+
                              |  +--UART 115200 (TEXT/CMD)--> [Heltec #1] ~~LoRa 923.2 MHz SF9~~> [Heltec #2] --USB--> [PC rx_viewer.py]
                              +------------ TX_RESULT --------+
```

| 目錄 | 內容 | 狀態 |
| --- | --- | --- |
| `protocol/` | UART 訊框 `link_frame.[ch]`、LoRa 空中封包 `lora_packet.h`、主機測試 | 主機測試通過 |
| `heltec/lora_text_node/` | Arduino 韌體，兩台燒同一份，只改 `NODE_ID` | 只做過 stub 語法檢查，未上板 |
| `pc/` | `send_text.py`（階段 0）、`stt_bridge.py`（路線 C STT）、`rx_viewer.py`（接收端）、`linkframe.py` | Python 測試通過，與 C 編碼位元組一致 |
| `e84_cm55/` | `stt_link.[ch]` PTT 錄音、上傳、文字轉送；`stt_link_mic_hook.c` 接 PR #5 麥克風 | 只做過 stub 語法檢查，未用 MTB 編譯 |

## 測試

```
cd protocol && cc -std=c99 -Wall -Wextra -I. test_link_frame.c link_frame.c && ./a.out
cd pc && python -I test_linkframe.py
```

## 階段 0：只測 LoRa（不接 E84）

1. Arduino IDE 裝 ESP32 core、RadioLib、Adafruit ST7735 and ST7789、Adafruit GFX。
2. 兩台 Heltec 都先接天線。`lora_text_node.ino` 第一台 `NODE_ID 1`、第二台 `NODE_ID 2`，各自燒錄。
3. `pip install pyserial`；PC 1 跑 `python send_text.py <埠>`，PC 2（或同一台）跑 `python rx_viewer.py <埠>`。
4. 打字送出，接收端顯示文字、RSSI、SNR；發送端顯示 `ok (acked)`。

## 階段 1：E84 → 電腦 STT → LoRa

1. 在 KIT_PSE84_AI 的專案（例如 PR #5 麥克風分支，或 PDM 範例）加入 `e84_cm55/` 檔案到 `proj_cm55/`。
2. Device Configurator：debug UART 改 921600；新增 SCB UART 115200 命名 `HELTEC_UART`（J7 P17.0/P17.1 若可當 UART，否則 J5＋TXB0104）。
3. `main.c` 在 `init_retarget_io()` 之後呼叫 `stt_link_start(configMAX_PRIORITIES - 3)`；麥克風來源用 `stt_link_mic_hook.c` 或自己呼叫 `stt_link_push_pcm()`。
4. 接線：E84 TX → Heltec GPIO6，Heltec GPIO17 → E84 RX，GND 對 GND。
5. `pip install faster-whisper opencc-python-reimplemented numpy`，`python stt_bridge.py <KitProg3 埠> --baud 921600`；按住 USER 鍵說話、放開。
6. 沒有麥克風程式時，用 `stt_bridge.py --typed` 打字，先驗證 E84 轉送這一段。

## 已知待確認

- Heltec V2 的 SX1262 腳位沿用 V1.1（NSS 8、SCK 9、MOSI 10、MISO 11、RST 12、BUSY 13、DIO1 14），前端 GPIO4/5/7 來自 espboards.dev，要對照 Heltec V2 線路圖。
- TFT 只顯示 ASCII；中文完整內容看 `rx_viewer.py`。要在 TFT 顯示中文需自製字型子集。
- PC UART 同時跑 printf 與二進位訊框，其他任務的 printf 可能打斷訊框（該訊框 CRC 錯會被丟掉）。
