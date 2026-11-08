# Contributing

QtSpice is pre-1.0. Discuss substantial API changes in an issue before implementing
them. Keep changes focused and include meaningful Qt Test coverage. Use Qt types in
public interfaces and examples; native SPICE/GLib belongs in library internals.
Do not link GTK widget libraries. Examples use Qt keywords; the library keeps
QT_NO_KEYWORDS private. Follow the surrounding code style.

## Build and test

See README for dependencies and CMake options. Build with CMake and run CTest.
Tests use synthetic fixtures and local fake servers; no VM credentials are needed.
Real USB, audio, compositor capture, and guest behavior need separate manual tests.
The optional private-libsoup integration is described in docs/viewers.md.

## Containers and GitHub Actions locally

The workflow uses GitHub Actions job containers directly: Rocky Linux 9
(CRB/EPEL), Debian 13, and Arch Linux. Dependencies are installed by
`ci/install-deps.sh`; `ci/validate.sh` builds and tests the checkout as an
unprivileged user. No custom Docker images or Docker-in-Docker setup is required.

With Docker and act installed, run the actual workflow locally:

```sh
act push -W .github/workflows/ci.yml -j build --concurrent-jobs 1 \
  --artifact-server-path /tmp/qtspice-act-artifacts
```

Add `--matrix distro:rocky9` (or `debian13` / `arch`) to select one entry.
For rootless Docker, set DOCKER_HOST to the daemon socket URL. No GitHub token or
application secrets are required. The bootstrap step installs Git and Node.js
inside every job container before JavaScript actions run. act does not validate
GitHub permission enforcement or scheduled delivery. Container tests use the host
kernel and cannot validate physical devices or real compositor permissions.

## Reports and patches

Include the commit, distribution, Qt/SPICE versions, reproduction steps, expected
behavior, and relevant test results. Redact credentials, tickets, proxy passwords,
private endpoints, local paths, and clipboard contents. Never attach an unredacted
.vv file. Contributions are accepted under LGPL-3.0-or-later; retain third-party
notices and identify any imported code and its license. No CLA is required.

Rocky 9 CI builds checksum-pinned spice-gtk 0.42 (GTK disabled) and phodav 2.5
under /opt/qtspice-native because the native SPICE client is absent from its
enabled repositories. Qt and FFmpeg remain distribution packages. The setup script
records source URLs, checksums, and build options; Debian and Arch use packaged SPICE.
