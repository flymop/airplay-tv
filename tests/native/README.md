# Native audio regression tests

Requires the UxPlay submodule and C11/C++17 compilers; no Android SDK is needed.

```sh
bash tests/native/run_tests.sh
```

Tests cover bounded RTP resend waits, packet ordering, PCM timing resets, and
decoder/output lifecycle across FLUSH. Android and crypto dependencies are
stubbed; MediaCodec and Oboe still require device testing.

The runner applies the UxPlay patches in a temporary directory and removes test
artifacts on exit. It uses UndefinedBehaviorSanitizer by default; override `CC`,
`CXX`, or `SANITIZERS` as needed.
