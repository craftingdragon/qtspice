# Private libsoup WebDAV fix

The submodule pins official libsoup 3.8.1 (`2756d0076654b680d2021a08b9f2bd9d36d7ae1b`). The patch retains upstream copyright/license notices and is intended for upstream review; nothing has been submitted automatically.

`libsoup-async-chunk-close.patch` adds a main-context, pollable asynchronous close implementation to SoupBodyOutputStream. The inherited async implementation invokes synchronous close from a worker. libspice 0.42's main-context-only in-memory pipe can return WOULD_BLOCK during that close, leaving the final chunk as `0\r\n` rather than `0\r\n\r\n`. A full payload can therefore arrive while curl reports error 18 and KIO waits or retries.

The patch waits for writability and resumes the existing chunk state without duplicating framing. Its task owns the stream and keeps framing buffers alive until completion or cancellation. Errors propagate; a failed payload must not acquire a successful terminator. Synchronous close behavior remains unchanged.

The patch also adds three cases to upstream chunk-io-test: one-byte partial writes with repeated WOULD_BLOCK on a main-context-only stream; cancellation; and a broken-pipe error. The first fails on stock libsoup and passes with the patch. QtSpice's optional webdavtransfer_test verifies complete chunk framing and 64 MiB synthetic payloads through the native SPICE/WebDAV channel over TCP and TLS.

The build script patches a separate source copy under build/, never the submodule. Dependencies are resolved locally with Meson's nodownload mode; missing tools/libraries must be installed explicitly. No system libsoup is installed or overwritten. The runtime launcher scopes the private shared library to its command and child processes; use ordinary viewer commands to return to system libsoup.

The streaming test's synchronous skip client previously shared the server's main context. That fixture can deadlock when close correctly waits on a pollable source instead of a worker thread. The patch runs that test's server on its own thread; the client still exercises the original synchronous skip API. As with other asynchronous server I/O, the server's owning main context must keep running until close finishes. Non-chunked close delegates to the unchanged upstream implementation.
