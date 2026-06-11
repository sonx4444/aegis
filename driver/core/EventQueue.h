/*
 * EventQueue.h - the driver's central event pipeline.
 *
 * Monitor modules translate a kernel callback into a payload and call
 * AegisPublish(). The core drains the queue into user mode via AegisQueuePull().
 * This is the single seam every present and future module shares.
 */
#pragma once
#include <ntddk.h>
#include "AegisDriverProtocol.h"

/* Initialize / tear down the queue. Call once from DriverEntry / Unload. */
void AegisQueueInit(void);
void AegisQueueDrain(void);

/*
 * Wrap 'payload' (payloadLen bytes, may be NULL if 0) in an AEGIS_EVENT_HEADER
 * of the given type and enqueue it. Safe to call at <= DISPATCH_LEVEL.
 * Silently drops the event on allocation failure or oversize payload.
 */
void AegisPublish(unsigned short type, const void *payload, unsigned short payloadLen);

/*
 * Copy as many whole events as fit into 'buffer'; returns bytes written.
 * The caller must provide at least AEGIS_MAX_EVENT_SIZE bytes. Events that
 * don't fit after the first stay queued for the next pull.
 */
ULONG AegisQueuePull(void *buffer, ULONG bufferLen);
