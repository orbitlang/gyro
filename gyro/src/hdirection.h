// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_HDIRECTION_H_
#define GYRO_HDIRECTION_H_

namespace gyro {
    /// Which of a handle's two queues an operation belongs to.
    enum class HandleDirection {
        IN,
        OUT
    };
} // namespace gyro

#endif // !GYRO_HDIRECTION_H_
