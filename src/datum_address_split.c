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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <jansson.h>
#include <errno.h>

#include "datum_address_split.h"
#include "datum_logger.h"
#include "datum_conf.h"
#include "datum_utils.h"

// Hash table size (prime number for better distribution)
#define ADDR_SPLIT_HASH_SIZE 101

// Global hash table for address splits
static address_split_table_t addr_split_table = {0};

// Check if configuration file has been modified
static bool datum_address_split_check_file_modified(void) {
    const char *config_path = datum_address_split_get_config_path();
    struct stat file_stat;
    time_t current_time = time(NULL);
    
    // Skip frequent checks to reduce filesystem operations
    if (current_time < addr_split_table.next_mod_check_time) {
        return false;  // Too soon to check again
    }
    
    // Set next check time (check at most every 5 seconds)
    addr_split_table.next_mod_check_time = current_time + 5;
    
    if (!config_path || !config_path[0]) {
        return false;
    }
    
    if (stat(config_path, &file_stat) != 0) {
        DLOG_ERROR("Cannot stat address split config file: %s", config_path);
        return false;
    }
    
    if (file_stat.st_mtime > addr_split_table.last_file_mod_time) {
        addr_split_table.last_file_mod_time = file_stat.st_mtime;
        return true;
    }
    
    return false;
}

// Simple hash function for strings
static size_t hash_string(const char *str) {
    size_t hash = 5381;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash % ADDR_SPLIT_HASH_SIZE;
}

// Initialize the address split system
bool datum_address_split_init(void) {
    const char *config_path = datum_address_split_get_config_path();
    
    DLOG_INFO("SPLIT-INIT: Starting address split initialization");
    DLOG_INFO("SPLIT-INIT: Configuration path is set to: '%s'", 
              config_path && config_path[0] ? config_path : "[not set]");
    
    // Initialize the hash table
    addr_split_table.buckets = (address_split_entry_t **)calloc(ADDR_SPLIT_HASH_SIZE, sizeof(address_split_entry_t *));
    if (!addr_split_table.buckets) {
        DLOG_ERROR("SPLIT-INIT: Failed to allocate memory for address split hash table");
        return false;
    }
    
    addr_split_table.size = ADDR_SPLIT_HASH_SIZE;
    addr_split_table.count = 0;
    addr_split_table.last_load_time = 0;
    addr_split_table.last_file_mod_time = 0;
    addr_split_table.next_mod_check_time = 0;  // Allow initial check immediately
    
    // Initialize the lock
    if (pthread_rwlock_init(&addr_split_table.lock, NULL) != 0) {
        DLOG_ERROR("SPLIT-INIT: Failed to initialize address split table lock");
        free(addr_split_table.buckets);
        addr_split_table.buckets = NULL;
        return false;
    }
    
    DLOG_INFO("SPLIT-INIT: Hash table and lock initialized successfully");
    
    // Try to stat the configuration file
    if (config_path && config_path[0]) {
        struct stat file_stat;
        if (stat(config_path, &file_stat) == 0) {
            DLOG_INFO("SPLIT-INIT: Configuration file exists, size: %zu bytes, modified: %s", 
                    (size_t)file_stat.st_size, ctime(&file_stat.st_mtime));
        } else {
            DLOG_ERROR("SPLIT-INIT: Failed to stat configuration file: %s (errno: %d - %s)",
                      config_path, errno, strerror(errno));
        }
    }
    
    // Load the configuration
    if (!datum_address_split_load_config()) {
        DLOG_WARN("SPLIT-INIT: Failed to load initial address split configuration");
        // Continue anyway, we'll retry later
    }
    
    DLOG_INFO("SPLIT-INIT: Address split system initialization completed");
    return true;
}

// Clean up the address split system
void datum_address_split_cleanup(void) {
    if (!addr_split_table.buckets) {
        return;  // Already cleaned up or not initialized
    }
    
    // Acquire write lock
    pthread_rwlock_wrlock(&addr_split_table.lock);
    
    // Free all entries
    for (size_t i = 0; i < addr_split_table.size; i++) {
        address_split_entry_t *entry = addr_split_table.buckets[i];
        while (entry) {
            address_split_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    
    // Free the buckets
    free(addr_split_table.buckets);
    addr_split_table.buckets = NULL;
    addr_split_table.size = 0;
    addr_split_table.count = 0;
    
    // Release the lock
    pthread_rwlock_unlock(&addr_split_table.lock);
    
    // Destroy the lock
    pthread_rwlock_destroy(&addr_split_table.lock);
    
    DLOG_INFO("Address split system cleaned up");
}

// Get the configuration file path
const char *datum_address_split_get_config_path(void) {
    return datum_config.mining_address_split_json;
}

// Validate a recipient entry
static bool validate_recipient(json_t *recipient, address_split_recipient_t *out_recipient) {
    json_t *addr_json = json_object_get(recipient, "address");
    json_t *percentage_json = json_object_get(recipient, "percentage");
    
    // Check if recipient is a valid JSON object
    if (!json_is_object(recipient)) {
        DLOG_ERROR("Invalid recipient entry: Not a JSON object (type: %d)", json_typeof(recipient));
        return false;
    }
    
    // Check for required fields
    if (!addr_json) {
        DLOG_ERROR("Recipient missing required 'address' field");
        return false;
    }
    
    if (!json_is_string(addr_json)) {
        DLOG_ERROR("Recipient 'address' field must be a string (found type: %d)", json_typeof(addr_json));
        return false;
    }
    
    if (!percentage_json) {
        DLOG_ERROR("Recipient missing required 'percentage' field");
        return false;
    }
    
    const char *addr = json_string_value(addr_json);
    if (!addr || !addr[0]) {
        DLOG_ERROR("Recipient address is empty or null");
        return false;
    }
    
    // Make a copy of the address to parse
    char addr_copy[256];
    strncpy(addr_copy, addr, sizeof(addr_copy) - 1);
    addr_copy[sizeof(addr_copy) - 1] = '\0';
    
    // Handle the address.worker format: extract just the address part
    char *dot_pos = strchr(addr_copy, '.');
    if (dot_pos) {
        DLOG_DEBUG("Address contains worker suffix: '%s'", addr_copy);
        *dot_pos = '\0';  // Terminate string at the dot to get just the address
    }
    
    // Validate Bitcoin address using existing datum function
    unsigned char dummy[64];
    if (!addr_2_output_script(addr_copy, &dummy[0], 64)) {
        DLOG_ERROR("Invalid Bitcoin address in recipient: '%s' (not a valid BTC address)", addr_copy);
        return false;
    }
    
    // Get percentage, which can be a number or a string
    double percentage = 0.0;
    if (json_is_real(percentage_json)) {
        percentage = json_real_value(percentage_json);
        DLOG_DEBUG("Recipient percentage parsed as real number: %f", percentage);
    } else if (json_is_integer(percentage_json)) {
        percentage = (double)json_integer_value(percentage_json);
        DLOG_DEBUG("Recipient percentage parsed as integer: %f", percentage);
    } else if (json_is_string(percentage_json)) {
        const char* pct_str = json_string_value(percentage_json);
        percentage = atof(pct_str);
        DLOG_DEBUG("Recipient percentage parsed from string '%s' as: %f", pct_str, percentage);
    } else {
        DLOG_ERROR("Percentage must be a number or string (found type: %d)", json_typeof(percentage_json));
        return false;
    }
    
    if (percentage <= 0.0) {
        DLOG_ERROR("Percentage must be greater than 0 (got: %f)", percentage);
        return false;
    }
    
    if (percentage > 100.0) {
        DLOG_ERROR("Percentage must be less than or equal to 100 (got: %f)", percentage);
        return false;
    }
    
    // Use the original address string (including any worker suffix)
    strncpy(out_recipient->address, addr, sizeof(out_recipient->address) - 1);
    out_recipient->address[sizeof(out_recipient->address) - 1] = '\0';
    out_recipient->percentage = percentage;
    
    DLOG_DEBUG("Validated recipient: address='%s', percentage=%.2f%%", addr, percentage);
    return true;
}

// Validate and process a source address entry
static bool process_source_address(const char *source_addr, json_t *recipients_array, 
                                   address_split_config_t *out_config) {
    if (!json_is_array(recipients_array)) {
        DLOG_ERROR("SPLIT-PROCESS: Recipients for address %s is not an array (found type: %d)", 
                  source_addr, json_typeof(recipients_array));
        return false;
    }
    
    size_t recipient_count = json_array_size(recipients_array);
    if (recipient_count == 0) {
        DLOG_ERROR("SPLIT-PROCESS: No recipients defined for address %s", source_addr);
        return false;
    }
    
    if (recipient_count > MAX_ADDR_SPLIT_RECIPIENTS) {
        DLOG_WARN("SPLIT-PROCESS: Too many recipients for address %s, limiting to %d", 
                  source_addr, MAX_ADDR_SPLIT_RECIPIENTS);
        recipient_count = MAX_ADDR_SPLIT_RECIPIENTS;
    }
    
    // Clear the config
    memset(out_config, 0, sizeof(address_split_config_t));
    
    // Accept any string as source_addr without validation
    // This allows arbitrary usernames to be used as source addresses
    DLOG_DEBUG("SPLIT-PROCESS: Processing source address: %s", source_addr);
    
    // Process all recipients
    double total_percentage = 0.0;
    uint16_t threshold_base = 0;
    
    for (size_t i = 0; i < recipient_count; i++) {
        json_t *recipient = json_array_get(recipients_array, i);
        
        if (!validate_recipient(recipient, &out_config->recipients[out_config->num_recipients])) {
            DLOG_ERROR("SPLIT-PROCESS: Invalid recipient at index %zu for address %s", i, source_addr);
            continue;
        }
        
        // Calculate threshold for distribution
        double scaled_percentage = out_config->recipients[out_config->num_recipients].percentage * 0xFFFF / 100.0;
        out_config->recipients[out_config->num_recipients].threshold = threshold_base + (uint16_t)scaled_percentage;
        
        threshold_base = out_config->recipients[out_config->num_recipients].threshold;
        total_percentage += out_config->recipients[out_config->num_recipients].percentage;
        out_config->num_recipients++;
    }
    
    // Validate total percentage (allow small epsilon for floating point precision)
    if (out_config->num_recipients > 0 && (total_percentage < 99.99 || total_percentage > 100.01)) {
        DLOG_WARN("SPLIT-PROCESS: Total percentage for address %s is not 100%% (got %.2f%%). Normalizing...", 
                  source_addr, total_percentage);
        
        // Normalize the thresholds to ensure they add up to 0xFFFF
        uint16_t last_threshold = 0;
        for (int i = 0; i < out_config->num_recipients; i++) {
            double normalized_pct = out_config->recipients[i].percentage * 100.0 / total_percentage;
            out_config->recipients[i].percentage = normalized_pct;
            
            double scaled_threshold = normalized_pct * 0xFFFF / 100.0;
            out_config->recipients[i].threshold = last_threshold + (uint16_t)scaled_threshold;
            last_threshold = out_config->recipients[i].threshold;
        }
        
        // Ensure the last threshold is exactly 0xFFFF
        if (out_config->num_recipients > 0) {
            out_config->recipients[out_config->num_recipients - 1].threshold = 0xFFFF;
        }
    }
    
    DLOG_DEBUG("SPLIT-PROCESS: Successfully processed address %s with %d recipients, total percentage: %.2f%%",
              source_addr, out_config->num_recipients, total_percentage);
    
    return out_config->num_recipients > 0;
}

// Add an entry to the hash table
static bool add_address_split_entry(const char *source_addr, address_split_config_t *config) {
    size_t bucket = hash_string(source_addr);
    
    // Create a new entry
    address_split_entry_t *entry = (address_split_entry_t *)malloc(sizeof(address_split_entry_t));
    if (!entry) {
        DLOG_ERROR("Failed to allocate memory for address split entry");
        return false;
    }
    
    // Copy data
    strncpy(entry->address, source_addr, sizeof(entry->address) - 1);
    entry->address[sizeof(entry->address) - 1] = '\0';
    memcpy(&entry->config, config, sizeof(address_split_config_t));
    
    // Insert at the beginning of the linked list for this bucket
    entry->next = addr_split_table.buckets[bucket];
    addr_split_table.buckets[bucket] = entry;
    addr_split_table.count++;
    
    return true;
}

// Load the address split configuration from file
bool datum_address_split_load_config(void) {
    const char *config_path = datum_address_split_get_config_path();
    json_t *root = NULL;
    json_t *address_splits = NULL;
    json_error_t error;
    struct stat file_stat;
    
    // Check if config file is defined
    if (!config_path || !config_path[0]) {
        DLOG_INFO("SPLIT-LOAD: Address split configuration file path not defined");
        return false;
    }
    
    DLOG_INFO("SPLIT-LOAD: Attempting to load address split configuration from '%s'", config_path);
    
    // Check if file exists
    if (stat(config_path, &file_stat) != 0) {
        DLOG_ERROR("SPLIT-LOAD: Address split configuration file not found: %s (errno: %d - %s)", 
                  config_path, errno, strerror(errno));
        return false;
    }
    
    DLOG_INFO("SPLIT-LOAD: File exists, size: %zu bytes, modified: %s", 
             (size_t)file_stat.st_size, ctime(&file_stat.st_mtime));
    
    // Update last modification time
    addr_split_table.last_file_mod_time = file_stat.st_mtime;
    
    // Load and parse JSON
    DLOG_INFO("SPLIT-LOAD: Parsing JSON file...");
    root = json_load_file(config_path, 0, &error);
    if (!root) {
        DLOG_ERROR("SPLIT-LOAD: Failed to parse address split configuration: %s (line %d, column %d)",
                  error.text, error.line, error.column);
        return false;
    }
    
    // Get the address_splits object
    address_splits = json_object_get(root, "address_splits");
    if (!address_splits) {
        DLOG_ERROR("SPLIT-LOAD: Missing 'address_splits' object in configuration");
        json_decref(root);
        return false;
    }
    
    if (!json_is_object(address_splits)) {
        DLOG_ERROR("SPLIT-LOAD: 'address_splits' is not an object (found type: %d)", json_typeof(address_splits));
        json_decref(root);
        return false;
    }
    
    DLOG_INFO("SPLIT-LOAD: Found 'address_splits' object with %zu entries", json_object_size(address_splits));
    
    // Acquire write lock
    pthread_rwlock_wrlock(&addr_split_table.lock);
    
    // Clear existing entries
    for (size_t i = 0; i < addr_split_table.size; i++) {
        address_split_entry_t *entry = addr_split_table.buckets[i];
        while (entry) {
            address_split_entry_t *next = entry->next;
            free(entry);
            entry = next;
        }
        addr_split_table.buckets[i] = NULL;
    }
    addr_split_table.count = 0;
    
    // Process each source address
    const char *source_addr;
    json_t *recipients;
    
    DLOG_INFO("SPLIT-LOAD: Beginning to process address split configurations");
    
    json_object_foreach(address_splits, source_addr, recipients) {
        address_split_config_t config;
        
        DLOG_DEBUG("SPLIT-LOAD: Processing source address: %s", source_addr);
        
        if (process_source_address(source_addr, recipients, &config)) {
            DLOG_DEBUG("SPLIT-LOAD: Successfully processed %s with %d recipients:", 
                      source_addr, config.num_recipients);
                      
            for (int i = 0; i < config.num_recipients; i++) {
                DLOG_DEBUG("SPLIT-LOAD:   Recipient %d: %s (%.2f%%, threshold: 0x%04X)", 
                          i + 1, 
                          config.recipients[i].address,
                          config.recipients[i].percentage,
                          config.recipients[i].threshold);
            }
            
            add_address_split_entry(source_addr, &config);
        } else {
            DLOG_ERROR("SPLIT-LOAD: Failed to process source address: %s", source_addr);
        }
    }
    
    // Update last load time
    addr_split_table.last_load_time = time(NULL);
    
    // Release write lock
    pthread_rwlock_unlock(&addr_split_table.lock);
    
    DLOG_INFO("SPLIT-LOAD: Loaded %zu address split configurations from %s", 
              addr_split_table.count, config_path);
    
    // Cleanup
    json_decref(root);
    
    return true;
}

// Check if a split configuration exists for an address
bool datum_address_split_exists(const char *address) {
    bool exists = false;
    
    if (!address || !address[0] || !addr_split_table.buckets) {
        return false;
    }
    
    // Acquire read lock
    pthread_rwlock_rdlock(&addr_split_table.lock);
    
    // Find the address in the hash table
    size_t bucket = hash_string(address);
    address_split_entry_t *entry = addr_split_table.buckets[bucket];
    
    while (entry) {
        if (strcmp(entry->address, address) == 0) {
            exists = true;
            break;
        }
        entry = entry->next;
    }
    
    // Release read lock
    pthread_rwlock_unlock(&addr_split_table.lock);
    
    // Check if config file has been modified since last load - using optimized check
    if (datum_address_split_check_file_modified()) {
        DLOG_INFO("Address split configuration file modified, reloading...");
        datum_address_split_load_config();
        
        // Check again after reload
        return datum_address_split_exists(address);
    }
    
    return exists;
}

// Get the relevant recipient address for an address split
const char *datum_address_split_get_recipient(const char *source_address, char *recipient_buf, 
                                              size_t recipient_buf_sz, uint16_t share_rnd) {
    if (!source_address || !source_address[0] || !recipient_buf || recipient_buf_sz < 2 || 
        !addr_split_table.buckets) {
        return source_address;
    }
    
    DLOG_DEBUG("SPLIT: Received share from address: %s (randomness: 0x%04X)", 
               source_address, share_rnd);
    
    // Check if config file has been modified - using the optimized check
    if (datum_address_split_check_file_modified()) {
        DLOG_INFO("Address split configuration file modified, reloading...");
        datum_address_split_load_config();
    }
    
    // Acquire read lock
    pthread_rwlock_rdlock(&addr_split_table.lock);
    
    // Find the address in the hash table
    size_t bucket = hash_string(source_address);
    address_split_entry_t *entry = addr_split_table.buckets[bucket];
    
    while (entry) {
        if (strcmp(entry->address, source_address) == 0) {
            // Found the entry, log all configured recipients
            DLOG_DEBUG("SPLIT: Found split configuration for %s with %d recipients:", 
                      source_address, entry->config.num_recipients);
            
            for (int i = 0; i < entry->config.num_recipients; i++) {
                DLOG_DEBUG("SPLIT:   Recipient %d: %s (%.2f%%, threshold: 0x%04X)", 
                          i + 1, 
                          entry->config.recipients[i].address,
                          entry->config.recipients[i].percentage,
                          entry->config.recipients[i].threshold);
            }
            
            // Find the appropriate recipient based on share_rnd
            for (int i = 0; i < entry->config.num_recipients; i++) {
                if (share_rnd <= entry->config.recipients[i].threshold) {
                    // This is the recipient
                    strncpy(recipient_buf, entry->config.recipients[i].address, recipient_buf_sz - 1);
                    recipient_buf[recipient_buf_sz - 1] = '\0';
                    
                    // Release read lock
                    pthread_rwlock_unlock(&addr_split_table.lock);
                    
                    DLOG_INFO("SPLIT: Routing share from %s to recipient %s (randomness: 0x%04X, threshold: 0x%04X)",
                              source_address, recipient_buf, share_rnd, entry->config.recipients[i].threshold);
                    
                    return recipient_buf;
                }
            }
            
            // If we get here, something is wrong with the thresholds
            DLOG_ERROR("Failed to find recipient for source %s with share_rnd %u", 
                      source_address, share_rnd);
            break;
        }
        entry = entry->next;
    }
    
    // Release read lock
    pthread_rwlock_unlock(&addr_split_table.lock);
    
    // If no split configuration is found, return the original address
    DLOG_DEBUG("SPLIT: No split configuration found for %s, using original address", 
              source_address);
              
    return source_address;
}

// Force a reload of the address split configuration
bool datum_address_split_reload(void) {
    return datum_address_split_load_config();
}

// API endpoint to trigger reload of the configuration
bool datum_api_address_split_reload(void) {
    bool success = datum_address_split_reload();
    if (success) {
        DLOG_INFO("Address split configuration reloaded via API");
    } else {
        DLOG_ERROR("Failed to reload address split configuration via API");
    }
    return success;
} 