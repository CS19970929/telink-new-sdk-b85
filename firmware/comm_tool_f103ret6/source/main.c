#include "board.h"
#include "board_uart.h"
#include "board_owc.h"
#include "ct_app.h"
#include "ct_modbus_bridge.h"
#include "ct_protocol.h"
#include "ct_self_iap.h"
#include "ct_watchdog.h"

static CtProtocolParser s_parser;
static CtFrame s_frame;

int main(void)
{
    uint8_t byte;

    Board_Init();
    CtProtocol_Init(&s_parser);
    CtModbusBridge_Init();
    CtApp_Init();

    while (1)
    {
        while (BoardUart_ReadByte(&byte))
        {
            BoardOwc_Poll();
            CtModbusBridge_FeedPcByte(byte);
            if (CtProtocol_Feed(&s_parser, byte, &s_frame) != 0u)
            {
                CtApp_HandleFrame(&s_frame);
            }
        }
        CtModbusBridge_Task();
        CtApp_Poll();
        Board_Poll();
        CtWatchdog_Feed();
    }
}
