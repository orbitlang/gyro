// This source file is part of the Gyro project.
//
// Licensed under the Apache License v2.0

#ifndef GYRO_PLATFORM_H_
#define GYRO_PLATFORM_H_

/**
 * @file platform.h
 *
 * Which operating system gyro is being compiled for, in one place.
 *
 * Each of these is **always defined**, to 1 or to 0, and is meant to be used
 * with `#if` rather than `#ifdef`. That is the point: `#if GYRO_OS_WINDOWS`
 * reads as a question with an answer, several of them combine with `||` without
 * repeating `defined()`, and a build with `-Wundef` turns a misspelled name
 * into a warning instead of a branch that is quietly never taken.
 *
 * The compiler macros underneath are spelled out once here because they are
 * easy to get subtly wrong: Windows answers to four different names depending
 * on the toolchain, and `BSD` only exists after `<sys/param.h>` has been
 * included, which is a good way to compile a whole backend out of a build
 * without noticing.
 *
 * This header is public because the public API needs it too: the socket type
 * and the buffer layout differ per platform, and they are described in terms
 * of these.
 */

/// Windows, under any of the four spellings its toolchains use.
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define GYRO_OS_WINDOWS 1
#else
#define GYRO_OS_WINDOWS 0
#endif

/// Linux, where the backend is epoll.
#if defined(__linux__)
#define GYRO_OS_LINUX 1
#else
#define GYRO_OS_LINUX 0
#endif

/// macOS and the rest of Apple's platforms.
#if defined(__APPLE__)
#define GYRO_OS_DARWIN 1
#else
#define GYRO_OS_DARWIN 0
#endif

/**
 * @brief FreeBSD, OpenBSD, NetBSD and DragonFly.
 *
 * Deliberately not the `BSD` macro, which comes from `<sys/param.h>` and is
 * therefore absent in any file that has not included it. Each system is named
 * by the macro its own compiler defines, which needs no header at all.
 */
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#define GYRO_OS_BSD 1
#else
#define GYRO_OS_BSD 0
#endif

/**
 * @brief Everything that is not Windows.
 *
 * A simplification, and an honest one for gyro's purposes: the sources under
 * `posix/` are the ones that apply, and the question they are asking is only
 * ever whether this is Windows.
 */
#define GYRO_OS_POSIX (!GYRO_OS_WINDOWS)

#endif // !GYRO_PLATFORM_H_
