#include "tl_common.h"
#include "sif_send.h"

/*
 * D011 has only Modbus RTU over RS485. One-wire/SIF is not a product
 * interface, so keep the historical entry points as inert stubs only.
 */
void sif_send_data_handle(void)
{
}

void sif_timer_init(void)
{
}

_attribute_ram_code_ void sif_timer_irq_handler(void)
{
}
