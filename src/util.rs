use std::path::Path;
use std::path::PathBuf;

use gtk4::gdk;
use gtk4::gdk::prelude::DisplayExt;
use gtk4::gio::prelude::ListModelExtManual;
use gtk4::glib;

use serde::Serialize;

use webkit6::javascriptcore as jsc;

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

pub fn list_monitors() -> Vec<gdk::Monitor> {
    gdk::Display::default()
        .map(|d| d.monitors().iter::<gdk::Monitor>().flatten().collect())
        .unwrap_or_default()
}

pub fn reply_json<T>(ctx: &jsc::Context, reply: &webkit6::ScriptMessageReply, value: &T)
where
    T: Serialize,
{
    match serde_json::to_string(value) {
        Ok(json) => reply.return_value(&jsc::Value::from_json(ctx, &json)),
        Err(e) => reply.return_error_message(&e.to_string()),
    }
}
