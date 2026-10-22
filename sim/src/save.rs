//! Eight-byte cartridge record: magic, 24-bit best score, checksum.
//! The same bytes are what a later Game Boy Color port would store.

const MAGIC: [u8; 4] = *b"BVR1";

pub fn encode_save(best: u32) -> [u8; 8] {
    let best = best.min(0x00FF_FFFF);
    let mut bytes = [0u8; 8];
    bytes[0..4].copy_from_slice(&MAGIC);
    bytes[4] = (best & 0xFF) as u8;
    bytes[5] = ((best >> 8) & 0xFF) as u8;
    bytes[6] = ((best >> 16) & 0xFF) as u8;
    bytes[7] = checksum(&bytes);
    bytes
}

pub fn decode_save(bytes: &[u8; 8]) -> Option<u32> {
    if bytes[0..4] != MAGIC {
        return None;
    }
    if bytes[7] != checksum(bytes) {
        return None;
    }
    Some(u32::from(bytes[4]) | (u32::from(bytes[5]) << 8) | (u32::from(bytes[6]) << 16))
}

fn checksum(bytes: &[u8; 8]) -> u8 {
    bytes[..7]
        .iter()
        .fold(0u8, |acc, byte| acc.wrapping_add(*byte))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_scores() {
        for score in [0, 1, 42, 123_456, 0x00FF_FFFF] {
            let bytes = encode_save(score);
            assert_eq!(decode_save(&bytes), Some(score));
        }
    }

    #[test]
    fn flipped_checksum_byte_is_rejected() {
        let mut bytes = encode_save(500);
        bytes[7] ^= 0xFF;
        assert_eq!(decode_save(&bytes), None);
    }

    #[test]
    fn damaged_magic_is_rejected() {
        let mut bytes = encode_save(500);
        bytes[0] ^= 0x01;
        assert_eq!(decode_save(&bytes), None);
    }
}
