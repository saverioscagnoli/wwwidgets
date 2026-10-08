use std::path::Path;
use std::path::PathBuf;

use gtk4::glib;

pub fn expand_tilde<P>(path: P) -> PathBuf
where
    P: AsRef<Path>,
{
    let path = path.as_ref();

    match path.strip_prefix("~") {
        Ok(rest) => glib::home_dir().join(rest),
        Err(_) => path.to_path_buf(),
    }
}
