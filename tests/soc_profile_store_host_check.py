"""真实 SOC profile/journal：固定视图、失败不发布、逐字节掉电及旧 payload。"""
from validation_support import read, run_c, evidence

code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bms_soc_profile_store.c"
static u8 flash[8192], backup[8192];
static int cut=-1, read_ok=1;
static unsigned programs, bytes;
static u8 chemistry=1u;
static int begin(void *c) { (void)c; return 1; }
static void end(void *c) { (void)c; }
static int rd(void *c,u32 a,u8 *d,u32 n) {
    (void)c; assert(a+n<=sizeof(flash)); if(!read_ok)return 0;
    memcpy(d,flash+a,n); return 1;
}
static int wr(void *c,u32 a,const u8 *d,u32 n) {
    (void)c; ++programs; assert(a+n<=sizeof(flash));
    for(u32 i=0;i<n;++i) {
        if(!cut)return 0;
        if(cut>0)--cut;
        assert((flash[a+i]|d[i])==flash[a+i]);flash[a+i]&=d[i];++bytes;
    }
    return 1;
}
static int erase(void *c,u32 a,u32 n) { (void)c;assert(a+n<=sizeof(flash));memset(flash+a,255,n);return 1; }
static const storage_port_t port={0,4096,4,255,begin,end,rd,wr,erase};
const storage_port_t *bms_storage_platform_port(void) { return &port; }
int bms_storage_platform_region(bms_storage_domain_t d,storage_region_t *r) {
    assert(d==BMS_STORAGE_DOMAIN_SOC_PROFILE);r->base=0;r->size=sizeof(flash);return 1;
}
int bms_config_store_get_soc(bms_soc_config_t *c) {
    memset(c,0,sizeof(*c));c->chemistry=chemistry;c->profile_id=chemistry;return 1;
}
void bms_error_raise(bms_error_id_t id) { (void)id; }
int main(void) {
    assert(sizeof(bms_soc_profile_config_t)==100u && BMS_SOC_PROFILE_WORDS==50u);
    assert(!bms_soc_profile_store_current());
    chemistry=0u;memset(flash,255,sizeof(flash));bms_soc_profile_store_init();
    assert(!bms_soc_profile_store_current()); /* AUTO 仍走算法回退，不能使用零点曲线。 */
    for(chemistry=1u;chemistry<=2u;++chemistry) {
        bms_soc_profile_config_t old,next,loaded;
        u8 payload[100];
        memset(flash,255,sizeof(flash));cut=-1;programs=0u;
        bms_soc_profile_store_init();assert(!programs && !bms_soc_profile_store_is_persisted());
        const bms_soc_profile_config_t *view=bms_soc_profile_store_current();
        assert(view && bms_soc_profile_store_valid(view));old=*view;
        /* 原布局按 50 个 little-endian word 编码，尾部无效点保持零。 */
        codec(&old,payload,1u);memset(&loaded,0,sizeof(loaded));codec(&loaded,payload,0u);
        assert(!memcmp(&old,&loaded,sizeof(old)));
        assert(payload[0]==chemistry && !payload[1] && payload[6]==old.point_count);
        for(unsigned i=0;i<50u;++i) {
            u16 v;memcpy(&v,(const u8*)&old+i*2u,2u);
            assert(payload[i*2u]==(u8)v && payload[i*2u+1u]==(u8)(v>>8));
        }
        for(unsigned i=old.point_count;i<15u;++i) assert(!old.points[i].mv&&!old.points[i].soc_percent);
        assert(bms_soc_profile_store_set(&old));assert(view==bms_soc_profile_store_current());
        unsigned same=programs;u16 generation=bms_soc_profile_store_generation();
        assert(bms_soc_profile_store_set(&old));assert(programs==same && generation==bms_soc_profile_store_generation());
        memcpy(backup,flash,sizeof(flash));next=old;next.current_deadband_ma=321u;++next.profile_version;
        assert(view->current_deadband_ma==old.current_deadband_ma); /* 仅编辑候选。 */
        bytes=0u;assert(bms_soc_profile_store_set(&next));unsigned total=bytes;
        assert(!memcmp(view,&next,sizeof(next)));
        for(unsigned byte=0;byte<total;++byte) {
            memcpy(flash,backup,sizeof(flash));cut=-1;bms_soc_profile_store_init();
            generation=bms_soc_profile_store_generation();cut=(int)byte;
            assert(!bms_soc_profile_store_set(&next));
            assert(generation==bms_soc_profile_store_generation() && !memcmp(view,&old,sizeof(old)));
            cut=-1;bms_soc_profile_store_init();assert(!memcmp(view,&old,sizeof(old)));
        }
        memcpy(flash,backup,sizeof(flash));bms_soc_profile_store_init();
        next=old;next.points[1].mv=next.points[0].mv;
        same=programs;assert(!bms_soc_profile_store_set(&next));assert(programs==same);
        next=old;next.chemistry=(u16)(3u-chemistry);next.profile_id=next.chemistry;
        assert(!bms_soc_profile_store_set(&next));assert(!memcmp(view,&old,sizeof(old)));
        /* reset 允许先于 Config 改类型；每个字节断点都不能发布候选。 */
        bms_soc_profile_config_t defaults;
        u8 target=(u8)(3u-chemistry);
        bms_soc_profile_store_defaults(target,&defaults);
        for(unsigned byte=0;byte<total;++byte) {
            memcpy(flash,backup,sizeof(flash));cut=-1;bms_soc_profile_store_init();
            generation=bms_soc_profile_store_generation();cut=(int)byte;
            assert(!bms_soc_profile_store_reset(target));
            assert(generation==bms_soc_profile_store_generation() && !memcmp(view,&old,sizeof(old)));
            assert(bms_soc_profile_store_is_persisted());
            cut=-1;bms_soc_profile_store_init();assert(!memcmp(view,&old,sizeof(old)));
        }
        memcpy(flash,backup,sizeof(flash));bms_soc_profile_store_init();
        s_generation=UINT16_MAX;bytes=0u;
        assert(bms_soc_profile_store_reset(target));assert(bytes==total);
        assert(bms_soc_profile_store_generation()==1u && !memcmp(view,&defaults,sizeof(defaults)));
        /* 与 set 去重不同，显式 reset 相同默认仍写 journal 并推进 generation。 */
        same=programs;generation=bms_soc_profile_store_generation();
        assert(bms_soc_profile_store_reset(target));
        assert(programs>same && bms_soc_profile_store_generation()==(u16)(generation+1u));
        memcpy(flash,backup,sizeof(flash));bms_soc_profile_store_init();
        /* 旧记录类型不匹配只降级默认，不自动覆写；读失败禁止后续写入。 */
        chemistry=(u8)(3u-chemistry);same=programs;bms_soc_profile_store_init();
        assert(view->chemistry==chemistry && !bms_soc_profile_store_is_persisted() && programs==same);
        chemistry=(u8)(3u-chemistry);read_ok=0;bms_soc_profile_store_init();
        assert(!bms_soc_profile_store_set(&old));assert(!bms_soc_profile_store_reset(chemistry));
        assert(programs==same);read_ok=1;
        printf("PASS SOC profile chemistry %u: set/reset each %u byte cuts, generation wrap, forced reset, unchanged 100-byte payload\n",chemistry,total);
    }
    return 0;
}
'''
output = run_c(code, ['bms/core/storage_record.c'], name='soc-profile-store')
evidence({'domain': 'storage', 'observations': output.strip(),
          'boundary': '完整 SOC profile 与 journal TU、RAM Flash；不含真实 Flash 时序与电源切断'})
