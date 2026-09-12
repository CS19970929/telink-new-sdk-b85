#ifndef __SIF_SEND_H__
#define __SIF_SEND_H__

#include "stdint.h"
#include "conf.h"

typedef enum
{
    SIF_IDLE = 0,
    SYNC_SIGNAL,
    SEND_PUBLIC,
    SEND_DATA,
    SEND_DATA_COMPLETE,
    STOP_SIGNAL,
} SIF_STATE_E;

void sif_send_data_handle(void);
void sif_timer_init(void);
void sif_timer_irq_handler(void);

#endif
