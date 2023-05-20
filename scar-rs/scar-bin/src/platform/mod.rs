#[cfg(unix)]
mod unix;
#[cfg(unix)]
pub use unix::cmd_create;

#[cfg(not(unix))]
mod generic;
#[cfg(not(unix))]
pub use generic::cmd_create;