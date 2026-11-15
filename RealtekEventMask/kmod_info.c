#include <mach/mach_types.h>
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
extern kern_return_t RealtekEventMask_kern_start(kmod_info_t *, void *);
extern kern_return_t RealtekEventMask_kern_stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(local.RealtekEventMask, "0.2.3", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = RealtekEventMask_kern_start;
__private_extern__ kmod_stop_func_t *_antimain = RealtekEventMask_kern_stop;
