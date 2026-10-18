#include <dlfcn.h>
#include "henge/henge.h"
#include "henge/apk.h"

/* Platform shims (Platform/*.m): EGL on EAGL, AAudio on AudioUnit. Register before loading a library. */
void henge_register_egl_shims(void);
void henge_register_audio_shims(void);
