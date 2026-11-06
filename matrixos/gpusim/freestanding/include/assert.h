/* Freestanding <assert.h>: a failed assertion is a validator/executor gap. */
#ifndef MX_FS_ASSERT_H
#define MX_FS_ASSERT_H
_Noreturn void mx_abort(void);
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : mx_abort())
#endif
#endif
