"""D008 真实调度顺序及 200 ms 唤醒边界，SDK 和外设是可观测替身。"""
from validation_support import read, function, run_c

app = read("bms/app/app.c")
power = read("bms/app/app_power.c")
parts = "\n".join(function(app, sig) for sig in (
    "static void app_schedule_sample_wakeup(", "uint8_t app_sample_is_due(",
    "static uint8_t app_sample_task(", "_attribute_no_inline_ void main_loop("))
state = power[power.index("typedef enum {"):power.index("uint8_t deepsleep_en;")]
parts = state + "\n" + function(power, "uint8_t app_power_prepare_loop(") + "\n" + parts
run_c(read("tests/fixtures/d008_scheduler/scheduler.c").replace("/* PRODUCTION */", parts),
      name="d008-scheduler")
