use std::cell::Cell;
use std::cell::RefCell;
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::path::PathBuf;
use std::rc::Rc;
use std::rc::Weak;
use std::time::Duration;

use gtk4::gio;
use gtk4::gio::prelude::FileExt;
use gtk4::gio::prelude::FileMonitorExt;
use gtk4::glib;
use gtk4::glib::object::ObjectExt;

use traccia::debug;
use traccia::warn;

use webkit6::prelude::WebViewExt;

const DEBOUNCE: Duration = Duration::from_millis(100);
const IGNORED: &[&str] = &["node_modules", "target"];

struct Watcher {
    webview: glib::WeakRef<webkit6::WebView>,
    monitors: RefCell<HashMap<PathBuf, gio::FileMonitor>>,
    pending: Cell<Option<glib::SourceId>>,
}

impl Watcher {
    fn schedule_reload(self: &Rc<Self>) {
        if let Some(id) = self.pending.take() {
            id.remove();
        }

        let weak = Rc::downgrade(self);

        self.pending.set(Some(glib::timeout_add_local_once(
            DEBOUNCE,
            move || {
                let Some(this) = weak.upgrade() else {
                    return;
                };

                this.pending.set(None);

                if let Some(webview) = this.webview.upgrade() {
                    debug!("Reloading {}", webview.uri().unwrap_or_default());
                    webview.reload();
                }
            },
        )));
    }

    fn unwatch(&self, path: &Path) {
        self.monitors.borrow_mut().retain(|p, monitor| {
            let keep = !p.starts_with(path);

            if !keep {
                monitor.cancel();
            }

            keep
        });
    }
}

fn is_ignored(path: &Path) -> bool {
    path.file_name()
        .and_then(|n| n.to_str())
        .is_some_and(|n| n.starts_with('.') || IGNORED.contains(&n))
}

fn watch_dir(watcher: &Rc<Watcher>, dir: &Path) {
    if is_ignored(dir) || watcher.monitors.borrow().contains_key(dir) {
        return;
    }

    let monitor = match gio::File::for_path(dir)
        .monitor_directory(gio::FileMonitorFlags::WATCH_MOVES, gio::Cancellable::NONE)
    {
        Ok(m) => m,
        Err(e) => {
            warn!("Can't watch {}: {e}", dir.display());
            return;
        }
    };

    let weak: Weak<Watcher> = Rc::downgrade(watcher);

    monitor.connect_changed(move |_, file, other, event| {
        use gio::FileMonitorEvent::*;

        let Some(watcher) = weak.upgrade() else {
            return;
        };

        let path = file.path();
        let target = other.and_then(|f| f.path()).or_else(|| path.clone());

        match event {
            Deleted | MovedOut => {
                if let Some(p) = &path {
                    watcher.unwatch(p);
                }
            }
            Renamed => {
                if let Some(p) = &path {
                    watcher.unwatch(p);
                }

                if let Some(t) = target.as_deref().filter(|t| t.is_dir()) {
                    watch_dir(&watcher, t);
                }
            }
            Created | MovedIn => {
                if let Some(t) = target.as_deref().filter(|t| t.is_dir()) {
                    watch_dir(&watcher, t);
                }
            }
            ChangesDoneHint => {}
            _ => return,
        }

        if target.as_deref().is_some_and(is_ignored) {
            return;
        }

        watcher.schedule_reload();
    });

    watcher
        .monitors
        .borrow_mut()
        .insert(dir.to_path_buf(), monitor);

    let Ok(entries) = fs::read_dir(dir) else {
        return;
    };

    for entry in entries.flatten() {
        if entry.file_type().is_ok_and(|t| t.is_dir()) {
            watch_dir(watcher, &entry.path());
        }
    }
}

pub fn watch(webview: &webkit6::WebView, uri: &str) {
    if glib::Uri::peek_scheme(uri).as_deref() != Some("file") {
        return;
    }

    let Some(root) = gio::File::for_uri(uri).parent().and_then(|f| f.path()) else {
        return;
    };

    let watcher = Rc::new(Watcher {
        webview: webview.downgrade(),
        monitors: RefCell::default(),
        pending: Cell::default(),
    });

    watch_dir(&watcher, &root);

    debug!(
        "Watching {} directories under {}",
        watcher.monitors.borrow().len(),
        root.display()
    );

    unsafe { webview.set_data("wwwidgets-reload", watcher) };
}
