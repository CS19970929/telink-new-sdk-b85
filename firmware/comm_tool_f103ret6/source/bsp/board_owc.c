#include "board_owc.h"
#include "board.h"
#include "ct_config.h"
#include "ct_owc.h"

#define OWC_EDGE_CAPACITY 512u
#define OWC_LEASE_MS 10000u
typedef struct { uint32_t tick_ms; uint16_t timer_us; uint8_t level; } OwcEdge;
static OwcEdge s_edges[OWC_EDGE_CAPACITY];
static volatile uint16_t s_head, s_tail;
static volatile uint8_t s_lost;
static uint8_t s_enabled;
static uint32_t s_lease_ms, s_last_ms;
static uint16_t s_last_us;

void BoardOwc_Init(void)
{
#if (CT_COMM_UART_PORT != CT_COMM_UART_PORT_USART3)
    GPIO_InitTypeDef gpio;
    RCC_ClocksTypeDef clocks;
    uint32_t timer_hz;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    /* Q4/Q3 高电平使能 OW 的 R61 上拉；默认关闭，接收期间不发送位波形。 */
    GPIO_ResetBits(GPIOC, GPIO_Pin_10);
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOC, GPIO_PinSource11);
    EXTI->IMR &= ~EXTI_Line11;
    EXTI->RTSR |= EXTI_Line11;
    EXTI->FTSR |= EXTI_Line11;
    EXTI->PR = EXTI_Line11;
    RCC_GetClocksFreq(&clocks);
    timer_hz = clocks.PCLK1_Frequency;
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency) timer_hz *= 2u;
    TIM2->CR1 = 0u;
    TIM2->PSC = (uint16_t)(timer_hz / 1000000u - 1u);
    TIM2->ARR = 0xffffu;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 = TIM_CR1_CEN;
    /* 高于 UART/CAN，ISR 只采样和入队，避免 0.5ms 脉冲被普通通信打断。 */
    NVIC_SetPriority(EXTI15_10_IRQn, NVIC_EncodePriority(2u, 0u, 0u));
    NVIC_EnableIRQ(EXTI15_10_IRQn);
#endif
    CtOwc_Init();
}

int BoardOwc_Enable(uint8_t enable)
{
#if (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART3)
    (void)enable;
    return 0; /* 同一 PC10/11 不能同时用作 PC UART 和 OW。 */
#else
    NVIC_DisableIRQ(EXTI15_10_IRQn);
    EXTI->IMR &= ~EXTI_Line11;
    s_enabled = enable;
    s_tail = s_head = 0u;
    s_lost = 0u;
    CtOwc_Init();
    s_last_us = (uint16_t)TIM2->CNT;
    s_last_ms = s_lease_ms = Board_GetTickMs();
    if (enable) GPIO_SetBits(GPIOC, GPIO_Pin_10);
    else GPIO_ResetBits(GPIOC, GPIO_Pin_10);
    EXTI->PR = EXTI_Line11;
    NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    if (enable) {
        EXTI->IMR |= EXTI_Line11;
        NVIC_EnableIRQ(EXTI15_10_IRQn);
    }
    return 1;
#endif
}

uint8_t BoardOwc_IsEnabled(void) { return s_enabled; }
void BoardOwc_KeepAlive(void) { s_lease_ms = Board_GetTickMs(); }

void EXTI15_10_IRQHandler(void)
{
    uint16_t next;
    uint16_t timer_us;
    uint8_t level;
    if ((EXTI->PR & EXTI_Line11) == 0u) return;
    timer_us = (uint16_t)TIM2->CNT;
    level = (GPIOC->IDR & GPIO_Pin_11) ? 1u : 0u;
    EXTI->PR = EXTI_Line11;
    next = (uint16_t)((s_head + 1u) % OWC_EDGE_CAPACITY);
    if (next == s_tail) {
        s_lost = 1u;
        return;
    }
    s_edges[s_head].timer_us = timer_us;
    s_edges[s_head].tick_ms = Board_GetTickMs();
    s_edges[s_head].level = level;
    __DMB();
    s_head = next;
}

void BoardOwc_Poll(void)
{
    OwcEdge edge;
    uint32_t duration_us;
    uint16_t budget = OWC_EDGE_CAPACITY;
    if (!s_enabled) return;
    if ((uint32_t)(Board_GetTickMs() - s_lease_ms) >= OWC_LEASE_MS) {
        (void)BoardOwc_Enable(0u);
        return;
    }
    if (s_lost) {
        NVIC_DisableIRQ(EXTI15_10_IRQn);
        s_tail = s_head;
        s_lost = 0u;
        NVIC_EnableIRQ(EXTI15_10_IRQn);
        CtOwc_RecordOverflow();
    }
    while ((s_tail != s_head) && budget--) {
        if (s_lost) return; /* 先丢弃不连续队列，不能继续拼接当前帧。 */
        __DMB();
        edge = s_edges[s_tail];
        __DMB();
        s_tail = (uint16_t)((s_tail + 1u) % OWC_EDGE_CAPACITY);
        /* 长空闲由毫秒 tick 定界，防止 16 bit 定时器回绕伪装成有效脉宽。 */
        duration_us = ((uint32_t)(edge.tick_ms - s_last_ms) >= 30u) ?
                      30000u : (uint16_t)(edge.timer_us - s_last_us);
        s_last_us = edge.timer_us;
        s_last_ms = edge.tick_ms;
        CtOwc_FeedEdge(duration_us, edge.level, edge.tick_ms);
    }
    if (s_tail == s_head) CtOwc_Poll(Board_GetTickMs());
}
