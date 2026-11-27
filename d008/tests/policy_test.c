#include "../product.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    Config c;
    config_defaults(&c,16,false);
    c.capacity_dah=140;
    c.balance_stop_mv=12;
    c.sw[OV].trip=4290;
    assert(!config_valid(&c));
    assert(config_apply_updates(&c) && config_valid(&c));
    assert(c.capacity_dah==300 && c.hw_sc_a10==5200 && c.sw[OV].trip==3650);
    assert(c.balance_stop_mv==12);
    c.capacity_dah=200;
    c.hw_sc_a10=4800;
    assert(!config_apply_updates(&c));
    assert(c.capacity_dah==200 && c.hw_sc_a10==4800);
    c.update_id[UPDATE_CAPACITY]=5;
    assert(!config_apply_updates(&c) && c.capacity_dah==200);
    puts("PASS fixed product updates: skipped versions, repair before validation, user edits, newer receipt");
}
