/* M117: host-gcc ET_REL — UND add (math.o) + strlen/cwrite (io.elf). */
extern long add(long a, long b);
extern unsigned long strlen(const char* s);
extern long cwrite(const char* p, long n);

long bump(long x) {
    static const char msg[] = "WMSG:H2\n";
    long m = (long)strlen(msg);
    (void)cwrite(msg, m);
    return add(x, 7);
}
