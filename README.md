<div align="center">

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/gyro-lockup-horizontal-dark-1x.png">
    <img src=".github/gyro-lockup-horizontal-light-1x.png" alt="Gyro" width="340">
  </picture>
</p>

**A cross-platform asynchronous I/O runtime with a C API, built to be embedded.**

[![status: work in progress](https://img.shields.io/badge/status-work%20in%20progress-orange)](#project-status)
[![version: 0.1.0](https://img.shields.io/badge/version-0.1.0-blue)](CMakeLists.txt)
[![API: C11](https://img.shields.io/badge/API-C11-A8B9CC?logo=c)](gyro/include/gyro/)
[![implementation: C++17](https://img.shields.io/badge/implementation-C%2B%2B17-00599C?logo=cplusplus)](gyro/src/)
[![license: Apache 2.0](https://img.shields.io/badge/license-Apache--2.0-green)](LICENSE)
[![platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Linux-lightgrey)](#platform-support)

</div>

---

> **gyro is a work in progress.** The core is in place and tested, and the
> shape of the API has settled, but it is not yet a release: Windows support
> is still to come, several I/O primitives are planned rather than present,
> and signatures may still move. See [Project status](#project-status) before
> depending on it.

## What is gyro?

Gyro is an event loop and I/O runtime. You hand it operations, a read, a
write, a connection to make, a timer, and it carries them out without
blocking, reporting each one through a callback when it is done. Underneath it
speaks to whatever the operating system offers: `kqueue` on macOS and the
BSDs, `epoll` on Linux, and, soon, I/O completion ports on Windows. Above
that, the API is one and the same everywhere.

It was designed for the runtime of the [Orbit](https://github.com/orbitlang/orbit)
programming language, where callbacks resume fibers, and that origin shaped a
few of its choices: completion rather than readiness, a synchronous fast path
that costs nothing when it wins, and submits that work from any thread. None
of it is specific to Orbit. Gyro knows nothing about the program embedding it
and is meant to be dropped into any project that needs non-blocking I/O behind
a small, stable C interface.

The implementation is C++17, used with discipline and built with
`-fno-exceptions -fno-rtti`; nothing C++ crosses the binary boundary. The
public headers are plain C11 and stay includable from C and C++ alike.

## What sets it apart

- **Completion, not readiness.** You are told "these bytes were read into your
  buffer", never "the socket is readable". It is the model a suspended
  coroutine actually wants, and the one that maps onto every backend,
  completion ports included.
- **A fast path that is genuinely free.** A read whose data is already in the
  kernel buffer, or a write with room to go, completes on the calling thread
  with no allocation, no queue entry and no wakeup: one syscall and a return.
  It works off the loop's thread too, guarded by a per-direction claim rather
  than a lock.
- **Submit, cancel and close from any thread.** Everything else belongs to the
  loop's thread, and the headers say so, entry point by entry point. Work
  that arrives from elsewhere is handed to the loop over a lock-free queue and
  carried out there, in the order it was handed over.
- **Requests you never allocate, named by tokens that never dangle.** The
  loop owns every request from beginning to end. What you get back is a
  64-bit token carrying identity and time; cancelling an operation that has
  already finished is a success that does nothing, by construction.
- **One lock in the whole runtime**, on the request store, covering a free
  list pop and push and never held across a callback. The timer heap, the
  handle queues and the loop itself are lock-free and single-threaded.
- **No dependencies.** The standard library and the operating system.

## A taste

```c
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <gyro/gyro.h>

/* Buffers, and the descriptors naming them, must outlive the operation:
 * gyro hands them to the kernel rather than copying them. */
static char request[] = "GET / HTTP/1.0\r\n\r\n";
static char reply[1024];
static gyro_buf_t out = {request, sizeof(request) - 1};
static gyro_buf_t in = {reply, sizeof(reply)};

static gyro_cb_status_t on_read(gyro_handle_t *h, int status, size_t n, void *data) {
    if (status == GYRO_COMPLETED)
        printf("%.*s", (int) n, reply);
    else
        fprintf(stderr, "read: %s\n", gyro_strerror(status));

    gyro_handle_close(h, NULL);
    return GYRO_CB_SUCCESS;
}

static gyro_cb_status_t on_connect(gyro_handle_t *h, int status, size_t n, void *data) {
    gyro_tcp_t *tcp = (gyro_tcp_t *) h;

    if (status != GYRO_COMPLETED) {
        fprintf(stderr, "connect: %s\n", gyro_strerror(status));
        gyro_handle_close(h, NULL);
        return GYRO_CB_SUCCESS;
    }

    /* Completes on the spot when the socket has room, and calls back later
     * otherwise. The read waits up to five seconds for an answer. */
    gyro_tcp_write(tcp, &out, 1, 0, NULL, NULL, NULL, NULL);
    gyro_tcp_read(tcp, &in, 1, 5000, on_read, NULL, NULL, NULL);

    return GYRO_CB_SUCCESS;
}

int main(void) {
    gyro_t *loop = gyro_new(NULL);
    gyro_tcp_t *tcp = gyro_tcp_new(loop);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "93.184.215.14", &addr.sin_addr);

    int rc = gyro_tcp_connect(tcp, (struct sockaddr *) &addr, sizeof(addr), 5000, on_connect, NULL, NULL);
    if (rc == GYRO_COMPLETED)
        on_connect(GYRO_HANDLE(tcp), GYRO_COMPLETED, 0, NULL);
    else if (rc < 0)
        fprintf(stderr, "connect: %s\n", gyro_strerror(rc));

    gyro_run(loop); /* returns once nothing is left to do */
    gyro_free(loop);
    return 0;
}
```

Every submit returns one of three things: `GYRO_COMPLETED`, meaning it was
done on the spot and no callback will follow; `GYRO_PENDING`, meaning the loop
has it and will call back; or a negative status. A callback fires exactly once
per pending operation, on the loop's thread.

## Project status

gyro is **actively developed** and not yet released. The table is honest
about what is there today.

| Area | State |
|---|---|
| Event loop: run, stop, wakeup, timers heap | ✅ Working |
| Requests, tokens, cancellation from any thread | ✅ Working |
| Handles, close from any thread, ordered teardown | ✅ Working |
| TCP: bind, listen, accept, connect, read, write | ✅ Working |
| Timers: one-shot and periodic | ✅ Working |
| Synchronous fast path from any thread | ✅ Working |
| `kqueue` backend (macOS, BSD) | ✅ Working |
| `epoll` backend (Linux) | ✅ Working |
| I/O completion ports backend (Windows) | 🚧 Next. The API is shaped for it; the backend is not written |
| UDP, pipes, regular files | ❌ Planned |
| Name resolution | ❌ Planned, needs the thread pool |
| Thread pool for blocking work | ❌ Planned |
| Extension API for custom handle types | ❌ Designed, not started |
| API and ABI stability | ❌ Not yet. Signatures may still change before 1.0 |

The test suite currently runs 84 cases, including multi-threaded ones that
overlap submits, cancellations and closes with a running loop. It is run
under ThreadSanitizer on Linux as part of the routine.

### Platform support

| Platform | Backend | x86_64 | ARM64 |
|---|---|---|---|
| macOS | `kqueue` | ? | ✓ |
| Linux | `epoll` | ✓ | ✓ |
| FreeBSD, OpenBSD, NetBSD | `kqueue` | ? | ? |
| Windows | IOCP | 🚧 | 🚧 |

`✓` tested · `?` should work, untested · `🚧` in progress.

## Building

Requirements:

- A C++17 compiler (Clang or GCC; MSVC once the Windows backend lands)
- [CMake](https://cmake.org/) 3.20 or newer (3.21 for the presets)
- A build backend (Ninja or Make)

The repository ships CMake presets, which is the easiest way in:

```sh
cmake --preset debug
cmake --build build/debug
ctest --preset debug
```

Other presets: `release`, `asan` (AddressSanitizer and UndefinedBehaviorSanitizer),
`tsan` (ThreadSanitizer), and `tsan-clang` for a ThreadSanitizer build that
forces `clang++`. 

> Sanitizer builds are known not to start on recent macOS releases; run them on an x86_64 Linux host.

Without presets:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options:

| Option | Default | Meaning |
|---|---|---|
| `GYRO_BUILD_TESTS` | `ON` when top-level | Build the GoogleTest suite (fetched if not found) |
| `GYRO_BUILD_SHARED` | `OFF` | Build a shared library instead of a static one |
| `GYRO_INSTALL` | `ON` when top-level | Generate the install target and CMake package |
| `GYRO_WERROR` | `OFF` | Treat warnings as errors |

## Using gyro in your project

As a subdirectory:

```cmake
add_subdirectory(third_party/gyro)
target_link_libraries(myapp PRIVATE gyro::gyro)
```

Or installed and found as a package:

```cmake
find_package(gyro REQUIRED)
target_link_libraries(myapp PRIVATE gyro::gyro)
```

Then `#include <gyro/gyro.h>` for the whole API, or the individual headers
under [`gyro/include/gyro/`](gyro/include/gyro/) for less.

### Rules of the road

A few contracts hold everything together. They are stated in full in the
headers and in the design notes; these are the ones that bite.

- **Callbacks run on the loop's thread**, always, whichever thread submitted
  the operation. Wherever fibers are resumed, that is where.
- **Buffers stay valid and unmodified until the callback fires.** gyro hands
  them to the kernel rather than copying them, and so does the array of
  descriptors naming them. A moving garbage collector has to pin them.
- **Thread-safe**: submitting an operation, cancelling one, closing a handle,
  arming a timer, stopping the loop. **Loop-affine**: running the loop,
  binding, listening, connecting, and inspecting a handle. Every entry point
  says which it is in its documentation.
- **You own handles until their close callback runs.** gyro frees them then
  and never before. There is no "close everything": whoever embeds gyro keeps
  track of what it opened.
- **Closing a handle another thread is submitting on at that very moment is
  undefined.** It fails cleanly rather than corrupting memory, but it is still
  a race in the caller.
- **Shutting down** means closing what is open, letting `gyro_run()` return
  `GYRO_COMPLETED`, then `gyro_free()`. A loop with work in it refuses to be
  freed.

## Documentation

- The headers under [`gyro/include/gyro/`](gyro/include/gyro/) are the
  reference: every entry point documents its contract, its threading rule and
  the reasons behind them.

## Contributing

Gyro is young and help is welcome, whether that is a bug report, a platform
you can test on, a review of the design notes, or code. The Windows backend
is the largest piece of open work and the one most in need of hands; UDP,
pipes and the thread pool follow.

Open an issue to discuss anything that changes the API or the model before
writing it. For everything else, a pull request with a test is the best
opening line. The test suite is the contract: a change that breaks it is
wrong until the suite is updated to say otherwise.

## License

Gyro is licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).
