/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Salsa64 — Serial Interface (SI): DMA de 64 bytes entre la RDRAM y la PIF RAM.
 *
 * Una operación tarda aproximadamente 2 ms: durante ese tiempo SI_STATUS
 * lleva DMA_BUSY. Al terminar se hace la copia y se levanta la interrupción.
 * El PIF prepara los comandos joybus al completar una escritura y los ejecuta
 * al completar la lectura siguiente (pif.c).
 */
#include <string.h>

#include "core/bus.h"
#include "core/sched.h"
#include "cpu/vr4300.h"

/* Aproximación documentada de la transferencia completa del SI/PIF: 2 ms al
 * reloj de 93,75 MHz del VR4300. */
enum { SI_DMA_NONE, SI_DMA_READ, SI_DMA_WRITE };

static uint32_t si_dram(struct salsa64_bus *bus)
{
	uint32_t dram = bus->si[SALSA64_SI_DRAM_ADDR] & 0x00FFFFF8;
	return dram + SALSA64_PIF_RAM_SIZE <= bus->rdram_size ? dram : UINT32_MAX;
}

static void si_complete(void *ctx, uint64_t when)
{
	struct salsa64_bus *bus = ctx;
	uint32_t dram = bus->si_dma_dram;
	uint8_t op = bus->si_pending;
	(void)when;
	if (op == SI_DMA_NONE)
		return;
	bus->si_pending = SI_DMA_NONE;
	if (op == SI_DMA_READ) {
		salsa64_pif_execute(bus);
		if (dram != UINT32_MAX) {
			memcpy(bus->rdram + dram, bus->pif_ram, SALSA64_PIF_RAM_SIZE);
			salsa64_bus_rdram_written(bus, dram, SALSA64_PIF_RAM_SIZE);
		}
	} else if (dram != UINT32_MAX) {
		memcpy(bus->pif_ram, bus->rdram + dram, SALSA64_PIF_RAM_SIZE);
		salsa64_pif_written(bus);
	}
	salsa64_mi_raise(bus, SALSA64_IRQ_SI);
}

void salsa64_si_init(struct salsa64_bus *bus, struct salsa64_sched *sched)
{
	bus->si_sched = sched;
	salsa64_sched_handler(sched, SALSA64_EV_SI_DMA, si_complete, bus);
}

void salsa64_si_reset(struct salsa64_bus *bus)
{
	memset(bus->si, 0, sizeof bus->si);
	bus->si_dma_dram = 0;
	bus->si_pending = SI_DMA_NONE;
	if (bus->si_sched)
		salsa64_sched_cancel(bus->si_sched, SALSA64_EV_SI_DMA);
}

void salsa64_si_set_immediate(struct salsa64_bus *bus, bool immediate)
{
	bus->si_immediate = immediate;
}

uint32_t salsa64_si_read(const struct salsa64_bus *bus, uint32_t reg)
{
	uint32_t i = reg >> 2;
	if (i >= SALSA64_SI_NREGS)
		return 0;
	if (i == SALSA64_SI_STATUS) {
		uint32_t status = 0;
		if (bus->si_pending == SI_DMA_READ)
			status = SALSA64_SI_STATUS_READ | SALSA64_SI_STATUS_DMA_BUSY;
		else if (bus->si_pending == SI_DMA_WRITE)
			status = SALSA64_SI_STATUS_WRITE | SALSA64_SI_STATUS_DMA_BUSY;
		if (bus->mi.intr & (1u << SALSA64_IRQ_SI))
			status |= SALSA64_SI_STATUS_INTR;
		return status;
	}
	return bus->si[i];
}

void salsa64_si_write(struct salsa64_bus *bus, uint32_t reg, uint32_t v)
{
	uint32_t i = reg >> 2;
	switch (i) {
	case SALSA64_SI_DRAM_ADDR:
		bus->si[i] = v & 0x00FFFFFF;
		return;
	case SALSA64_SI_PIF_ADDR_RD64B:  /* PIF RAM → RDRAM */
		if (bus->si_pending != SI_DMA_NONE)
			return;  /* una sola operación en vuelo: una segunda se ignora */
		bus->si[i] = v;
		bus->si_dma_dram = si_dram(bus);
		bus->si_pending = SI_DMA_READ;
		if (bus->si_immediate)
			si_complete(bus, bus->cpu ? bus->cpu->cycles : 0);
		else if (bus->si_sched)
			salsa64_sched_set(bus->si_sched, SALSA64_EV_SI_DMA,
					  (bus->cpu ? bus->cpu->cycles : 0) + SALSA64_SI_DMA_CYCLES);
		return;
	case SALSA64_SI_PIF_ADDR_WR64B:  /* RDRAM → PIF RAM */
		if (bus->si_pending != SI_DMA_NONE)
			return;
		bus->si[i] = v;
		bus->si_dma_dram = si_dram(bus);
		bus->si_pending = SI_DMA_WRITE;
		if (bus->si_immediate)
			si_complete(bus, bus->cpu ? bus->cpu->cycles : 0);
		else if (bus->si_sched)
			salsa64_sched_set(bus->si_sched, SALSA64_EV_SI_DMA,
					  (bus->cpu ? bus->cpu->cycles : 0) + SALSA64_SI_DMA_CYCLES);
		return;
	case SALSA64_SI_STATUS:
		salsa64_mi_clear(bus, SALSA64_IRQ_SI);
		return;
	default:
		return;
	}
}
