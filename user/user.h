#define SBRK_ERROR ((char *)-1)

struct stat;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int pipe(int *);
int write(int, const void *, int);
int read(int, void *, int);
int close(int);
int kill(int);
int exec(const char *, char **);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, struct stat *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getpid(void);
char *sys_sbrk(int, int);
int pause(int);
int uptime(void);
int sync(void);
int fsync(int fd);
int pmfs_put(unsigned long key, const void *val, int len);
int pmfs_get(unsigned long key, void *dst, int cap, unsigned int *len);
int pmfs_seal(void);
int pmfs_stat(void *wa);
unsigned long cycles(void);
int pmfs_create(const char *);
int pmfs_fopen(const char *);
int pmfs_fread(int fd, void *dst, int cap, unsigned int *len);
int pmfs_fwrite(int fd, const void *src, int len);
int pmfs_fsync(int fd);
int pmfs_funlink(const char *);
int pmfs_fclose(int fd);
int pmfs_mkdir(const char *);
int pmfs_readdir(const char *, char *dst, int cap);
int pmfs_format(void);
int pmfs_fault(int stage, int skip);
int pmfs_fstat(const char *path, unsigned int *size, unsigned int *type);

// ulib.c
int stat(const char *, struct stat *);
char *strcpy(char *, const char *);
void *memmove(void *, const void *, int);
char *strchr(const char *, char c);
int strcmp(const char *, const char *);
char *gets(char *, int max);
uint strlen(const char *);
void *memset(void *, int, uint);
int atoi(const char *);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char *sbrk(int);
char *sbrklazy(int);

// printf.c
void fprintf(int, const char *, ...) __attribute__((format(printf, 2, 3)));
void printf(const char *, ...) __attribute__((format(printf, 1, 2)));

// umalloc.c
void *malloc(uint);
void free(void *);
