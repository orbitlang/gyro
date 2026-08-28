// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_ERROR_INTERNAL_H_
#define GYRO_ERROR_INTERNAL_H_

namespace gyro {
    /**
     * @brief Normalizes a platform error code onto the gyro set.
     *
     * @param error errno on POSIX, a WSA or Win32 code on Windows.
     * @return A negative gyro code, GYRO_EUNKNOWN when there is no match.
     */
    int ErrorToStatus(int error);
} // namespace gyro

#endif // !GYRO_ERROR_INTERNAL_H_
