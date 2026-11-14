## Fakio

[![CI](https://github.com/SerhoLiu/fakio/actions/workflows/ci.yml/badge.svg)](https://github.com/SerhoLiu/fakio/actions/workflows/ci.yml)


Protect your data transmission security.

## CI and releases

GitHub Actions builds and tests every branch push and pull request. Stable Rust
covers the following targets:

- `x86_64-unknown-linux-gnu`
- `x86_64-unknown-linux-musl`
- `x86_64-apple-darwin` (Intel Mac)
- `aarch64-apple-darwin` (Apple Silicon Mac)
- `x86_64-pc-windows-gnu`
- `x86_64-pc-windows-msvc`

Linux GNU also runs beta and nightly checks; nightly failures are allowed.
Pushing a `v*` tag builds release binaries for all six stable targets and uploads
them to a GitHub Release using the built-in `GITHUB_TOKEN`.

## License

MIT LICENSE, see MIT-LICENSE.txt

## Warning

IGNORANCE IS STRENGTH
