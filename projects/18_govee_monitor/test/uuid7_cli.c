/* Re-derive a datapoint id from its inputs, for cross-checking live rows
 * against the firmware. Usage: uuid7_cli <epoch_ms> <device_id> <AA:BB:..:FF> */
#include "uuid7.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <epoch_ms> <device_id> <mac>\n", argv[0]);
        return 2;
    }
    unsigned m[6];
    if (sscanf(argv[3], "%2x:%2x:%2x:%2x:%2x:%2x",
               &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) {
        fprintf(stderr, "bad mac: %s\n", argv[3]);
        return 2;
    }
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)m[i];

    char out[UUID7_STR_LEN];
    uuid7_deterministic(strtoll(argv[1], NULL, 10), argv[2], mac, out);
    printf("%s\n", out);
    return 0;
}
