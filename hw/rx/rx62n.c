/*
 * RX62N Microcontroller
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 * (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/rx/rx62n.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "cpu.h"

/*
 * RX62N Internal Memory
 */
#define RX62N_IRAM_BASE     0x00000000
#define RX62N_DFLASH_BASE   0x00100000
#define RX62N_CFLASH_BASE   0xfff80000

/*
 * RX62N Peripheral Address
 * See users manual section 5
 */
#define RX62N_ICU_BASE  0x00087000
#define RX62N_TMR_BASE  0x00088200
#define RX62N_CMT_BASE  0x00088000
#define RX62N_SCI_BASE  0x00088240

/*
 * RX62N Peripheral IRQ
 * See users manual section 11
 */
#define RX62N_TMR_IRQ   174
#define RX62N_CMT_IRQ   28
#define RX62N_SCI_IRQ   214

/*
 * IRQ -> IPR mapping table
 * 0x00 - 0x91: IPR no (IPR00 to IPR91)
 * 0xff: IPR not assigned
 * See "11.3.1 Interrupt Vector Table" in hardware manual.
 */
static const uint8_t ipr_table[NR_IRQS] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 15 */
    0x00, 0xff, 0xff, 0xff, 0xff, 0x01, 0xff, 0x02,
    0xff, 0xff, 0xff, 0x03, 0x04, 0x05, 0x06, 0x07, /* 31 */
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x14, 0x14, 0x14, /* 47 */
    0x15, 0x15, 0x15, 0x15, 0xff, 0xff, 0xff, 0xff,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x1d, 0x1e, 0x1f, /* 63 */
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, /* 79 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x3a, 0x3b, 0x3c, 0xff, 0xff, 0xff, /* 95 */
    0x40, 0xff, 0x44, 0x45, 0xff, 0xff, 0x48, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 111 */
    0xff, 0xff, 0x51, 0x51, 0x51, 0x51, 0x52, 0x52,
    0x52, 0x53, 0x53, 0x54, 0x54, 0x55, 0x55, 0x56, /* 127 */
    0x56, 0x57, 0x57, 0x57, 0x57, 0x58, 0x59, 0x59,
    0x59, 0x59, 0x5a, 0x5b, 0x5b, 0x5b, 0x5c, 0x5c, /* 143 */
    0x5c, 0x5c, 0x5d, 0x5d, 0x5d, 0x5e, 0x5e, 0x5f,
    0x5f, 0x60, 0x60, 0x61, 0x61, 0x62, 0x62, 0x62, /* 159 */
    0x62, 0x63, 0x64, 0x64, 0x64, 0x64, 0x65, 0x66,
    0x66, 0x66, 0x67, 0x67, 0x67, 0x67, 0x68, 0x68, /* 175 */
    0x68, 0x69, 0x69, 0x69, 0x6a, 0x6a, 0x6a, 0x6b,
    0x6b, 0x6b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* 191 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x70, 0x71,
    0x72, 0x73, 0x74, 0x75, 0xff, 0xff, 0xff, 0xff, /* 207 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x80,
    0x80, 0x80, 0x81, 0x81, 0x81, 0x81, 0x82, 0x82, /* 223 */
    0x82, 0x82, 0x83, 0x83, 0x83, 0x83, 0xff, 0xff,
    0xff, 0xff, 0x85, 0x85, 0x85, 0x85, 0x86, 0x86, /* 239 */
    0x86, 0x86, 0xff, 0xff, 0xff, 0xff, 0x88, 0x89,
    0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, /* 255 */
};

/*
 * Level triggerd IRQ list
 * Not listed IRQ is Edge trigger.
 * See "11.3.1 Interrupt Vector Table" in hardware manual.
 */
static const uint8_t levelirq[] = {
     16,  21,  32,  44,  47,  48,  51,  64,  65,  66,
     67,  68,  69,  70,  71,  72,  73,  74,  75,  76,
     77,  78,  79,  90,  91, 170, 171, 172, 173, 214,
    217, 218, 221, 222, 225, 226, 229, 234, 237, 238,
    241, 246, 249, 250, 253,
};

static void register_icu(RX62NState *s)
{
    int i;
    SysBusDevice *icu;

    object_initialize_child(OBJECT(s), "icu", &s->icu, TYPE_RX_ICU);
    icu = SYS_BUS_DEVICE(&s->icu);
    qdev_prop_set_uint32(DEVICE(icu), "len-ipr-map", NR_IRQS);
    for (i = 0; i < NR_IRQS; i++) {
        char propname[32];
        snprintf(propname, sizeof(propname), "ipr-map[%d]", i);
        qdev_prop_set_uint32(DEVICE(icu), propname, ipr_table[i]);
    }
    qdev_prop_set_uint32(DEVICE(icu), "len-trigger-level",
                         ARRAY_SIZE(levelirq));
    for (i = 0; i < ARRAY_SIZE(levelirq); i++) {
        char propname[32];
        snprintf(propname, sizeof(propname), "trigger-level[%d]", i);
        qdev_prop_set_uint32(DEVICE(icu), propname, levelirq[i]);
    }

    for (i = 0; i < NR_IRQS; i++) {
        s->irq[i] = qdev_get_gpio_in(DEVICE(icu), i);
    }
    sysbus_realize(icu, &error_abort);
    sysbus_connect_irq(icu, 0, qdev_get_gpio_in(DEVICE(&s->cpu), RX_CPU_IRQ));
    sysbus_connect_irq(icu, 1, qdev_get_gpio_in(DEVICE(&s->cpu), RX_CPU_FIR));
    sysbus_connect_irq(icu, 2, s->irq[SWI]);
    sysbus_mmio_map(SYS_BUS_DEVICE(icu), 0, RX62N_ICU_BASE);
}

static void register_tmr(RX62NState *s, int unit)
{
    SysBusDevice *tmr;
    int i, irqbase;

    object_initialize_child(OBJECT(s), "tmr[*]",
                            &s->tmr[unit], TYPE_RENESAS_TMR);
    tmr = SYS_BUS_DEVICE(&s->tmr[unit]);
    qdev_prop_set_uint64(DEVICE(tmr), "input-freq", RX62N_PCLK);
    sysbus_realize(tmr, &error_abort);

    irqbase = RX62N_TMR_IRQ + TMR_NR_IRQ * unit;
    for (i = 0; i < TMR_NR_IRQ; i++) {
        sysbus_connect_irq(tmr, i, s->irq[irqbase + i]);
    }
    sysbus_mmio_map(tmr, 0, RX62N_TMR_BASE + unit * 0x10);
}

static void register_cmt(RX62NState *s, int unit)
{
    SysBusDevice *cmt;
    int i, irqbase;

    object_initialize_child(OBJECT(s), "cmt[*]",
                            &s->cmt[unit], TYPE_RENESAS_CMT);
    cmt = SYS_BUS_DEVICE(&s->cmt[unit]);
    qdev_prop_set_uint64(DEVICE(cmt), "input-freq", RX62N_PCLK);
    sysbus_realize(cmt, &error_abort);

    irqbase = RX62N_CMT_IRQ + CMT_NR_IRQ * unit;
    for (i = 0; i < CMT_NR_IRQ; i++) {
        sysbus_connect_irq(cmt, i, s->irq[irqbase + i]);
    }
    sysbus_mmio_map(cmt, 0, RX62N_CMT_BASE + unit * 0x10);
}

static void register_sci(RX62NState *s, int unit)
{
    SysBusDevice *sci;
    int i, irqbase;

    object_initialize_child(OBJECT(s), "sci[*]",
                            &s->sci[unit], TYPE_RENESAS_SCI);
    sci = SYS_BUS_DEVICE(&s->sci[unit]);
    qdev_prop_set_chr(DEVICE(sci), "chardev", serial_hd(unit));
    qdev_prop_set_uint64(DEVICE(sci), "input-freq", RX62N_PCLK);
    sysbus_realize(sci, &error_abort);

    irqbase = RX62N_SCI_IRQ + SCI_NR_IRQ * unit;
    for (i = 0; i < SCI_NR_IRQ; i++) {
        sysbus_connect_irq(sci, i, s->irq[irqbase + i]);
    }
    sysbus_mmio_map(sci, 0, RX62N_SCI_BASE + unit * 0x08);
}

static void rx62n_realize(DeviceState *dev, Error **errp)
{
    RX62NState *s = RX62N_MCU(dev);

    memory_region_init_ram(&s->iram, OBJECT(dev), "iram",
                           RX62N_IRAM_SIZE, &error_abort);
    memory_region_add_subregion(s->sysmem, RX62N_IRAM_BASE, &s->iram);
    memory_region_init_rom(&s->d_flash, OBJECT(dev), "flash-data",
                           RX62N_DFLASH_SIZE, &error_abort);
    memory_region_add_subregion(s->sysmem, RX62N_DFLASH_BASE, &s->d_flash);
    memory_region_init_rom(&s->c_flash, OBJECT(dev), "flash-code",
                           RX62N_CFLASH_SIZE, &error_abort);
    memory_region_add_subregion(s->sysmem, RX62N_CFLASH_BASE, &s->c_flash);

    if (!s->kernel) {
        rom_add_file_fixed(bios_name, RX62N_CFLASH_BASE, 0);
    }

    /* Initialize CPU */
    object_initialize_child(OBJECT(s), "cpu", &s->cpu, TYPE_RX62N_CPU);
    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);

    register_icu(s);
    s->cpu.env.ack = qdev_get_gpio_in_named(DEVICE(&s->icu), "ack", 0);
    register_tmr(s, 0);
    register_tmr(s, 1);
    register_cmt(s, 0);
    register_cmt(s, 1);
    register_sci(s, 0);
}

static Property rx62n_properties[] = {
    DEFINE_PROP_LINK("main-bus", RX62NState, sysmem, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_BOOL("load-kernel", RX62NState, kernel, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void rx62n_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rx62n_realize;
    device_class_set_props(dc, rx62n_properties);
}

static const TypeInfo rx62n_info = {
    .name = TYPE_RX62N_MCU,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(RX62NState),
    .class_init = rx62n_class_init,
};

static void rx62n_register_types(void)
{
    type_register_static(&rx62n_info);
}

type_init(rx62n_register_types)
