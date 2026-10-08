use std::cell::Cell;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::OsStr;
use std::rc::Rc;

use gtk4::gio;
use gtk4::gio::prelude::DataInputStreamExtManual;
use gtk4::glib;
use gtk4::glib::object::CastNone;
use gtk4::glib::object::ObjectExt;
use gtk4::prelude::GtkWindowExt;
use gtk4::prelude::WidgetExt;

use serde::Deserialize;

use traccia::debug;
use traccia::error;

use webkit6::javascriptcore as jsc;
use webkit6::prelude::WebViewExt;

use crate::config::Anchor;
use crate::config::Exclusivity;
use crate::config::KeyboardMode;
use crate::config::Layer;
use crate::config::Margin;
use crate::ext::LayerWindowExt;

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

/// Every message that applies some rule to the window
/// or needs some state from the window
/// (it needs to upgrade the weakref)
#[derive(Deserialize)]
#[serde(tag = "cmd", rename_all = "lowercase")]
enum WindowMessage {
    IsVisible,
    Hide,
    Show,
    Size,
    SetSize { argv: (i32, i32) },
    SetWidth { argv: i32 },
    SetHeight { argv: i32 },
    SetKeyboard { argv: KeyboardMode },
    SetClickThrough { argv: bool },
    SetMargin { argv: Margin },
    SetAnchor { argv: Anchor },
    SetLayer { argv: Layer },
    SetExclusive { argv: Exclusivity },
}

#[rustfmt::skip]
#[derive(Deserialize)]
#[serde(tag = "cmd", rename_all = "lowercase")]
enum Message {
    Exec { argv: Vec<String> },
    Spawn { id: u32, argv: Vec<String> },
    Kill { id: u32 },
    #[serde(untagged)]
    Window(WindowMessage),
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
            Ok(Message::Window(wmsg)) => {
                let Some(window) = weak
                    .upgrade()
                    .and_then(|wv| wv.root())
                    .and_downcast::<gtk4::Window>()
                else {
                    reply.return_error_message("Failed to get the window!");
                    return true;
                };

                match wmsg {
                    WindowMessage::IsVisible => {
                        reply.return_value(&jsc::Value::new_boolean(&ctx, window.is_visible()));
                    }
                    wmsg @ (WindowMessage::Hide | WindowMessage::Show) => {
                        window.set_visible(matches!(wmsg, WindowMessage::Show));
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::Size => {
                        reply.return_value(&jsc::Value::new_array_from_garray(
                            &ctx,
                            &[
                                jsc::Value::new_number(&ctx, window.width() as f64),
                                jsc::Value::new_number(&ctx, window.height() as f64),
                            ],
                        ));
                    }
                    WindowMessage::SetSize { argv: (w, h) } => {
                        window.set_default_size(w, h);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::SetWidth { argv } => {
                        window.set_default_width(argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::SetHeight { argv } => {
                        window.set_default_height(argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::SetKeyboard { argv } => {
                        window.apply_keyboard_mode(argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::SetClickThrough { argv } => {
                        window.apply_click_through(argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::SetMargin { argv } => {
                        window.apply_margin(argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::SetAnchor { argv } => {
                        window.apply_anchor(&argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                    WindowMessage::SetLayer { argv } => {
                        window.apply_layer(argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }

                    WindowMessage::SetExclusive { argv } => {
                        window.apply_exclusivity(argv);
                        reply.return_value(&jsc::Value::new_undefined(&ctx));
                    }
                }
            }
            Err(e) => reply.return_error_message(&format!("invalid message: {e}")),
        }

        true
    });
}
