/* Strong function, weak function and unresolved data must have distinct loader behavior. */
#ifdef MISSING_DATA
extern int henge_absent_data;
__asm__(".type henge_absent_data, %object");
int read_data(void) { return henge_absent_data; }
#else
extern void henge_absent_function(void);
__asm__(".type henge_absent_function, %function");
extern void henge_absent_weak(void) __attribute__((weak));
int unused_missing(void) { return henge_absent_weak == 0 ? 42 : -1; }
void call_missing(void) { henge_absent_function(); }
void crash_here(void) { __asm__ volatile("brk #0"); }
#endif
