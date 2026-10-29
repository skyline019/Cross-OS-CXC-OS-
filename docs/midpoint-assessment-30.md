# 第三十次中场评估（M286 / GUIUX）

**版本**：v2.82.0  
本地实证：默认 `qemu-test` 仍以串口 `CXC_OS_SELF_TEST_PASS` + `ONBOX_CCDRV_PASS` + `peer_ok=1` 为门禁。  
有 GOP 时自测末另跑 GUIUX 合同 → `ONBOX_GUIUX_PASS`（无 GOP 时跳过，不失败）。

## 本窗交付

1. **鼠标**：PS/2 AUX（IRQ12）+ 合成包；窗口焦点点击。
2. **多窗口**：简易 WM（IDE + Terminal 两窗布局/绘制/焦点）。
3. **字体缩放**：`fb::set_scale(1|2)`；合同验证 glyph 尺寸。
4. **VT100**：CSI 子集（H/J/K/m/A–D）+ 屏缓冲渲染。
5. **TTY**：`/dev/tty0` 读写环 + `ioctl(TCGETS)`。
6. **fbdev**：`/dev/fb0` + `FBIOGET_VSCREENINFO` + `mmap` 映射线性 FB。

## 纪律

- **未宣称 A2**；未宣称完整桌面/X11。
- 默认 CI 不要求 `ONBOX_GUIUX_PASS`；禁止因无显示设备导致串口回归失败。

## 验收清单

1. 无 GOP：串口 CCDRV + SELF_TEST 不回退。
2. 有 GOP：`ONBOX_GUIUX_PASS`（mouse/windows/scale/vt100/tty/fb0）。
3. VERSION **2.82.0**
