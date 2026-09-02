// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
bool gyro::IOCancel(GyroRequest *request) {
    // TODO: CancelIoEx, the completion packet reports the outcome.
    return false;
}
#endif
