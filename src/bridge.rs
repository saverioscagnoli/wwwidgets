use std::cell::Cell;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::OsStr;
use std::rc::Rc;

use gtk4::cairo;
use gtk4::gdk::prelude::SurfaceExt;
use gtk4::gio;
use gtk4::gio::prelude::DataInputStreamExtManual;
use gtk4::glib;
use gtk4::glib::object::CastNone;
use gtk4::glib::object::ObjectExt;
use gtk4::prelude::GtkWindowExt;
use gtk4::prelude::NativeExt;
use gtk4::prelude::WidgetExt;
use gtk4_layer_shell::Edge;
use gtk4_layer_shell::LayerShell;
use serde::Deserialize;

use traccia::debug;
use traccia::error;
use traccia::info;

use webkit6::javascriptcore as jsc;
use webkit6::prelude::WebViewExt;

use crate::config::Anchor;
use crate::config::Exclusivity;
use crate::config::KeyboardMode;
use crate::config::Layer;
use crate::config::Margin;

const SHIM: &str = include_str!("../shim.js");

type Processes = Rc<RefCell<HashMap<u32, gio::Subprocess>>>;
type Generation = Rc<Cell<u64>>;

#[derive(Clone)]
struct Target {
    webview: glib::WeakRef<webkit6::WebView>,
    generation: Generation,
    spawned_in: u64,
}

impl Target {
    fn is_current(&self) -> bool {
        self.generation.get() == self.spawned_in
    }
}

#[derive(Deserialize)]
#[serde(tag = "cmd", rename_all = "lowercase")]
enum Message {
    IsVisible,
    Hide,
    Show,
    SetSize { argv: (i32, i32) },
    SetWidth { argv: i32 },
    SetHeight { argv: i32 },
    SetKeyboard { argv: KeyboardMode },
    SetClickThrough { argv: bool },
    SetMargin { argv: Margin },
    SetAnchor { argv: Anchor },
    SetLayer { argv: Layer },
    SetExclusive { argv: Exclusivity },
    Exec { argv: Vec<String> },
    Spawn { id: u32, argv: Vec<String> },
    Kill { id: u32 },
}

fn exec(argv: Vec<String>, ctx: jsc::Context, reply: webkit6::ScriptMessageReply) {
    if argv.is_empty() {
        reply.return_error_message("exec: empty argv");
        return;
    }

    let args = argv.iter().map(|s| s.as_ref()).collect::<Vec<&OsStr>>();
    let flags = gio::SubprocessFlags::STDOUT_PIPE | gio::SubprocessFlags::STDERR_PIPE;

    let proc = match gio::Subprocess::newv(&args, flags) {
        Ok(p) => p,
        Err(e) => {
            reply.return_error_message(&e.to_string());
            return;
        }
    };

    glib::spawn_future_local(async move {
        match proc.communicate_utf8_future(None).await {
            Ok((stdout, stderr)) => {
                let code = if proc.has_exited() {
                    proc.exit_status()
                } else {
                    -1
                };

                let Some(obj) = ctx.evaluate("({})") else {
                    reply.return_error_message("exec: failed to create result object");
                    return;
                };

                obj.object_set_property("stdout", &jsc::Value::new_string(&ctx, stdout.as_deref()));
                obj.object_set_property("stderr", &jsc::Value::new_string(&ctx, stderr.as_deref()));
                obj.object_set_property("code", &jsc::Value::new_number(&ctx, code as f64));

                reply.return_value(&obj);
            }
            Err(e) => reply.return_error_message(&e.to_string()),
        }
    });
}

fn emit(target: &Target, id: u32, kind: &str, data: &str) {
    if !target.is_current() {
        return;
    }

    let Some(webview) = target.webview.upgrade() else {
        return;
    };

    let script = format!("window.__wwwidgets_emit({id}, \"{kind}\", {data})");
    webview.evaluate_javascript(&script, None, None, gio::Cancellable::NONE, |_| {});
}

async fn read_lines(stream: gio::InputStream, id: u32, kind: &'static str, target: Target) {
    let stream = gio::DataInputStream::new(&stream);

    while let Ok(Some(line)) = stream.read_line_utf8_future(glib::Priority::DEFAULT).await {
        let Ok(literal) = json5::to_string(&line.to_string()) else {
            continue;
        };

        emit(&target, id, kind, &literal);
    }
}

fn spawn(id: u32, argv: Vec<String>, target: Target, processes: Processes) -> Result<(), String> {
    if argv.is_empty() {
        return Err("spawn: empty argv".into());
    };

    let args = argv.iter().map(|s| s.as_ref()).collect::<Vec<&OsStr>>();
    let flags = gio::SubprocessFlags::STDOUT_PIPE | gio::SubprocessFlags::STDERR_PIPE;
    let proc = gio::Subprocess::newv(&args, flags).map_err(|e| e.to_string())?;

    processes.borrow_mut().insert(id, proc.clone());

    let stderr_task = proc
        .stderr_pipe()
        .map(|s| glib::spawn_future_local(read_lines(s, id, "stderr", target.clone())));

    glib::spawn_future_local(async move {
        if let Some(stdout) = proc.stdout_pipe() {
            read_lines(stdout, id, "stdout", target.clone()).await;
        }

        if let Some(task) = stderr_task {
            let _ = task.await;
        }

        let _ = proc.wait_future().await;

        if !target.is_current() {
            return;
        }

        processes.borrow_mut().remove(&id);

        let code = if proc.has_exited() {
            proc.exit_status()
        } else {
            -1
        };

        emit(&target, id, "exit", &code.to_string());
    });

    Ok(())
}

fn kill_all(processes: &Processes) {
    let mut procs = processes.borrow_mut();

    if !procs.is_empty() {
        debug!("Killing {} process(es)", procs.len());
    }

    for (_, proc) in procs.drain() {
        proc.force_exit();
    }
}

pub fn setup(webview: &webkit6::WebView) {
    let Some(ucm) = webview.user_content_manager() else {
        error!("Webview has no user content manager");
        return;
    };

    ucm.add_script(&webkit6::UserScript::new(
        SHIM,
        webkit6::UserContentInjectedFrames::TopFrame,
        webkit6::UserScriptInjectionTime::Start,
        &[],
        &[],
    ));

    let processes: Processes = Rc::default();
    let generation: Generation = Rc::default();

    {
        let processes = Rc::clone(&processes);
        let generation = Rc::clone(&generation);

        webview.connect_load_changed(move |_, event| {
            if event == webkit6::LoadEvent::Started {
                generation.set(generation.get() + 1);
                kill_all(&processes);
            }
        });
    }

    {
        let processes = Rc::clone(&processes);
        let generation = Rc::clone(&generation);

        webview.connect_destroy(move |_| {
            generation.set(generation.get() + 1);
            kill_all(&processes);
        });
    }

    let weak = webview.downgrade();

    ucm.register_script_message_handler_with_reply("wwwidgets", None);
    ucm.connect_script_message_with_reply_received(Some("wwwidgets"), move |_, value, reply| {
        let Some(ctx) = value.context() else {
            return false;
        };

        let msg = value
            .to_json(0)
            .ok_or_else(|| "message is not serializable".to_string())
            .and_then(|json| json5::from_str::<Message>(&json).map_err(|e| e.to_string()));

        match msg {
            Ok(Message::IsVisible) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    reply.return_value(&jsc::Value::new_boolean(&ctx, window.is_visible()));
                } else {
                    reply.return_value(&jsc::Value::new_boolean(&ctx, false));
                }
            }
            Ok(msg @ (Message::Hide | Message::Show)) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    let state = matches!(msg, Message::Show);
                    window.set_visible(state);

                    info!("Setting window visibility to {}", state);
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetSize { argv: (w, h) }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    window.set_default_size(w, h);
                    info!("Setting window size to {w}x{h}");
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetWidth { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    window.set_default_width(argv);
                    info!("Setting window width to {argv}");
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetHeight { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    window.set_default_height(argv);
                    info!("Setting window height to {argv}");
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetKeyboard { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    info!("Setting keyboard mode to {:?}", argv);
                    match argv {
                        KeyboardMode::None => {
                            window.set_keyboard_mode(gtk4_layer_shell::KeyboardMode::None)
                        }
                        KeyboardMode::OnDemand => {
                            window.set_keyboard_mode(gtk4_layer_shell::KeyboardMode::OnDemand)
                        }
                        KeyboardMode::Exclusive => {
                            window.set_keyboard_mode(gtk4_layer_shell::KeyboardMode::Exclusive)
                        }
                    }
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetClickThrough { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    info!("Setting window click through to {argv}");
                    if let Some(surface) = window.surface() {
                        let region = argv.then(cairo::Region::create);
                        surface.set_input_region(region.as_ref());
                    }
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetMargin { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    match argv {
                        Margin::Named(m) => {
                            info!("Setting window margin {:?}", m);
                            window.set_margin(Edge::Top, m.top as i32);
                            window.set_margin(Edge::Right, m.right as i32);
                            window.set_margin(Edge::Bottom, m.bottom as i32);
                            window.set_margin(Edge::Left, m.left as i32);
                        }
                        Margin::Number(n) => {
                            let n = n as i32;

                            info!("Setting margin to {}", n);
                            window.set_margin(Edge::Top, n);
                            window.set_margin(Edge::Right, n);
                            window.set_margin(Edge::Bottom, n);
                            window.set_margin(Edge::Left, n);
                        }
                    }
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetAnchor { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    window.set_anchor(Edge::Bottom, false);
                    window.set_anchor(Edge::Left, false);
                    window.set_anchor(Edge::Right, false);
                    window.set_anchor(Edge::Top, false);

                    for edge in argv.edges() {
                        info!("Setting anchor {:?}", edge);
                        window.set_anchor(edge, true);
                    }
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetLayer { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    info!("Setting layer to {:?}", argv);
                    window.set_layer(argv.to_gtk());
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::SetExclusive { argv }) => {
                if let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                {
                    info!("Setting exclusivity to {:?}", argv);
                    match argv {
                        Exclusivity::Number(n) => window.set_exclusive_zone(n),
                        Exclusivity::Bool(true) => window.auto_exclusive_zone_enable(),
                        Exclusivity::Bool(false) => window.set_exclusive_zone(0),
                    }
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Ok(Message::Exec { argv }) => exec(argv, ctx, reply.clone()),
            Ok(Message::Spawn { id, argv }) => {
                let target = Target {
                    webview: weak.clone(),
                    generation: Rc::clone(&generation),
                    spawned_in: generation.get(),
                };

                match spawn(id, argv, target, processes.clone()) {
                    Ok(()) => reply.return_value(&jsc::Value::new_undefined(&ctx)),
                    Err(e) => reply.return_error_message(&e),
                }
            }
            Ok(Message::Kill { id }) => {
                if let Some(proc) = processes.borrow_mut().remove(&id) {
                    proc.force_exit();
                }

                reply.return_value(&jsc::Value::new_undefined(&ctx));
            }
            Err(e) => reply.return_error_message(&format!("invalid message: {e}")),
        }

        true
    });
}
