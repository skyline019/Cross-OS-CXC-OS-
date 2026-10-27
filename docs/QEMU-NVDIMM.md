# QEMU 持久内存窗口（xv6-riscv / virt）

## 阶段 A（推荐先做）：扩容 DRAM + 软件划界

xv6 原版：`-m 128M`，`PHYSTOP = KERNBASE + 128MiB`。

课题：

```text
QEMU  -m 160M
内核  PHYSTOP  = KERNBASE + 128MiB   # kalloc 仍只用前 128MiB
      PMEMBASE = PHYSTOP
      PMEMSIZE = 32MiB               # 后 32MiB 当 PMEM
```

`share=on` 的 memory-backend-file 可把整段 guest RAM 落到文件，便于「掉电」后重启读回：

```bash
# 示例：整机 RAM 落盘（开发期）
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel \
  -object memory-backend-file,id=mem,size=160M,mem-path=./guest-ram.img,share=on \
  -numa node,memdev=mem \
  -smp 3 -nographic \
  -global virtio-mmio.force-legacy=false \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
```

（具体参数随本机 QEMU 版本微调；以能启动并看到 `pmem:` 探针为准。）

## 阶段 B：NVDIMM / virtio-pmem（可选）

在 **x86 `pc` + nvdimm** 上更贴近真设备；若毕设评审强调「NVDIMM」字样，可：

- 另开实验附录：同套 `pmem_*` API 接到 ACPI NFIT 物理范围；或
- RISC-V 上尝试 `virtio-pmem`（视 QEMU 是否启用）。

主线仍以「字节寻址 + 刷写原语 + 双槽 SB」论证架构优势，设备形态为附录。

## 验收探针

1. `pmem_init` 打印 `PMEMBASE/SIZE`。
2. 写入魔数 → `pmem_persist` → 正常关机/杀 QEMU → 再启动仍可读。
3. 对比：不调用 persist 的写在部分配置下可能丢失（作反例）。
