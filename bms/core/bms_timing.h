#ifndef BMS_TIMING_H_
#define BMS_TIMING_H_

/* 应用采样与软件保护共用节拍；AFE 转换周期不参与应用调度。 */
#define BMS_SAMPLE_PERIOD_MS 200u
#define BMS_SAMPLE_PERIOD_US (BMS_SAMPLE_PERIOD_MS * 1000u)

/* SOC 不补算长时间盲区；物理保护恢复的连续观察不能跨越调度长间断。 */
#define BMS_SAMPLE_MAX_POLL_GAP_MS 400u
#if BMS_SAMPLE_PERIOD_MS != 200u
#error "Normal BMS scheduling requires the reviewed 200 ms period"
#endif

#endif
