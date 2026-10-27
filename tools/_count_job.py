import re
from pathlib import Path
t = Path("/mnt/e/OS/workspace-study/cxc-os/src/user.cpp").read_text()
m = re.search(r"job_rsp_text\[\] =\s*((?:\"[^\"]*\"\s*)+);", t)
if not m:
    print("no match")
    raise SystemExit(1)
s = "".join(re.findall(r"\"([^\"]*)\"", m.group(1)))
lines = [x for x in s.splitlines() if x.strip()]
print("job entries", len(lines))
print("first5", lines[:5])
print("last5", lines[-5:])
print(".c", sum(1 for x in lines if x.endswith(".c")))
print(".s", sum(1 for x in lines if x.endswith(".s")))
print(".o", sum(1 for x in lines if x.endswith(".o")))
