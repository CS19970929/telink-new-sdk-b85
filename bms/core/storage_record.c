#include "storage_record.h"

#define OFF_MAGIC       0u
#define OFF_FORMAT      4u
#define OFF_SCHEMA      6u
#define OFF_LENGTH      8u
#define OFF_SEQUENCE   12u
#define OFF_CRC        16u
#define OFF_COMMIT0    24u
#define OFF_COMMIT1    28u
#define OFF_PAYLOAD    STORAGE_RECORD_HEADER_SIZE
#define INVALID_ADDR   0xFFFFFFFFu
#define IO_CHUNK       32u

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t bit;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

uint32_t storage_record_crc32(const uint8_t *data, uint32_t len)
{
    return ~crc_update(0xFFFFFFFFu, data, len);
}

static uint32_t align_up(uint32_t value, uint32_t align)
{
    uint32_t rem = value % align;
    return rem ? value + align - rem : value;
}

static int begin_write(const storage_record_store_t *s)
{
    return (s->port->begin == 0) ? 1 : s->port->begin(s->port->ctx);
}

static void end_write(const storage_record_store_t *s)
{
    if (s->port->end != 0) s->port->end(s->port->ctx);
}

static int read_bytes(const storage_record_store_t *s, uint32_t addr,
                      uint8_t *buf, uint32_t len)
{
    return s->port->read(s->port->ctx, addr, buf, len);
}

static int program_bytes(const storage_record_store_t *s, uint32_t addr,
                         const uint8_t *buf, uint32_t len)
{
    uint8_t tail[STORAGE_RECORD_MAX_PROGRAM_SIZE];
    uint32_t unit = s->port->program_size;
    uint32_t full = len - (len % unit);
    uint32_t rest = len - full;
    uint32_t i;

    if ((addr % unit) != 0u) return 0;
    if ((full != 0u) && !s->port->program(s->port->ctx, addr, buf, full)) return 0;
    if (rest == 0u) return 1;
    for (i = 0u; i < unit; ++i) tail[i] = s->port->erased_value;
    for (i = 0u; i < rest; ++i) tail[i] = buf[full + i];
    return s->port->program(s->port->ctx, addr + full, tail, unit);
}

static uint32_t sector_base(const storage_record_store_t *s, uint16_t sector)
{
    return s->region.base + (uint32_t)sector * s->port->erase_size;
}

static uint16_t sector_index(const storage_record_store_t *s, uint32_t addr)
{
    return (uint16_t)((addr - s->region.base) / s->port->erase_size);
}

static uint32_t slot_addr(const storage_record_store_t *s,
                          uint16_t sector, uint16_t slot)
{
    return sector_base(s, sector) + (uint32_t)slot * s->slot_size;
}

static uint16_t slot_index(const storage_record_store_t *s, uint32_t addr)
{
    uint32_t base = sector_base(s, sector_index(s, addr));
    return (uint16_t)((addr - base) / s->slot_size);
}

static int is_erased(const storage_record_store_t *s, uint32_t addr, uint32_t len)
{
    uint8_t buf[IO_CHUNK];
    uint32_t i;
    uint32_t n;
    while (len != 0u) {
        n = (len > IO_CHUNK) ? IO_CHUNK : len;
        if (!read_bytes(s, addr, buf, n)) return 0;
        for (i = 0u; i < n; ++i) if (buf[i] != s->port->erased_value) return 0;
        addr += n;
        len -= n;
    }
    return 1;
}

static uint32_t payload_crc(const storage_record_store_t *s,
                            uint32_t sequence, const uint8_t *payload)
{
    uint8_t meta[12];
    uint32_t crc = 0xFFFFFFFFu;
    put32(meta, s->magic);
    put16(meta + 4u, s->schema_version);
    put16(meta + 6u, s->payload_size);
    put32(meta + 8u, sequence);
    crc = crc_update(crc, meta, sizeof(meta));
    return ~crc_update(crc, payload, s->payload_size);
}

static int flash_payload_crc(const storage_record_store_t *s, uint32_t addr,
                             uint32_t sequence, uint32_t *out)
{
    uint8_t meta[12];
    uint8_t buf[IO_CHUNK];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t remain = s->payload_size;
    uint32_t n;

    put32(meta, s->magic);
    put16(meta + 4u, s->schema_version);
    put16(meta + 6u, s->payload_size);
    put32(meta + 8u, sequence);
    crc = crc_update(crc, meta, sizeof(meta));
    addr += OFF_PAYLOAD;
    while (remain != 0u) {
        n = (remain > IO_CHUNK) ? IO_CHUNK : remain;
        if (!read_bytes(s, addr, buf, n)) return 0;
        crc = crc_update(crc, buf, n);
        addr += n;
        remain -= n;
    }
    *out = ~crc;
    return 1;
}

static int valid_record(const storage_record_store_t *s, uint32_t addr,
                        const uint8_t *h, uint32_t *sequence)
{
    uint32_t calc;
    if (get32(h + OFF_MAGIC) != s->magic ||
        get16(h + OFF_FORMAT) != STORAGE_RECORD_FORMAT_VERSION ||
        get16(h + OFF_SCHEMA) != s->schema_version ||
        get32(h + OFF_LENGTH) != s->payload_size ||
        get32(h + OFF_COMMIT0) != STORAGE_RECORD_COMMIT0 ||
        get32(h + OFF_COMMIT1) != STORAGE_RECORD_COMMIT1) return 0;
    *sequence = get32(h + OFF_SEQUENCE);
    if (*sequence == 0u) return 0;
    return flash_payload_crc(s, addr, *sequence, &calc) &&
           calc == get32(h + OFF_CRC);
}

static int sequence_newer(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) > 0;
}

int storage_record_open(storage_record_store_t *s, const storage_port_t *port,
                        storage_region_t region, uint32_t magic,
                        uint16_t schema, uint16_t payload_size)
{
    uint32_t slot;
    if (s == 0 || port == 0 || port->read == 0 || port->program == 0 ||
        port->erase == 0 || port->erase_size == 0u || port->program_size == 0u ||
        port->program_size > STORAGE_RECORD_MAX_PROGRAM_SIZE || payload_size == 0u ||
        region.size < port->erase_size * 2u ||
        region.base % port->erase_size != 0u || region.size % port->erase_size != 0u)
        return 0;
    slot = align_up(STORAGE_RECORD_HEADER_SIZE + payload_size, port->program_size);
    if (slot > port->erase_size || STORAGE_RECORD_HEADER_SIZE % port->program_size != 0u ||
        OFF_COMMIT0 % port->program_size != 0u || 8u % port->program_size != 0u)
        return 0;
    s->port = port;
    s->region = region;
    s->magic = magic;
    s->schema_version = schema;
    s->payload_size = payload_size;
    s->slot_size = (uint16_t)slot;
    s->slots_per_sector = (uint16_t)(port->erase_size / slot);
    s->sector_count = (uint16_t)(region.size / port->erase_size);
    s->latest_addr = INVALID_ADDR;
    s->next_sequence = 1u;
    s->has_latest = 0u;
    s->ready = s->slots_per_sector ? 1u : 0u;
    return s->ready;
}

int storage_record_load(storage_record_store_t *s, uint8_t *payload)
{
    uint8_t h[STORAGE_RECORD_HEADER_SIZE];
    uint16_t sec, slot;
    uint32_t addr, seq, best_seq = 0u, best_addr = INVALID_ADDR;
    uint8_t found = 0u;
    if (s == 0 || !s->ready || payload == 0) return 0;
    for (sec = 0u; sec < s->sector_count; ++sec) {
        for (slot = 0u; slot < s->slots_per_sector; ++slot) {
            addr = slot_addr(s, sec, slot);
            if (!read_bytes(s, addr, h, sizeof(h))) return 0;
            if (valid_record(s, addr, h, &seq) && (!found || sequence_newer(seq, best_seq))) {
                found = 1u;
                best_seq = seq;
                best_addr = addr;
            }
        }
    }
    if (!found) {
        s->latest_addr = INVALID_ADDR;
        s->next_sequence = 1u;
        s->has_latest = 0u;
        return 0;
    }
    if (!read_bytes(s, best_addr + OFF_PAYLOAD, payload, s->payload_size)) return 0;
    s->latest_addr = best_addr;
    s->next_sequence = best_seq + 1u;
    if (s->next_sequence == 0u) s->next_sequence = 1u;
    s->has_latest = 1u;
    return 1;
}

static int prepare_target(storage_record_store_t *s, uint32_t *addr, uint8_t *erase)
{
    uint16_t sec, slot, next;
    if (!s->has_latest) {
        *addr = slot_addr(s, 0u, 0u);
        *erase = 1u;
        return 1;
    }
    sec = sector_index(s, s->latest_addr);
    slot = (uint16_t)(slot_index(s, s->latest_addr) + 1u);
    if (slot < s->slots_per_sector) {
        *addr = slot_addr(s, sec, slot);
        if (is_erased(s, *addr, s->slot_size)) {
            *erase = 0u;
            return 1;
        }
    }
    next = (uint16_t)(sec + 1u);
    if (next >= s->sector_count) next = 0u;
    if (next == sec) return 0;
    *addr = slot_addr(s, next, 0u);
    *erase = 1u;
    return 1;
}

int storage_record_save(storage_record_store_t *s, const uint8_t *payload)
{
    uint8_t header[STORAGE_RECORD_HEADER_SIZE];
    uint8_t commit[8];
    uint32_t addr, sequence, crc, base;
    uint8_t erase;
    uint32_t i;
    int ok = 0;

    if (s == 0 || !s->ready || payload == 0 || !prepare_target(s, &addr, &erase)) return 0;
    sequence = s->next_sequence ? s->next_sequence : 1u;
    crc = payload_crc(s, sequence, payload);
    for (i = 0u; i < sizeof(header); ++i) header[i] = s->port->erased_value;
    put32(header + OFF_MAGIC, s->magic);
    put16(header + OFF_FORMAT, STORAGE_RECORD_FORMAT_VERSION);
    put16(header + OFF_SCHEMA, s->schema_version);
    put32(header + OFF_LENGTH, s->payload_size);
    put32(header + OFF_SEQUENCE, sequence);
    put32(header + OFF_CRC, crc);
    put32(commit, STORAGE_RECORD_COMMIT0);
    put32(commit + 4u, STORAGE_RECORD_COMMIT1);

    if (!begin_write(s)) return 0;
    if (erase) {
        base = sector_base(s, sector_index(s, addr));
        if (!s->port->erase(s->port->ctx, base, s->port->erase_size)) goto done;
    }
    if (!program_bytes(s, addr, header, OFF_COMMIT0)) goto done;
    if (!program_bytes(s, addr + OFF_PAYLOAD, payload, s->payload_size)) goto done;
    if (!program_bytes(s, addr + OFF_COMMIT0, commit, sizeof(commit))) goto done;
    s->latest_addr = addr;
    s->has_latest = 1u;
    s->next_sequence = sequence + 1u;
    if (s->next_sequence == 0u) s->next_sequence = 1u;
    ok = 1;
done:
    end_write(s);
    return ok;
}

int storage_record_format(storage_record_store_t *s)
{
    uint16_t sec;
    int ok = 1;
    if (s == 0 || !s->ready || !begin_write(s)) return 0;
    for (sec = 0u; sec < s->sector_count; ++sec) {
        if (!s->port->erase(s->port->ctx, sector_base(s, sec), s->port->erase_size)) {
            ok = 0;
            break;
        }
    }
    end_write(s);
    if (ok) {
        s->latest_addr = INVALID_ADDR;
        s->next_sequence = 1u;
        s->has_latest = 0u;
    }
    return ok;
}
