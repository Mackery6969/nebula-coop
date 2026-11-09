//! Files many mods add to at once: each mod brings only what it adds, in a layer's `merge/` folder, and the cooked file
//! gets all of it. Works on cooked files, as the game's own store holds them.
//!
//! - `<table>.json`: rows for a table: `{ "rows": [ { "column": value, ... } ] }`. A column left out is zero or "".
//!   Tables merge in cook/table.rs (TableIr).
//! - `Particles.jpc`, with `ParticleNames.bcsv.json` beside it: new particles. Each resource's user index in the file
//!   is its place in that list of names.

use anyhow::Result;

/// A cooked particle file with `extra`'s particles after its own, their user indices from `first_index`, their
/// textures after its own (super::particles, on their records).
pub fn add_particles(base: &[u8], extra: &[u8], first_index: u16) -> Result<Vec<u8>> {
    if !base.starts_with(b"JPAc2-10") || !extra.starts_with(b"JPAc2-10") {
        bail!("not a cooked particle file");
    }
    let base_resources = le16(base, 8)?;
    let base_textures = le16(base, 0xA)?;
    let base_texture_at = le32(base, 0xC)? as usize;
    let extra_resources = le16(extra, 8)?;
    let extra_textures = le16(extra, 0xA)?;
    let extra_texture_at = le32(extra, 0xC)? as usize;
    let base_end = walk_resources(base, |_, _| Ok(()))?;

    let mut moved = extra.to_vec();
    let extra_end = {
        let mut starts = Vec::new();
        let end = walk_resources(extra, |start, places| {
            starts.push((start, places.to_vec()));
            Ok(())
        })?;
        for (start, places) in starts {
            let index = le16(extra, start)?;
            let new_index = first_index.checked_add(index).context("too many particles")?;
            moved[start..start + 2].copy_from_slice(&new_index.to_le_bytes());
            for (at, size) in places {
                if &extra[at..at + 4] == b"TDB1" {
                    for place in (at + 8..at + (size & !1)).step_by(2) {
                        let texture = le16(extra, place)?;
                        let new = texture.checked_add(base_textures).context("too many particle textures")?;
                        moved[place..place + 2].copy_from_slice(&new.to_le_bytes());
                    }
                }
            }
        }
        end
    };

    let mut out = base[..base_end].to_vec();
    out.extend_from_slice(&moved[0x10..extra_end]);
    while out.len() % 32 != 0 {
        out.push(0);
    }
    let texture_at = out.len() as u32;
    out.extend_from_slice(base.get(base_texture_at..).context("particle textures past the file's end")?);
    out.extend_from_slice(moved.get(extra_texture_at..).context("particle textures past the file's end")?);
    out[8..10].copy_from_slice(&(base_resources.checked_add(extra_resources).context("too many particles")?).to_le_bytes());
    out[0xA..0xC].copy_from_slice(&(base_textures.checked_add(extra_textures).context("too many particle textures")?).to_le_bytes());
    out[0xC..0x10].copy_from_slice(&texture_at.to_le_bytes());
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A cooked table: an id (u32), a name (a word).
    fn table() -> Vec<u8> {
        let mut t = Vec::new();
        for v in [1u32, 2, 40, 8] {
            t.extend_from_slice(&v.to_le_bytes());
        }
        t.extend_from_slice(&column_hash("id").to_le_bytes());
        t.extend_from_slice(&u32::MAX.to_le_bytes());
        t.extend_from_slice(&[0, 0, 0, 0]);
        t.extend_from_slice(&column_hash("name").to_le_bytes());
        t.extend_from_slice(&u32::MAX.to_le_bytes());
        t.extend_from_slice(&[4, 0, 0, 6]);
        t.extend_from_slice(&7u32.to_le_bytes());
        t.extend_from_slice(&0u32.to_le_bytes());
        t.extend_from_slice(b"old\0");
        t
    }

    fn word(table: &[u8], row: usize) -> String {
        let entries = le32(table, 0).unwrap() as usize;
        let pool = 40 + entries * 8;
        let at = pool + le32(table, 40 + row * 8 + 4).unwrap() as usize;
        let end = table[at..].iter().position(|b| *b == 0).unwrap();
        String::from_utf8(table[at..at + end].to_vec()).unwrap()
    }

    #[test]
    fn rows_go_after_the_tables_own_with_their_words() {
        let rows = read_rows(br#"{ "rows": [ { "id": 9, "name": "new" }, { "id": 10 } ] }"#).unwrap();
        let out = add_rows(&table(), &rows).unwrap();
        assert_eq!(row_count(&out).unwrap(), 3);
        assert_eq!(le32(&out, 40).unwrap(), 7);
        assert_eq!(word(&out, 0), "old");
        assert_eq!(le32(&out, 48).unwrap(), 9);
        assert_eq!(word(&out, 1), "new");
        assert_eq!(le32(&out, 56).unwrap(), 10);
        assert_eq!(word(&out, 2), "");
    }

    #[test]
    fn a_new_table_comes_from_json() {
        let text = br#"{ "columns": { "Empty": "string", "Size": "float" }, "rows": [ { "PartName": "A", "TotalStep": 240, "Size": 1 }, { "PartName": "B", "Size": 0.5 } ] }"#;
        let table = table_from_json(text).unwrap();
        let be = |at: usize| u32::from_be_bytes(table[at..at + 4].try_into().unwrap());
        assert_eq!(be(0), 2);
        assert_eq!(be(4), 4);
        assert_eq!(be(8), 16 + 48);
        assert_eq!(be(12), 16);
        assert_eq!(table.len() % 32, 0);
        // Empty, Size, then the rows' own in the order they came.
        assert_eq!(be(16), column_hash("Empty"));
        assert_eq!(table[16 + 11], 6);
        assert_eq!(table[28 + 11], 2);
        let data = 64;
        assert_eq!(f32::from_bits(be(data + 4)), 1.0);
        assert_eq!(be(data + 12), 240);
        assert_eq!(f32::from_bits(be(data + 16 + 4)), 0.5);
        assert!(table_from_json(br#"{ "rows": [ { "A": 1 }, { "A": "x" } ] }"#).is_err());
        assert!(table_from_json(br#"{ "rows": [ { "A": [] } ] }"#).is_err());
    }

    #[test]
    fn a_column_the_table_lacks_is_left_out() {
        // Another layer's table of the same name may have it: the row still counts, its other columns as given.
        let rows = read_rows(br#"{ "rows": [ { "nope": 1 } ] }"#).unwrap();
        let out = add_rows(&table(), &rows).unwrap();
        assert_eq!(u32::from_le_bytes(out[0..4].try_into().unwrap()), 2);
        assert!(out.len() >= table().len() + 8);
    }

    /// A cooked particle file of resources (user index, the texture it names) and textures (bytes).
    fn particles(resources: &[(u16, u16)], textures: &[u8]) -> Vec<u8> {
        let mut p = b"JPAc2-10".to_vec();
        p.extend_from_slice(&(resources.len() as u16).to_le_bytes());
        p.extend_from_slice(&(textures.len() as u16).to_le_bytes());
        p.extend_from_slice(&[0; 4]);
        for (index, texture) in resources {
            p.extend_from_slice(&index.to_le_bytes());
            p.extend_from_slice(&1u16.to_le_bytes());
            p.extend_from_slice(&[0; 4]);
            p.extend_from_slice(b"TDB1");
            p.extend_from_slice(&12u32.to_le_bytes());
            p.extend_from_slice(&texture.to_le_bytes());
            p.extend_from_slice(&[0; 2]);
        }
        while p.len() % 32 != 0 {
            p.push(0);
        }
        let at = p.len() as u32;
        p[0xC..0x10].copy_from_slice(&at.to_le_bytes());
        for t in textures {
            p.extend_from_slice(b"TEX1");
            p.extend_from_slice(&32u32.to_le_bytes());
            p.extend_from_slice(&[*t; 24]);
        }
        p
    }

    #[test]
    fn particles_go_after_the_files_own_with_their_textures() {
        let base = particles(&[(0, 0), (1, 1)], &[0xA, 0xB]);
        let extra = particles(&[(0, 0), (1, 0)], &[0xC]);
        let out = add_particles(&base, &extra, 100).unwrap();
        assert_eq!(le16(&out, 8).unwrap(), 4);
        assert_eq!(le16(&out, 0xA).unwrap(), 3);
        let mut seen = Vec::new();
        walk_resources(&out, |start, places| {
            seen.push((le16(&out, start)?, le16(&out, places[0].0 + 8)?));
            Ok(())
        })
        .unwrap();
        assert_eq!(seen, [(0, 0), (1, 1), (100, 2), (101, 2)]);
        let textures = le32(&out, 0xC).unwrap() as usize;
        assert_eq!(textures % 32, 0);
        assert_eq!(out[textures + 8], 0xA);
        assert_eq!(out[textures + 32 + 8], 0xB);
        assert_eq!(out[textures + 64 + 8], 0xC);
    }
}
