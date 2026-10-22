# Beaver Surf

Test the rules:

```sh
cargo test --manifest-path sim/Cargo.toml
```

Rebuild the pictures and sounds, then both ROMs. The Game Boy Advance build has to be started from `gba/` so its toolchain config is used. The Game Boy Color build needs GBDK-2020 4.5.0 in `~/gbdk`.

```sh
cargo run --release --manifest-path tools/gen_assets/Cargo.toml
cd gba
cargo build --release
agb-gbafix target/thumbv4t-none-eabi/release/beaver-gba -o ../web/beaver.gba
cd ..
python3 tools/check_header.py web/beaver.gba

cd gbc
make
cd ..
python3 tools/check_header.py web/beaver.gbc
./tools/check_gbc.sh
```

Play it in a browser, at three times the Game Boy Advance resolution:

```sh
python3 -m http.server -d web 8080
```

Arrows steer. Z or Space jumps. Enter starts.

`rust-toolchain.toml` pins the nightly that agb 0.25 builds with.
