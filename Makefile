client:
	RUST_BACKTRACE=1 cargo run --bin fakio-client

server:
	RUST_BACKTRACE=1 cargo run --bin fakio-server

release:
	cargo build --release

clippy:
	cargo clippy

format:
	cargo fmt -- --check || exit 0
	cargo fmt || exit 0
