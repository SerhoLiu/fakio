client:
	RUST_BACKTRACE=1 cargo run --bin fakio-client

server:
	RUST_BACKTRACE=1 cargo run --bin fakio-server

release:
	cargo build --release

lint:
	cargo +nightly clippy

format:
	cargo +nightly fmt -- --write-mode diff || exit 0
	cargo +nightly fmt -- --write-mode overwrite || exit 0
