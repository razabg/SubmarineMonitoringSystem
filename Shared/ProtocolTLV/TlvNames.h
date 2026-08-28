/*
 * tlv_names.h - human readable tag names, host side only.
 * Do not add this file to the firmware build.
 */

#ifndef SMS_TLV_NAMES_H
#define SMS_TLV_NAMES_H

#include "Tlv.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *tlv_tag_name(uint8_t tag);
const char *tlv_status_name(tlv_status_t st);

#ifdef __cplusplus
}
#endif

#endif /* SMS_TLV_NAMES_H */