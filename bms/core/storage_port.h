#ifndef STORAGE_PORT_H_
#define STORAGE_PORT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct storage_port storage_port_t;

typedef int (*storage_port_begin_fn)(void *ctx);
typedef void (*storage_port_end_fn)(void *ctx);
typedef int (*storage_port_read_fn)(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len);
typedef int (*storage_port_program_fn)(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len);
typedef int (*storage_port_erase_fn)(void *ctx, uint32_t addr, uint32_t len);

struct storage_port {
    void *ctx;
    uint32_t erase_size;
    uint32_t program_size;
    uint8_t erased_value;
    storage_port_begin_fn begin;
    storage_port_end_fn end;
    storage_port_read_fn read;
    storage_port_program_fn program;
    storage_port_erase_fn erase;
};

typedef struct {
    uint32_t base;
    uint32_t size;
} storage_region_t;

#ifdef __cplusplus
}
#endif

#endif /* STORAGE_PORT_H_ */
