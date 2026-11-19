//! The sims share one seedable per-thread generator.
pub use crate::modules::rain::random::{random_range, random01};
#[cfg(test)]
pub use crate::modules::rain::random::seed;
