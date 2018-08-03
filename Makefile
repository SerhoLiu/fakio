client:
	RUST_BACKTRACE=1 cargo run --bin fakio-client

server:
	RUST_BACKTRACE=1 cargo run --bin fakio-server

release:
	cargo build --release

clippy:
	cargo +nightly clippy -- -A many_single_char_names

format:
	cargo +nightly fmt -- --check || exit 0
	cargo +nightly fmt || exit 0
