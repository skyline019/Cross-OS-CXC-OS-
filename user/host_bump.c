/* M116: host-gcc ET_REL — .rodata + UND strlen/cwrite → io.elf. */
extern unsigned long strlen(const char* s);
extern long cwrite(const char* p, long n);

long bump(long x) {
    static const char msg[] = "WMSG:HG\n";
    long m = (long)strlen(msg);
    (void)cwrite(msg, m);
    return x + 7;
}
