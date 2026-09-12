#ifndef SIF_SEND_H_
#define SIF_SEND_H_

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

#endif /* SIF_SEND_H_ */
