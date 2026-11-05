#pragma once
#include <stdint.h>
/* A bounded locator only: caller must validate that the initrd range is
 * readable RAM. No dereference beyond that range. Never starts a kernel. */
const void *lisa_find_payload(const void *initrd, uint64_t length, uint64_t *payload_size);
