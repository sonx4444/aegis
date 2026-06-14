/*
 * Modules.h - registry of monitor modules.
 *
 * Each module exposes a Start/Stop pair. DriverEntry starts them, Unload stops
 * them. Adding a module (thread/image/registry/file/network) = a new .c with a
 * Start/Stop pair declared here and one Start call in Driver.c.
 */
#pragma once
#include <ntddk.h>

NTSTATUS ProcessMonStart(void);
void     ProcessMonStop(void);

NTSTATUS ThreadMonStart(void);
void     ThreadMonStop(void);

NTSTATUS ImageMonStart(void);
void     ImageMonStop(void);
