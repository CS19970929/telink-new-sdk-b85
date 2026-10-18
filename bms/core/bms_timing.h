#ifndef BMS_TIMING_H_
#define BMS_TIMING_H_

/* 应用采样与软件保护共用节拍；AFE 转换周期不参与应用调度。 */
#define BMS_SAMPLE_PERIOD_MS 200u
#define BMS_SAMPLE_PERIOD_US (BMS_SAMPLE_PERIOD_MS * 1000u)

/* 应用长时间未观察时撤销连续资格；不得随采样周期自动放宽。 */
#define BMS_SAMPLE_MAX_POLL_GAP_MS 400u
/* 业务数据有效期与持续停更关断期限分开；正常模式下两类 ADC 分别检查。 */
#define BMS_AFE_ADC_STOP_CONFIRM_MS 2000u
#if BMS_SAMPLE_PERIOD_MS != 200u
#error "Normal BMS scheduling requires the reviewed 200 ms period"
#endif

#endif
