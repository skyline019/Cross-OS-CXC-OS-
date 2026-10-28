#!/usr/bin/env python3

#
# python script that tests xv6 without having to boot it and type to its shell
#
# ./test-xv6.py usertests  (runs usertests)
# ./test-xv6.py -q usertests (runs the quick tests of usertests)
# make test-xv6-crash  (rebuilds a test-only kernel; use a disposable checkout)
# ./test-xv6.py log (requires XV6_LOG_FAULT_INJECT in the built kernel)
# ./test-xv6.py stress (concurrent block-log writers and readback)
# ./test-xv6.py vfs (ordinary VFS namespace/descriptor tests)

import argparse, os, inspect, re, signal, subprocess, sys, time
from subprocess import run

sys.stdout.reconfigure(line_buffering=True)

parser = argparse.ArgumentParser()
parser.add_argument('testrex', help="test name or regular expression")
parser.add_argument("-q", action='store_true', help="usertests quick")
args = parser.parse_args()

class QEMU(object):

    def __init__(self, reset=False):
        if reset:
            self.build_xv6()
            self.reset_fs()
        q = ["make", "qemu"]
        self.proc = subprocess.Popen(q, stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT,
                                      start_new_session=True)
        os.set_blocking(self.proc.stdout.fileno(), False)
        self.output = ""
        self.outbytes = bytearray()
        self.reported = 0

    def reset_fs(self):
        run(["rm", "-f", "fs.img"], check=True)
        run(["make", "fs.img"], check=True)

    def build_xv6(self):
        run(["make", "kernel/kernel"], check=True)

    def save_output(self):
      try:
        with open("test-xv6.out", "w") as f:
            f.write(self.output)
            f.close()
      except OSError as e:
        print("Provided a bad results path. Error:", e)     
        
    def wait_for_prompt(self, timeout=45):
        # Do not send a command before sh actually prints its prompt. Sending
        # too early can interleave the prompt and a guest's result on one line.
        deadline = time.monotonic() + timeout
        while True:
            self.read()
            if re.search(r'(?m)^\$ $', self.output):
                return
            if self.proc.poll() is not None or time.monotonic() >= deadline:
                self.error('shell prompt')
            time.sleep(0.05)

    def cmd(self, c):
        if isinstance(c, str):
            c = c.encode('utf-8')
        self.proc.stdin.write(c)
        self.proc.stdin.flush()
        
    def _stop_group(self, sig):
        try:
            os.killpg(self.proc.pid, sig)
        except ProcessLookupError:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.proc.wait(timeout=5)
        self.read()
        self.proc.stdin.close()
        self.proc.stdout.close()

    def crash(self):
        print("kill qemu process group", self.proc.pid)
        self._stop_group(signal.SIGKILL)

    def stop(self):
        self._stop_group(signal.SIGTERM)

    def read(self):
        while True:
            try:
                buf = os.read(self.proc.stdout.fileno(), 4096)
            except BlockingIOError:
                break
            if len(buf) == 0:  # qemu exited
                break
            self.outbytes.extend(buf)
        self.output = self.outbytes.decode("utf-8", "replace")

    def lines(self):
        # Serial output can arrive one character at a time. Do not match a
        # trailing incomplete line (for example inode 3 before inode 30).
        end = self.output.rfind("\n") + 1
        return self.output[:end].splitlines()

    def error(self, *regexps):
        print("FAIL: match failed", regexps)
        if os.environ.get("GITHUB_ACTIONS") == "true":
            detail = self.output[-3000:].replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
            print("::error title=xv6 test serial tail::" + detail)
        self.save_output()
        self.stop()
        sys.exit(1)

    def match(self, *regexps, exit=True):
        found = False
        for line in self.lines():
            if any(re.match(r, line) for r in regexps):
                print(line)
                found = True
        if not found and exit:
            self.error(*regexps)
        return found

    # Print the lines matching regexp that have arrived since the last
    # call.  A trailing partial line is left for the next call, so that
    # each line is printed once, after all of it has been read.
    def progress(self, regexp):
        end = self.output.rfind("\n") + 1
        if end <= self.reported:
            return
        for line in self.output[self.reported:end].splitlines():
            if re.match(regexp, line):
                print(line)
        self.reported = end

    def monitor(self, *regexps, progress="", failure="", timeout):
        deadline = time.monotonic() + timeout
        while True:
            time.sleep(1)
            self.read()
            if progress:
                self.progress(progress)
            if failure and self.match(failure, exit=False):
                self.error(*regexps)
            if self.match(*regexps, exit=False):
                return
            if self.proc.poll() is not None or time.monotonic() >= deadline:
                self.error(*regexps)

def crash_orphan(program):
    q = QEMU(True)
    q.wait_for_prompt()
    q.cmd(program + "\n")
    q.monitor(r'^wait for kill and reclaim \d+$', timeout=30)
    inum = int(re.search(r'^wait for kill and reclaim (\d+)\r?$', q.output, re.MULTILINE).group(1))
    q.crash()
    return inum

def recover_orphan(inum):
    pattern = r'^ireclaim: orphaned inode %d$' % inum
    q = QEMU()
    q.monitor(pattern, timeout=30)
    # The diagnostic is printed before reclamation finishes. Wait for fsinit
    # to complete, then check a second boot does not reclaim the inode again.
    q.monitor('^init: starting sh', timeout=30)
    q.stop()
    q = QEMU()
    q.monitor('^init: starting sh', timeout=30)
    if q.match(pattern, exit=False):
        q.error('orphan must not reappear on the second reboot')
    q.stop()

def test_log():
    print("Test deterministic recovery of the native block log")
    result = run([sys.executable, "tools/xv6_log_recovery.py"])
    if result.returncode:
        sys.exit(result.returncode)
    print("OK")

def test_vfs():
    print("Test VFS path, directory, descriptor and rename semantics")
    q = QEMU(True)
    q.wait_for_prompt()
    q.cmd("vfstest basic\n")
    q.monitor(r'^VFS_TEST_PASS mode=basic$', failure=r'^VFS_TEST_FAIL', timeout=120)
    q.stop()
    print("OK")

def test_vfscapacity():
    print("Test rollback when PMFS metadata space is exhausted")
    q = QEMU(True)
    q.wait_for_prompt()
    q.cmd("vfstest capacity\n")
    q.monitor(r'^VFS_TEST_PASS mode=capacity$', progress=r'^VFS_CAPACITY_PASS',
              failure=r'^VFS_TEST_FAIL', timeout=120)
    q.stop()
    print("OK")

def test_efficiency():
    print("Test PMFS software write avoidance and exact/fallback queries")
    q = QEMU(True)
    q.wait_for_prompt()
    q.cmd("pmfs_eff\n")
    q.monitor(r'^PMFS_EFFICIENCY_PASS', progress=r'^PMFS_METRIC',
              failure=r'^PMFS_EFFICIENCY_FAIL', timeout=120)
    q.stop()
    print("OK")

def test_efficiency_metrics():
    print("Test PMFS software metrics utility")
    q = QEMU(True)
    q.wait_for_prompt()
    q.cmd("pmstats\n")
    q.monitor(r'^PMFS_SOFTWARE_METRICS version=1', failure=r'^pmstats:', timeout=30)
    q.stop()
    print("OK")

def test_stress():
    print("Test concurrent block-log writes and readback")
    q = QEMU(True)
    q.wait_for_prompt()
    q.cmd("logstress f0 f1 f2 f3 f4 f5\n")
    q.monitor('^LOGSTRESS_PASS files=6 bytes=240000$',
              failure='^LOGSTRESS_FAIL', timeout=90)
    q.stop()
    print("OK")

def test_forphan():
    print("Test recovery of an orphaned file")
    recover_orphan(crash_orphan("forphan"))
    print("OK")

def test_dorphan():
    print("Test recovery of an orphaned directory")
    recover_orphan(crash_orphan("dorphan"))
    print("OK")

def test_crash():
    test_log()
    test_forphan()
    test_dorphan()

def test_usertests(test=""):
    timeout = 600
    opt = ""
    if args.q:
        opt = " -q"
        timeout = 300
    elif test != "":
        opt += " " + test
    q = QEMU(True)
    q.wait_for_prompt()
    q.cmd("usertests" + opt + "\n")
    q.monitor('^ALL TESTS PASSED', progress='test', timeout=timeout)
    q.stop()

def main():
    print(args)
    rex = r'%s' % args.testrex
    funcs = [(obj,name) for name,obj in inspect.getmembers(sys.modules[__name__]) 
                     if (inspect.isfunction(obj) and 
                         name.startswith('test'))]
    none = True
    for (f,n) in funcs:
        if re.search(rex, n):
            none = False
            f()
    if none:
        test_usertests(test=args.testrex)

main()
