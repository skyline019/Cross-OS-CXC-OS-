/* M118: host-gcc -fPIC ET_REL — GOTPCREL UND + .rodata via GOT. */
extern long add(long a, long b);
extern unsigned long strlen(const char* s);
extern long cwrite(const char* p, long n);

long bump(long x) {
    static const char msg[] = "WMSG:GT\n";
    long m = (long)strlen(msg);
    (void)cwrite(msg, m);
    return add(x, 7);
}
