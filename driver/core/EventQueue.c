/*
 * EventQueue.c - bounded FIFO of variable-length events, guarded by a spinlock.
 *
 * Producers run inside kernel callbacks, possibly at DISPATCH_LEVEL; the reader
 * is a user-mode pull at PASSIVE_LEVEL. A spinlock is the right primitive to
 * bridge them, and a hard cap keeps a busy system from exhausting non-paged pool.
 */
#include "EventQueue.h"

#define AEGIS_TAG         'sigA'   /* 'Agis' in pool tracking */
#define AEGIS_MAX_QUEUED  4096     /* drop-oldest cap to bound non-paged memory */
#define AEGIS_MAX_EVENT   AEGIS_MAX_EVENT_SIZE

typedef struct _AEGIS_QUEUE_ENTRY {
    LIST_ENTRY ListEntry;
    USHORT     Bytes;        /* valid bytes in Data (header + payload) */
    UCHAR      Data[1];      /* AEGIS_EVENT_HEADER followed by payload  */
} AEGIS_QUEUE_ENTRY, *PAEGIS_QUEUE_ENTRY;

static LIST_ENTRY g_Queue;
static KSPIN_LOCK g_Lock;
static LONG       g_Count    = 0;
static ULONG      g_Sequence = 0;

void
AegisQueueInit(void)
{
    InitializeListHead(&g_Queue);
    KeInitializeSpinLock(&g_Lock);
    g_Count = 0;
    g_Sequence = 0;
}

void
AegisQueueDrain(void)
{
    KIRQL irql;
    LIST_ENTRY local;

    InitializeListHead(&local);

    /* Detach the whole list under the lock, then free outside it. */
    KeAcquireSpinLock(&g_Lock, &irql);
    while (!IsListEmpty(&g_Queue)) {
        InsertTailList(&local, RemoveHeadList(&g_Queue));
    }
    g_Count = 0;
    KeReleaseSpinLock(&g_Lock, irql);

    while (!IsListEmpty(&local)) {
        PAEGIS_QUEUE_ENTRY e =
            CONTAINING_RECORD(RemoveHeadList(&local), AEGIS_QUEUE_ENTRY, ListEntry);
        ExFreePoolWithTag(e, AEGIS_TAG);
    }
}

void
AegisPublish(unsigned short type, const void *payload, unsigned short payloadLen)
{
    USHORT total;
    SIZE_T allocSize;
    PAEGIS_QUEUE_ENTRY entry;
    PAEGIS_EVENT_HEADER hdr;
    KIRQL irql;
    PAEGIS_QUEUE_ENTRY evicted = NULL;

    /* Reject oversize payloads before the width-limited arithmetic below:
     * test payloadLen itself, since header + payloadLen could otherwise wrap
     * the USHORT 'total' and slip past the cap. */
    if (payloadLen > AEGIS_MAX_EVENT - sizeof(AEGIS_EVENT_HEADER)) {
        return;   /* payload larger than we are willing to carry */
    }

    total = (USHORT)(sizeof(AEGIS_EVENT_HEADER) + payloadLen);
    allocSize = FIELD_OFFSET(AEGIS_QUEUE_ENTRY, Data) + total;

    /* Build the entry outside the lock - allocation and copy don't need it,
     * and we want to hold the spinlock for as few instructions as possible. */
    entry = (PAEGIS_QUEUE_ENTRY)ExAllocatePool2(POOL_FLAG_NON_PAGED, allocSize, AEGIS_TAG);
    if (entry == NULL) {
        return;
    }
    entry->Bytes = total;

    hdr = (PAEGIS_EVENT_HEADER)entry->Data;
    hdr->Size = total;
    hdr->Type = type;
    KeQuerySystemTime(&hdr->Timestamp);

    if (payloadLen != 0 && payload != NULL) {
        RtlCopyMemory(entry->Data + sizeof(AEGIS_EVENT_HEADER), payload, payloadLen);
    }

    KeAcquireSpinLock(&g_Lock, &irql);
    hdr->Sequence = ++g_Sequence;
    if (g_Count >= AEGIS_MAX_QUEUED) {
        /* Full: make room by dropping the oldest. A slow reader loses history,
         * never the newest activity, and the producer never blocks. */
        evicted = CONTAINING_RECORD(RemoveHeadList(&g_Queue), AEGIS_QUEUE_ENTRY, ListEntry);
        g_Count--;
    }
    InsertTailList(&g_Queue, &entry->ListEntry);
    g_Count++;
    KeReleaseSpinLock(&g_Lock, irql);

    if (evicted != NULL) {
        ExFreePoolWithTag(evicted, AEGIS_TAG);
    }
}

ULONG
AegisQueuePull(void *buffer, ULONG bufferLen)
{
    UCHAR *out = (UCHAR *)buffer;
    LIST_ENTRY local;
    ULONG selected = 0;
    ULONG written = 0;
    KIRQL irql;

    InitializeListHead(&local);

    /* Detach a complete batch while holding the lock. Copying and freeing can
     * then happen at the caller's IRQL without making producers spin. */
    KeAcquireSpinLock(&g_Lock, &irql);
    while (!IsListEmpty(&g_Queue)) {
        PAEGIS_QUEUE_ENTRY e;

        /* Peek the head size; only dequeue if the whole event fits, so we never
         * hand the caller a half-event. The rest waits for the next pull. */
        e = CONTAINING_RECORD(g_Queue.Flink, AEGIS_QUEUE_ENTRY, ListEntry);
        if (e->Bytes > bufferLen - selected) {
            break;
        }
        InsertTailList(&local, RemoveHeadList(&g_Queue));
        g_Count--;
        selected += e->Bytes;
    }
    KeReleaseSpinLock(&g_Lock, irql);

    while (!IsListEmpty(&local)) {
        PAEGIS_QUEUE_ENTRY e =
            CONTAINING_RECORD(RemoveHeadList(&local), AEGIS_QUEUE_ENTRY, ListEntry);
        RtlCopyMemory(out + written, e->Data, e->Bytes);
        written += e->Bytes;
        ExFreePoolWithTag(e, AEGIS_TAG);
    }

    return written;
}
