client:
	RUST_BACKTRACE=1 cargo run --bin fakio-client

server:
	RUST_BACKTRACE=1 cargo run --bin fakio-server

format:
	rustup run nightly cargo fmt -- --write-mode diff || exit 0
	rustup run nightly cargo fmt -- --write-mode overwrite || exit 0