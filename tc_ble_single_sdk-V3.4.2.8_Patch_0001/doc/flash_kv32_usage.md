# flash_kv32 使用文档

## 1. 文档目的

这份文档只回答一个问题：以后怎么用 `flash_kv32`。  
设计原理、事务格式、掉电语义、磨损均衡策略请看 `doc/flash_kv32_design.md`，这里重点写接入方法、接口说明、推荐用法和扩展方式。

## 2. 现在仓库里有哪些文件

### 2.1 通用持久化层

- `tc_ble_single_sdk/vendor/ble_sample/flash_kv32.h`
- `tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c`

这是通用层，负责：

- Flash 扇区扫描
- 事务追加
- compact 压缩
- generation 判新旧
- tail dirty 处理
- CRC32 校验
- 掉电恢复

### 2.2 业务封装层

- `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.h`
- `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`

这是当前已经落地的热区封装，用来持久化：

- `SOC`
- `DSG`
- `cycle`

### 2.3 编译集成

如果工程文件不是自动扫描源文件，而是手工维护源文件列表，那么新增模块时还要把下面文件加入工程：

- `tc_ble_single_sdk/vendor/ble_sample/flash_kv32.c`

头文件只需要能被包含路径找到即可：

- `tc_ble_single_sdk/vendor/ble_sample/flash_kv32.h`

如果以后新增自己的 wrapper，例如：

- `config_kv_store.c`
- `calib_kv_store.c`

也要把这些 `.c` 文件一起加入工程编译。

## 3. 适用范围

`flash_kv32` 适合下面这类数据：

- 数量不多
- 每项值都是 `u32`
- 需要掉电保存
- 不适合频繁整扇区重写
- 不需要文件系统

典型场景：

- SOC / DSG / 循环次数
- 运行计数
- 配置参数
- 校准参数
- 出厂参数索引

不适合：

- 大块二进制数据
- 变长字符串
- 图片、日志文件、升级包

## 4. 当前对外接口

### 4.1 通用层接口

`flash_kv32.h` 暴露的核心接口如下：

```c
int flash_kv32_init(flash_kv32_t *kv,
                    const flash_kv32_cfg_t *cfg,
                    flash_kv32_cache_entry_t *cache);

int flash_kv32_format(flash_kv32_t *kv);
int flash_kv32_get(const flash_kv32_t *kv, u32 key, u32 *value);
int flash_kv32_set(flash_kv32_t *kv, u32 key, u32 value);
int flash_kv32_write_pairs(flash_kv32_t *kv,
                           const flash_kv32_pair_t *pairs,
                           u16 pair_count);
int flash_kv32_compact(flash_kv32_t *kv);
flash_kv32_dbg_t flash_kv32_get_dbg(const flash_kv32_t *kv);
```

接口含义：

- `init`
  - 初始化并扫描分区。
  - 如果分区还没格式化，会自动格式化并写入第一份完整快照。

- `format`
  - 整个分区恢复默认值并重新建库。
  - 适合出厂恢复或清空分区。

- `get`
  - 读取某个 key 当前值。

- `set`
  - 写一个 key。

- `write_pairs`
  - 原子写多个 key。
  - 推荐业务优先用这个接口，避免同一批参数拆成多条事务。

- `compact`
  - 主动触发压缩。
  - 一般不需要主动调，除非你想在休眠前、空闲时主动整理。

- `get_dbg`
  - 获取当前活动扇区、写偏移、代次等调试信息。

### 4.2 业务层接口

当前 `soc_kv_store` 暴露的接口如下：

```c
int  soc_kv_store_init(void);
soc_kv_data_t soc_kv_store_get(void);
int  soc_kv_store_put(soc_item_t item, u32 value);
void soc_kv_store_update_and_log_if_changed(u32 soc, u32 dsg, u32 cycle);
void soc_kv_store_factory_reset(void);
soc_kv_dbg_t soc_kv_store_get_dbg(void);
```

当前 `soc_kv_data_t` 已经是：

```c
typedef struct {
    u32 soc;
    u32 dsg;
    u32 cycle;
} soc_kv_data_t;
```

也就是说，`cycle` 现在不会再被截成 `u16`。

## 5. 当前项目里怎么用

### 5.1 启动初始化

系统启动后先初始化存储，再把恢复出来的数据喂给业务层：

```c
soc_kv_store_init();
soc_kv_data_t d = soc_kv_store_get();
soc_param_lib_init(&d);
```

当前项目已经是这样接的。

### 5.2 主循环更新

如果一批参数属于同一个原子状态，应该一次一起写，而不是拆开写：

```c
soc_kv_store_update_and_log_if_changed(
    SOC_Calculate_Element.u8SOC_Now,
    SOC_Calculate_Element.u8DSG_SOC_Int,
    SOC_Calculate_Element.u32Cycle_times);
```

这样做的好处：

- 一次事务同时写入 `soc/dsg/cycle`
- 掉电后不会恢复出跨版本拼接状态
- Flash 写放大更小

### 5.3 单项更新

如果只改一个值，可以用：

```c
soc_kv_store_put(SOC_ITEM_CYCLE, new_cycle);
```

### 5.4 恢复默认值

如果需要恢复默认值：

```c
soc_kv_store_factory_reset();
```

这个操作会清空整个热区分区，然后用默认值重新建第一份快照。

## 6. 如何新建一个自己的 KV 分区

如果未来要存“配置参数”或“校准参数”，不要再复制 `soc_kv_store`。  
正确方式是新建一个新的业务封装，例如：

- `config_kv_store.c/.h`
- `calib_kv_store.c/.h`

底层都复用 `flash_kv32`。

### 6.1 第一步：定义 key

示例：

```c
#define CONFIG_KV_KEY_MODE         0x1001u
#define CONFIG_KV_KEY_BALANCE_EN   0x1002u
#define CONFIG_KV_KEY_CUTOFF_MV    0x1003u
```

要求：

- 同一个分区内的 key 不能重复
- 建议按模块分段编号，方便维护

### 6.2 第二步：定义默认值表

```c
static const flash_kv32_key_def_t g_config_keys[] = {
    { CONFIG_KV_KEY_MODE,       0 },
    { CONFIG_KV_KEY_BALANCE_EN, 1 },
    { CONFIG_KV_KEY_CUTOFF_MV,  2800 },
};
```

### 6.3 第三步：准备运行时对象

```c
static flash_kv32_t g_config_kv;
static flash_kv32_cache_entry_t g_config_cache[3];
static u32 g_config_sector_addrs[2];
```

其中：

- `g_config_kv` 是实例对象
- `g_config_cache` 长度要等于 key 数量
- `g_config_sector_addrs` 长度要等于分区扇区数

### 6.4 第四步：实现 Flash 端口

如果还是 Telink 当前平台，可以直接复用当前写法：

```c
static void config_flash_read(void *ctx, u32 addr, u8 *buf, u32 len)
{
    (void)ctx;
    flash_read_page(addr, (int)len, buf);
}

static int config_flash_prog(void *ctx, u32 addr, const u8 *buf, u32 len)
{
    (void)ctx;
    flash_write_page(addr, (int)len, (u8 *)buf);
    return 1;
}

static int config_flash_erase_sector(void *ctx, u32 addr, u32 size)
{
    (void)ctx;
    (void)size;
    flash_erase_sector(addr);
    return 1;
}
```

再组一个 `flash_kv32_port_t`：

```c
static const flash_kv32_port_t g_config_port = {
    0,
    config_flash_read,
    config_flash_prog,
    config_flash_erase_sector,
    0,
    0,
};
```

### 6.5 第五步：配置分区地址

示例：

```c
g_config_sector_addrs[0] = CONFIG_KV_BASE + 0 * FLASH_SECTOR_SIZE;
g_config_sector_addrs[1] = CONFIG_KV_BASE + 1 * FLASH_SECTOR_SIZE;
```

如果是热区，建议给更多扇区，例如 `4` 或 `8`。  
如果是冷区，通常 `2` 或 `4` 就够。

### 6.6 第六步：初始化

```c
flash_kv32_cfg_t cfg;

memset(&cfg, 0, sizeof(cfg));
cfg.port = &g_config_port;
cfg.sector_addrs = g_config_sector_addrs;
cfg.keys = g_config_keys;
cfg.sector_count = 2;
cfg.sector_size = FLASH_SECTOR_SIZE;
cfg.write_align = 4;
cfg.key_count = sizeof(g_config_keys) / sizeof(g_config_keys[0]);

flash_kv32_init(&g_config_kv, &cfg, g_config_cache);
```

### 6.7 第七步：读写

读：

```c
u32 mode = 0;
flash_kv32_get(&g_config_kv, CONFIG_KV_KEY_MODE, &mode);
```

写一个值：

```c
flash_kv32_set(&g_config_kv, CONFIG_KV_KEY_MODE, 2);
```

一次写多个值：

```c
flash_kv32_pair_t pairs[2];

pairs[0].key = CONFIG_KV_KEY_MODE;
pairs[0].value = 2;
pairs[1].key = CONFIG_KV_KEY_BALANCE_EN;
pairs[1].value = 1;

flash_kv32_write_pairs(&g_config_kv, pairs, 2);
```

## 7. 怎么做冷热分区

冷热分区不是一个接口开关，而是“多个独立实例”。

### 7.1 推荐分法

热区：

- SOC
- cycle
- 运行分钟数
- 高频状态计数

冷区：

- 配置项
- 校准项
- 出厂参数
- 用户偏好设置

### 7.2 推荐配置

热区建议：

- 扇区数：`4~8`
- 每区 key 数量：少量
- 写频率：高

冷区建议：

- 扇区数：`2~4`
- 每区 key 数量：少量到中等
- 写频率：低

### 7.3 接入方式

做两个实例即可：

```c
static flash_kv32_t g_hot_kv;
static flash_kv32_t g_cold_kv;
```

分别配置：

- `sector_addrs`
- `sector_count`
- `keys`
- `cache`

两边互不影响，也不共享 compact。

## 8. 以后如何扩展新 key

步骤非常固定：

1. 在对应业务封装里新增 key 宏。
2. 把 key 加进 `flash_kv32_key_def_t` 默认表。
3. 把 cache 数组长度加 1。
4. 如果有业务结构体，就把字段加进去。
5. 在 `get/set/update` 封装里补映射。

只要不改已有 key 的含义，Flash 格式本身不需要改。

## 9. 如何移植到其他 MCU

核心原则：只换 Flash 端口，不动 `flash_kv32` 核心逻辑。

### 9.1 你需要实现的最小接口

```c
read(addr, buf, len)
prog(addr, buf, len)
erase_sector(addr, size)
```

可选接口：

```c
lock()
unlock()
```

### 9.2 需要确认的 MCU 约束

移植前确认下面几个参数：

- 扇区大小
- 最小写粒度
- Flash 是否要求写前对齐
- 擦除是否只能整扇区
- 是否需要在临界区里写 Flash

然后正确填写：

- `cfg.sector_size`
- `cfg.write_align`

### 9.3 不要做的事

不要直接把 C 结构体原样写入 Flash。  
`flash_kv32` 当前已经使用手工字节序列化，移植时不要再改回“整结构体落盘”。

## 10. 使用建议

### 10.1 优先批量写

如果一组参数逻辑上属于同一时刻状态，优先用：

- `flash_kv32_write_pairs`
- 或业务封装里的批量更新接口

不要拆成连续多个 `set`。

### 10.2 不要在中断里擦扇区

compact 过程中会擦扇区。  
擦扇区通常是长阻塞操作，不适合在中断上下文里做。

建议：

- 在主循环
- 在空闲任务
- 在休眠前
- 在允许短阻塞的任务上下文

进行写入或 compact。

### 10.3 高频热区要留足扇区

如果某类数据写得很频繁，不要只给 `2` 个扇区。  
否则虽然逻辑正确，但寿命和 GC 抖动都会比较差。

### 10.4 一个分区当前有效 key 集必须能装进一个扇区

这是当前实现的硬约束。  
compact 时会把“当前有效值集合”写成一条完整快照事务，所以必须能放进单扇区。

### 10.5 同一个实例不要被多个上下文无保护并发写

当前 `flash_kv32` 没有做多线程并发仲裁。  
如果同一个实例会被多个任务访问，应该在业务层自己加互斥或串行化调用。

## 11. 掉电语义

对使用者来说，可以按下面理解：

- 事务尾 `commit_magic` 没写完的记录，重启后会被丢弃。
- 新扇区没完成 `active_mark`，重启后不会抢占旧扇区。
- 新扇区已完成 `active_mark`，即使旧扇区还没擦，也会优先选新扇区。
- 遇到脏尾时不会继续原地续写，而是后续 compact 转到下一扇区。

也就是说，业务上可以认为：

- 旧数据不会因为半写而被污染
- 新数据只有在完整提交后才会生效

## 12. 当前 `soc_kv_store` 的配置点

`soc_kv_store.h` 里已经预留了热区/冷区配置宏：

```c
#define SOC_KV_HOT_BASE
#define SOC_KV_HOT_SECTOR_SIZE
#define SOC_KV_HOT_SECTORS

#define SOC_KV_COLD_BASE
#define SOC_KV_COLD_SECTOR_SIZE
#define SOC_KV_COLD_SECTORS
```

当前实际只接了热区。  
以后如果要接冷区，推荐新建：

- `config_kv_store`
- `calib_kv_store`

不要继续往 `soc_kv_store` 里硬塞。

## 13. 常见问题

### 13.1 想新增一个参数，必须改 Flash 格式吗

不需要。  
只要新增 key，并把它放进默认值表即可。

### 13.2 想改默认值，会影响旧设备吗

会有两个层面的影响：

- 新格式化的设备会用新默认值
- 已经落盘过的旧设备会继续保留原先存储值

所以改默认值时要确认这是不是你想要的行为。

### 13.3 想提寿命，优先做什么

优先顺序建议如下：

1. 热冷分区
2. 热区增加扇区数量
3. 合并成批量事务写
4. 减少无意义重复写

### 13.4 想看当前状态

可以调用：

```c
flash_kv32_get_dbg(...)
soc_kv_store_get_dbg()
```

常用观测项：

- 当前活动扇区
- 当前 generation
- 当前写偏移
- `tail_dirty`

## 14. 推荐落地规范

为了后续维护统一，建议以后所有新的持久化模块都按下面规则写：

1. 每个业务分区单独一个 wrapper，不直接在业务代码里散着调用 `flash_kv32`。
2. 高频数据和低频数据必须分区。
3. 默认优先使用批量更新接口。
4. key 编号按模块分段。
5. wrapper 对外暴露业务结构体，不把通用 key 细节扩散到全项目。

## 15. 总结

以后使用时，可以按一句话理解：

- 如果只是用现成的 SOC 持久化，就继续用 `soc_kv_store_*`。
- 如果要新增别的持久化模块，就新建一个 wrapper，底层直接复用 `flash_kv32`。
- 如果数据冷热差异很大，就拆成两个或更多 `flash_kv32` 实例。
- 如果移植 MCU，只改 Flash 端口和配置，不改核心格式。
