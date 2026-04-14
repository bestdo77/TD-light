/**
 * @file tdlight.h
 * @brief TDlight - Astronomical Light Curve Management & Classification System
 * 
 * Convenience header that includes all TDlight modules.
 * 
 * Architecture:
 *   config.h     - Configuration management (load/save config.json)
 *   sanitize.h   - Input validation and sanitization (SQL, shell, path)
 *   http_utils.h - HTTP response construction and parsing
 * 
 * @see https://github.com/bestdo77/TD-light
 */

#ifndef TDLIGHT_H
#define TDLIGHT_H

#include "config.h"
#include "sanitize.h"
#include "http_utils.h"

#endif // TDLIGHT_H
