use std::cell::Cell;
use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::OsStr;
use std::rc::Rc;

use gtk4::gio;
use gtk4::gio::prelude::DataInputStreamExtManual;
use gtk4::glib;
use gtk4::glib::object::ObjectExt;
use serde::Deserialize;

use traccia::error;

use webkit6::javascriptcore as jsc;
use webkit6::prelude::WebViewExt;

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

                for (_, proc) in processes.borrow_mut().drain() {
                    proc.force_exit();
                }
            }
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
            Err(e) => reply.return_error_message(&format!("invalid message: {e}")),
        }

        true
    });
}
