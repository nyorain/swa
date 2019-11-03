#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct swa_display;
typedef struct swa_display* (*swa_backend)(void);

void swa_backend_register(swa_backend);
bool swa_backend_unregister(swa_backend);
swa_backend* swa_backends(void);

#ifdef __cplusplus
}
#endif
