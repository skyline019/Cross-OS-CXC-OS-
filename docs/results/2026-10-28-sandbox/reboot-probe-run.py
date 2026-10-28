#!/usr/bin/env python3
"""Run two-boot PMFS readback observations against an already-built scratch copy.

The scratch fs.img must contain user/_pmreboot; the kernel must use PMEM_MAP.
The test uses a new temporary RAM file for each trial and SIGKILLs QEMU only
once the guest command has completed and the shell is idle. It deliberately
returns 1 when the file or KV cannot be read after the second boot.
"""
import argparse
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import tempfile
import time


def run_boot(root, qemu, ram, log, boot_marker, mode, timeout):
    cmd = [qemu, '-machine', 'virt', '-bios', 'none', '-kernel', 'kernel/kernel',
           '-m', '160M', '-smp', '1', '-nographic',
           '-object', f'memory-backend-file,id=mem,size=160M,mem-path={ram},share=on',
           '-numa', 'node,memdev=mem', '-global', 'virtio-mmio.force-legacy=false',
           '-drive', 'file=fs.img,if=none,format=raw,id=x0',
           '-device', 'virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0']
    proc = subprocess.Popen(cmd, cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, start_new_session=True)
    sel = selectors.DefaultSelector()
    sel.register(proc.stdout, selectors.EVENT_READ)
    out = bytearray()

    def wait_until(predicate, description):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            if proc.poll() is not None:
                raise RuntimeError(f'QEMU exited {proc.returncode} while waiting for {description}')
            for key, _ in sel.select(min(0.2, max(0, deadline - time.monotonic()))):
                chunk = os.read(key.fd, 4096)
                if chunk:
                    out.extend(chunk)
        raise RuntimeError(f'timed out waiting for {description}')

    try:
        wait_until(lambda: b'\n$ ' in out, 'initial shell prompt')
        if b'ONBOX_PMFS_PMEM0_PASS mode=MAP' not in out or boot_marker.encode() not in out:
            raise RuntimeError('boot lacked MAP or requested boot marker')
        offset = len(out)
        proc.stdin.write(f'pmreboot {mode}\n'.encode())
        proc.stdin.flush()
        choices = ([b'PROBE_WRITE_OK', b'PROBE_WRITE_FAIL', b'PROBE_BAD_ARGS']
                   if mode == 'write' else
                   [b'PROBE_READ_OK', b'PROBE_READ_FAIL', b'PROBE_BAD_ARGS'])

        def completed():
            for mark in choices:
                p = out.find(mark, offset)
                if p != -1 and out.find(b'\n$ ', p) != -1:
                    return True
            return False

        wait_until(completed, f'{mode} result and next shell prompt')
        if mode == 'write' and b'PROBE_WRITE_OK' not in out[offset:]:
            raise RuntimeError('same-boot write/read control failed')
        if mode == 'read' and b'ONBOX_PMFS_PMEM2_PASS' not in out:
            raise RuntimeError('shared RAM file magic did not survive restart')
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)
        sel.close()
        proc.stdin.close()
        proc.stdout.close()
        log.write_bytes(out)
    return out.decode('utf-8', 'replace')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='isolated, prebuilt MAP xv6 tree')
    parser.add_argument('--qemu', default='qemu-system-riscv64')
    parser.add_argument('--log-dir', type=Path, required=True)
    parser.add_argument('--label', default='normal')
    parser.add_argument('--trials', type=int, default=2)
    parser.add_argument('--timeout', type=int, default=60)
    parser.add_argument('--boot-marker', default='ONBOX_PMFS_PASS')
    args = parser.parse_args()
    if args.trials < 1 or args.timeout < 1 or not args.label.replace('-', '').isalnum():
        parser.error('trials/timeout must be positive and label must be alphanumeric or hyphen')
    args.root = args.root.resolve()
    args.log_dir.mkdir(parents=True, exist_ok=True)
    failures = 0
    for trial in range(1, args.trials + 1):
        with tempfile.TemporaryDirectory(prefix='pmfs-reboot-probe-') as d:
            ram = Path(d) / 'guest-ram.img'
            with ram.open('wb') as f:
                f.truncate(160 * 1024 * 1024)
            print(f'=== fresh backing file trial {trial} ===', flush=True)
            outputs = []
            for bootnum, mode in enumerate(('write', 'read'), 1):
                log = args.log_dir / f'{args.label}-trial{trial}-boot{bootnum}.log'
                output = run_boot(args.root, args.qemu, ram, log, args.boot_marker, mode, args.timeout)
                outputs.append(output)
                for line in output.splitlines():
                    if ('PROBE_WRITE_' in line or 'PROBE_READ_' in line or
                            'ONBOX_PMFS_PMEM2_PASS' in line):
                        print(line, flush=True)
            if 'PROBE_READ_OK' not in outputs[1]:
                failures += 1
    print(f'REBOOT_READBACK_TRIALS={args.trials} FAILED={failures}', flush=True)
    return 1 if failures else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr, flush=True)
        sys.exit(2)
