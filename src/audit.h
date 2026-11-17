#ifndef F2400_AUDIT_H
#define F2400_AUDIT_H
#include "internal.h"
typedef struct {
    uint8_t cdb[16], sense[64], data[14016];
    unsigned cdb_len, sense_len, data_len, status, host, driver;
    int residual, has_status;
} wire_record;
typedef int (*record_callback)(const wire_record *, void *);
int audit_records(const char *path, record_callback callback, void *data);
int audit_good(const wire_record *r);
#endif
