/*
 *
 * DATUM Gateway
 * Decentralized Alternative Templates for Universal Mining
 *
 * This file is part of OCEAN's Bitcoin mining decentralization
 * project, DATUM.
 *
 * https://ocean.xyz
 *
 * ---
 *
 * Copyright (c) 2024 Bitcoin Ocean, LLC & Jason Hughes
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _DATUM_ADDRESS_SPLIT_H_
#define _DATUM_ADDRESS_SPLIT_H_

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <jansson.h>

// Maximum number of recipient addresses per source address
#define MAX_ADDR_SPLIT_RECIPIENTS 100

// Recipient in an address split configuration
typedef struct {
    char address[256];         // Recipient Bitcoin address
    double percentage;         // Percentage split (0-100)
    uint16_t threshold;        // Calculated threshold for random distribution
} address_split_recipient_t;

// Configuration for a single address
typedef struct {
    int num_recipients;                                  // Number of recipients
    address_split_recipient_t recipients[MAX_ADDR_SPLIT_RECIPIENTS]; // Array of recipients
} address_split_config_t;

// Hash table entry for address splits
typedef struct address_split_entry {
    char address[256];                  // Source address
    address_split_config_t config;      // Configuration for this address
    struct address_split_entry *next;   // Next entry in hash table bucket (for collisions)
} address_split_entry_t;

// Hash table for address splits
typedef struct {
    address_split_entry_t **buckets;    // Array of hash table buckets
    size_t size;                        // Number of buckets
    size_t count;                       // Number of entries
    pthread_rwlock_t lock;              // Lock for thread-safe access
    time_t last_load_time;              // Last time the configuration was loaded
    time_t last_file_mod_time;          // Last modification time of the config file
    time_t next_mod_check_time;         // Next time to check for file modifications
} address_split_table_t;

// Initialize the address split system
bool datum_address_split_init(void);

// Clean up the address split system
void datum_address_split_cleanup(void);

// Load the address split configuration from file
bool datum_address_split_load_config(void);

// Check if a split configuration exists for an address
bool datum_address_split_exists(const char *address);

// Get the relevant recipient address for an address split
const char *datum_address_split_get_recipient(const char *source_address, char *recipient_buf, 
                                              size_t recipient_buf_sz, uint16_t share_rnd);

// Force a reload of the address split configuration
bool datum_address_split_reload(void);

// Get the configuration file path
const char *datum_address_split_get_config_path(void);

// API endpoint to trigger reload of the configuration
bool datum_api_address_split_reload(void);

#endif /* _DATUM_ADDRESS_SPLIT_H_ */ 