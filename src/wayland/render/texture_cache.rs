//! Least-recently-used cache of rasterised textures, keyed by what was rasterised.

use std::collections::HashMap;

use super::renderer::{Renderer, Texture};
use super::text::Raster;

pub const MAX_ENTRIES: usize = 512;

/// Generic over the texture so the eviction logic is testable without GL.
pub struct TextureCache<T = Texture> {
    entries: HashMap<String, (T, u64)>,
    clock: u64,
}

impl<T> Default for TextureCache<T> {
    fn default() -> Self {
        TextureCache { entries: HashMap::new(), clock: 0 }
    }
}

impl<T> TextureCache<T> {
    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// The cached entry, else the one `create` makes (not cached when `None`); `evicted` receives what falls out.
    pub fn get_with(&mut self, key: &str, create: impl FnOnce() -> Option<T>, mut evicted: impl FnMut(T)) -> Option<&T> {
        self.clock += 1;
        let now = self.clock;
        if !self.entries.contains_key(key) {
            let made = create()?;
            self.entries.insert(key.to_string(), (made, now));
            // O(n) scan per eviction over 512 entries; an ordered index if the cache grows.
            while self.entries.len() > MAX_ENTRIES {
                let Some(oldest) = self.entries.iter().min_by_key(|(_, (_, used))| *used).map(|(k, _)| k.clone()) else { break };
                if let Some((t, _)) = self.entries.remove(&oldest) {
                    evicted(t);
                }
            }
        }
        let entry = self.entries.get_mut(key)?;
        entry.1 = now;
        Some(&entry.0)
    }

    pub fn clear_with(&mut self, mut evicted: impl FnMut(T)) {
        for (_, (t, _)) in self.entries.drain() {
            evicted(t);
        }
    }
}

impl TextureCache<Texture> {
    pub fn get(&mut self, renderer: &Renderer, key: &str, rasterize: impl FnOnce() -> Raster) -> Option<&Texture> {
        self.get_with(
            key,
            || {
                let raster = rasterize();
                renderer.upload_rgba(raster.width, raster.height, raster.scale, &raster.rgba)
            },
            |t| renderer.delete_texture(t),
        )
    }

    pub fn clear(&mut self, renderer: &Renderer) {
        self.clear_with(|t| renderer.delete_texture(t));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn texture_cache_evicts_lru() {
        let mut cache: TextureCache<u32> = TextureCache::default();
        let mut made = 0;
        let mut evicted = Vec::new();
        for i in 0..MAX_ENTRIES as u32 {
            assert_eq!(cache.get_with(&format!("k{i}"), || { made += 1; Some(i) }, |t| evicted.push(t)), Some(&i));
        }
        assert_eq!(made, MAX_ENTRIES);
        assert_eq!(cache.get_with("k0", || panic!("cached"), |t| evicted.push(t)), Some(&0), "a hit never rasterises");
        assert!(evicted.is_empty());
        assert_eq!(cache.get_with("new", || Some(999), |t| evicted.push(t)), Some(&999));
        assert_eq!(evicted, [1], "the least recently used entry goes, not the one just read");
        assert_eq!(cache.len(), MAX_ENTRIES);
        assert_eq!(cache.get_with("empty", || None, |t| evicted.push(t)), None, "an empty raster is not cached");
        assert_eq!(cache.len(), MAX_ENTRIES);
        let mut cleared = Vec::new();
        cache.clear_with(|t| cleared.push(t));
        assert_eq!((cleared.len(), cache.len()), (MAX_ENTRIES, 0));
    }
}
