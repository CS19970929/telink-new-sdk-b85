#include "ct_boot_control.h"
#include "ct_config.h"

static volatile CtBootMailbox *boot_mailbox(void)
{
    return (volatile CtBootMailbox *)CT_BOOT_MAILBOX_ADDR;
}

static uint32_t boot_crc(uint32_t magic, uint32_t request)
{
    return magic ^ request ^ 0xA5A55A5Au;
}

static int boot_request_valid(void)
{
    volatile CtBootMailbox *mailbox = boot_mailbox();

    if ((mailbox->magic != CT_BOOT_MAILBOX_MAGIC) ||
        (mailbox->magic_inv != ~CT_BOOT_MAILBOX_MAGIC) ||
        (mailbox->request != CT_BOOT_MAILBOX_REQUEST) ||
        (mailbox->request_inv != ~CT_BOOT_MAILBOX_REQUEST) ||
        (mailbox->crc != boot_crc(CT_BOOT_MAILBOX_MAGIC, CT_BOOT_MAILBOX_REQUEST)))
    {
        return 0;
    }

    return 1;
}

int CtBoot_RequestIap(void)
{
    volatile CtBootMailbox *mailbox = boot_mailbox();

    mailbox->magic = CT_BOOT_MAILBOX_MAGIC;
    mailbox->magic_inv = ~CT_BOOT_MAILBOX_MAGIC;
    mailbox->request = CT_BOOT_MAILBOX_REQUEST;
    mailbox->request_inv = ~CT_BOOT_MAILBOX_REQUEST;
    mailbox->crc = boot_crc(CT_BOOT_MAILBOX_MAGIC, CT_BOOT_MAILBOX_REQUEST);

    return boot_request_valid();
}

void CtBoot_ClearRequest(void)
{
    volatile CtBootMailbox *mailbox = boot_mailbox();

    mailbox->magic = 0u;
    mailbox->magic_inv = 0u;
    mailbox->request = 0u;
    mailbox->request_inv = 0u;
    mailbox->crc = 0u;
}
