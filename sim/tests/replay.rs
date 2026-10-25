//! Replays every golden trace in `fixtures/` and checks the rules reproduce it byte for byte.
//! Regenerate with `cargo run --release --manifest-path sim/Cargo.toml --bin dump_traces`.

use std::fs;
use std::path::Path;

use beaver_sim::trace::{STATE_LEN, trace_frames, trace_record};
use beaver_sim::{Input, World, decode_save, encode_save};

fn fixtures() -> std::path::PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../fixtures")
}

#[test]
fn every_trace_replays_exactly() {
    let mut traces: Vec<_> = fs::read_dir(fixtures())
        .expect("fixtures directory")
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.extension().is_some_and(|ext| ext == "trace"))
        .collect();
    traces.sort();
    assert_eq!(traces.len(), 9, "expected the nine scripted traces");

    for path in traces {
        let bytes = fs::read(&path).unwrap();
        let frames = trace_frames(&bytes).expect("trace header");
        let mut world = World::new();
        let mut state = [0u8; STATE_LEN];
        for index in 0..frames {
            let (bits, expected) = trace_record(&bytes, index);
            if index > 0 {
                world.step(Input::from_bits(bits));
            }
            world.encode_state(&mut state);
            assert!(
                state == expected,
                "{} diverges at record {index}",
                path.display()
            );
        }
    }
}

#[test]
fn the_save_fixtures_decode_as_written() {
    let good = fs::read(fixtures().join("save_best_123456.bin")).unwrap();
    let good: [u8; 8] = good.as_slice().try_into().unwrap();
    assert_eq!(decode_save(&good), Some(123_456));
    assert_eq!(encode_save(123_456), good);

    let damaged = fs::read(fixtures().join("save_damaged.bin")).unwrap();
    let damaged: [u8; 8] = damaged.as_slice().try_into().unwrap();
    assert_eq!(decode_save(&damaged), None);
}
