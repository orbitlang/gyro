// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#include <gyro/gyro.h>

struct Gyro {
};

extern "C" {
gyro_t *gyro_new(void) {
    // TODO
}

void gyro_free(gyro_t *gyro) {
    // TODO
}

const char *gyro_version(void) {
    return GYRO_VERSION_STRING;
}
} // extern "C"
