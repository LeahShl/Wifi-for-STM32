/**
 * @file tests_db.h
 * @brief Database interface for hardware testing program
 */
#pragma once
#include <stdint.h>

/* 
 * Uncomment next line if you want the database to be saved locally
 * (in the current working directory) instead of the user's 
 * home folder
 */
// #define LOCAL_DB_PATH 1

/*************************
 * FUNCTION DECLERATIONS *
 *************************/

/**
 * @brief Init tests database
 * 
 * @return int 1 if successful, 0 otherwise
 */
int init_db (void);

/**
 * @brief Log a test to database
 * 
 * @param test_id Unique test ID (use get_next_id() to get it beforehand)
 * @param timestamp Timestamp string in ISO 8601 format
 * @param duration_sec Test duration in seconds
 * @param result Test result
 * @return int 1 if successful, 0 otherwise
 */
int log_test (uint32_t test_id, const char *timestamp,
             double duration_sec, int result);

/**
 * @brief Print all logs to stdout in a csv format
 * 
 * @return int 1 if successful, 0 otherwise
 */
int print_all_logs (void);

/**
 * @brief Print formatted log details by test ID
 * 
 * @param test_id Unique test ID
 * @return int 1 if successful, 0 otherwise
 */
int print_log_by_id (uint32_t test_id);

/**
 * @brief Get the next test ID from database
 * 
 * @attention This function doesn't return the ID,
 * it writes the ID to dest and returns status.
 * 
 * @param dest Destination address
 * @return int 1 if successful, 0 otherwise
 */
int get_next_id (uint32_t *dest);
