#!/usr/bin/env python3
"""Build the pinned, patched libsoup without changing the submodule or system."""
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
upstream = root / 'third_party/libsoup'
patch = root / 'patches/libsoup-async-chunk-close.patch'
build = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build'
os.environ.setdefault('CCACHE_DIR', str(build / 'ccache'))
revision = '2756d0076654b680d2021a08b9f2bd9d36d7ae1b'

def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)

if not (upstream / 'meson.build').exists():
    sys.exit('Initialize the submodule: git submodule update --init third_party/libsoup')
if subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip() != revision:
    sys.exit('Unexpected libsoup revision; restore the pinned submodule before building.')
run('git', '-C', str(upstream), 'diff', '--quiet', 'HEAD')
source = build / 'libsoup-src'
meson_build = build / 'libsoup-private-build'
prefix = build / 'libsoup-private'
stamp = source / '.qtspice-patch'
fingerprint = hashlib.sha256(patch.read_bytes() + revision.encode()).hexdigest()
if not stamp.exists() or stamp.read_text() != fingerprint:
    if source.exists():
        shutil.rmtree(source)
    shutil.copytree(upstream, source, ignore=shutil.ignore_patterns('.git'))
    with patch.open('rb') as changes:
        run('patch', '-p1', '--batch', '--forward', '-d', str(source), stdin=changes)
    stamp.write_text(fingerprint)
setup = ['meson', 'setup']
if (meson_build / 'meson-private/coredata.dat').exists():
    setup.append('--reconfigure')
run(*setup, str(meson_build), str(source), '--prefix', str(prefix), '--libdir', 'lib',
    '--wrap-mode=nodownload', '-Dtests=true', '-Ddocs=disabled', '-Dintrospection=disabled',
    '-Dvapi=disabled', '-Dsysprof=disabled', '-Dautobahn=disabled', '-Dpkcs11_tests=disabled')
run('meson', 'compile', '-C', str(meson_build), '-j', '6')
run('meson', 'test', '-C', str(meson_build), '--print-errorlogs', 'chunk-io-test')
run('meson', 'install', '-C', str(meson_build), '--no-rebuild')
print('Private libsoup installed to', prefix)
