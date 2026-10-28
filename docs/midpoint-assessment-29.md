# 第二十九次中场评估（M285 / 迷你 IDE）

**版本**：v2.81.1  
本地实证：默认 `qemu-test` 仍以串口 `CXC_OS_SELF_TEST_PASS` + `ONBOX_CCDRV_PASS` + `peer_ok=1` 为门禁。  
若固件提供线性 GOP：自测末可选跑迷你 IDE 合同 → `ONBOX_GUIFB_PASS` + `ONBOX_GUIKBD_PASS` + `ONBOX_IDE_PASS`（无 GOP 时跳过，不失败）。

## 本窗交付

1. **GUI_FB**：EBS 前 `LocateProtocol(GOP)`；VMM WC 映射；8×16 字模。
2. **GUI_KBD**：PS/2 i8042 + IRQ1；合成键泵用于无头合同。
3. **IDE**：全屏编辑；**F2** 只写 `disk/ide_main.c`；**F5** 真跑 `disk/cc -o disk/ide_out.elf disk/ide_main.c`（轻量单 TU，不碰 HARD `lib.c`/`job.rsp`）；**F9** 重跑 `ide_out.elf`；光标高亮与卷动。
4. **工具**：`qemu_runner.py --gui` 打开显示；默认仍 `-display none`。
5. **交互**：ESP `IDEHOLD` 存在时，合同通过后进入编辑循环。

## 纪律

- **未宣称 A2**；未宣称桌面 OS / 鼠标 / 多窗口。
- 默认 CI 不要求 GUI PASS；禁止因无显示设备导致串口回归失败。

## 验收清单

1. 无 GOP：串口 CCDRV + SELF_TEST 不回退。
2. 有 GOP：`ONBOX_IDE_PASS`（真 `disk/cc` 轻量编译 `fn main(){return 42;}`）。
3. VERSION **2.81.1**
