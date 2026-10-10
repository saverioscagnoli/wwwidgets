mod audio;
mod bridge;
mod config;
mod ext;
mod navigation;
mod notifications;
mod reload;
mod tray;
mod util;
mod workspaces;

use std::cell::RefCell;
use std::collections::HashMap;
use std::ffi::OsString;
use std::path::Path;
use std::path::PathBuf;
use std::rc::Rc;

use gtk4::gdk;
use gtk4::gdk::prelude::DisplayExt;
use gtk4::gdk::prelude::MonitorExt;
use gtk4::gio;
use gtk4::gio::prelude::ApplicationCommandLineExt;
use gtk4::gio::prelude::ApplicationExt;
use gtk4::gio::prelude::ApplicationExtManual;
use gtk4::gio::prelude::ListModelExt;
use gtk4::gio::prelude::ListModelExtManual;
use gtk4::glib;
use gtk4::glib::GString;
use gtk4::glib::LogLevel;
use gtk4::glib::LogWriterOutput;
use gtk4::glib::object::CastNone;
use gtk4::glib::object::ObjectExt;
use gtk4::prelude::GtkApplicationExt;
use gtk4::prelude::GtkWindowExt;

use gtk4_layer_shell::LayerShell;

use traccia::Colored;
use traccia::debug;
use traccia::error;
use traccia::fatal;
use traccia::info;

use webkit6::prelude::WebViewExt;

use crate::audio::Audio;
use crate::config::Config;
use crate::config::MonitorPreset;
use crate::config::Monitors;
use crate::config::WidgetConfig;
use crate::ext::LayerWindowExt;
use crate::notifications::Notifications;
use crate::tray::Tray;

const APP_ID: &str = "dev.svscagn.wwwidgets";

#[derive(Default)]
pub struct Shared {
    pub state: RefCell<HashMap<String, String>>,
    pub views: RefCell<Vec<glib::WeakRef<webkit6::WebView>>>,
    pub notifications: Notifications,
    pub workspaces: RefCell<Option<workspaces::ExtWorkspaces>>,
    pub tray: Tray,
    pub audio: RefCell<Option<Audio>>,
}

impl Shared {
    pub fn set_state(
        &self,
        name: String,
        value: String,
        sender: Option<&webkit6::WebView>,
    ) -> Result<(), String> {
        if serde_json::from_str::<serde::de::IgnoredAny>(&value).is_err() {
            return Err("value is not valid JSON".into());
        }

        if self.state.borrow().get(&name) == Some(&value) {
            return Ok(());
        }

        let key = serde_json::to_string(&name).map_err(|e| e.to_string())?;
        let script = format!("window.__wwwidgets_state?.({key}, {value})");

        self.state.borrow_mut().insert(name, value);

        let targets = {
            let mut views = self.views.borrow_mut();

            views.retain(|w| w.upgrade().is_some());
            views.iter().filter_map(|w| w.upgrade()).collect::<Vec<_>>()
        };

        for view in targets.iter().filter(|v| Some(*v) != sender) {
            view.evaluate_javascript(&script, None, None, gio::Cancellable::NONE, |_| {});
        }

        Ok(())
    }
}

struct Formatter;

impl traccia::Formatter for Formatter {
    fn format(&self, record: &traccia::Record) -> String {
        let (level, color) = match record.level() {
            traccia::Level::Trace => ("TRCE", traccia::Color::Cyan),
            traccia::Level::Debug => ("DEBG", traccia::Color::Magenta),
            traccia::Level::Info => ("INFO", traccia::Color::Green),
            traccia::Level::Warn => ("WARN", traccia::Color::Yellow),
            traccia::Level::Error => ("ERRO", traccia::Color::Red),
        };

        format!("{} {}", level.foreground_color(color), record.args())
    }
}

fn route_glib_logs() {
    glib::log_set_writer_func(|level, fields| {
        let mut domain = None;
        let mut message = None;

        for field in fields {
            match field.key() {
                "GLIB_DOMAIN" => domain = field.value_str(),
                "MESSAGE" => message = field.value_str(),
                _ => {}
            }
        }

        let domain = domain.unwrap_or("glib");

        if domain.starts_with("Gdk") {
            return LogWriterOutput::Handled;
        }

        let message = message.unwrap_or("<no message>");

        match level {
            LogLevel::Error | LogLevel::Critical => traccia::error!("[{domain}] {message}"),
            LogLevel::Warning => traccia::warn!("[{domain}] {message}"),
            LogLevel::Message | LogLevel::Info => traccia::info!("[{domain}] {message}"),
            LogLevel::Debug => traccia::debug!("[{domain}] {message}"),
        }

        LogWriterOutput::Handled
    });
}

fn monitors_for(display: &gdk::Display, selection: &Monitors) -> Vec<gdk::Monitor> {
    let monitors = display.monitors();

    let all = monitors.iter::<gdk::Monitor>().filter_map(Result::ok);

    match selection {
        Monitors::Preset(MonitorPreset::All) => all.collect(),
        Monitors::List(names) => all
            .filter(|m| {
                m.connector()
                    .is_some_and(|c| names.iter().any(|n| n == c.as_str()))
            })
            .collect(),
    }
}

fn spawn_window(
    app: &gtk4::Application,
    webview: &webkit6::WebView,
    shared: Rc<Shared>,
    base: &Path,
    widget: &WidgetConfig,
    monitor: &gdk::Monitor,
) {
    let geometry = monitor.geometry();
    let mut builder = webkit6::WebView::builder()
        .related_view(webview)
        .user_content_manager(&webkit6::UserContentManager::new());

    if let Some(settings) = webview.settings() {
        builder = builder.settings(&settings);
    }

    let webview = builder.build();

    let uri = widget.resolve_uri(base);

    navigation::pin(&webview, &uri);

    if widget.bridge_enabled(&uri) {
        shared.views.borrow_mut().push(webview.downgrade());
        bridge::setup(&webview, shared, monitor.clone());
    } else {
        debug!("Bridge disabled for {uri}");
    }

    let width = match widget.width.resolve(geometry.width()) {
        Ok(w) => w,
        Err(e) => {
            error!("Failed to resolve width: {e}",);
            return;
        }
    };

    let height = match widget.height.resolve(geometry.height()) {
        Ok(h) => h,
        Err(e) => {
            error!("Failed to resolve height: {e}",);
            return;
        }
    };

    info!(
        "Spawning widget: {} ({}x{}) on {}",
        uri,
        width,
        height,
        monitor
            .connector()
            .unwrap_or(GString::from("Unnamed monitor"))
    );

    webview.load_uri(&uri);

    reload::watch(&webview, &uri);

    let window = gtk4::ApplicationWindow::builder()
        .application(app)
        .default_width(width)
        .default_height(height)
        .child(&webview)
        .build();

    window.init_layer_shell();
    window.set_monitor(Some(monitor));

    window.apply_namespace(&widget.namespace);
    window.apply_transparency(widget.transparent, &webview);
    window.apply_click_through(widget.click_through);
    window.apply_layer(widget.layer);
    window.apply_anchor(&widget.anchor);
    window.apply_margin(widget.margin);
    window.apply_exclusivity(widget.exclusive);
    window.apply_keyboard_mode(widget.keyboard);

    window.apply_visibility(widget.visible);

    let weak = window.downgrade();

    monitor.connect_invalidate(move |_| {
        if let Some(w) = weak.upgrade() {
            w.destroy();
        }
    });
}

enum Command {
    Run { config: Option<PathBuf> },
    Get { name: Option<String> },
    Set { name: String, value: String },
    Quit,
}

impl Command {
    pub fn parse(args: Vec<OsString>) -> Result<Self, lexopt::Error> {
        use lexopt::prelude::*;

        let mut parser = lexopt::Parser::from_args(args.into_iter().skip(1));
        let mut config = None;

        while let Some(arg) = parser.next()? {
            match arg {
                Short('c') | Long("config") => {
                    config = Some(parser.value()?.parse()?);
                }
                Value(cmd) => {
                    let cmd = cmd.string()?;
                    let rest = parser
                        .raw_args()?
                        .map(|a| a.string())
                        .collect::<Result<Vec<_>, _>>()?;

                    return match (cmd.as_str(), rest.as_slice()) {
                        ("get", []) => Ok(Self::Get { name: None }),
                        ("get", [name]) => Ok(Self::Get {
                            name: Some(name.clone()),
                        }),
                        ("set", [name, value]) => Ok(Self::Set {
                            name: name.clone(),
                            value: value.clone(),
                        }),
                        ("quit", []) => Ok(Self::Quit),
                        _ => Err(format!("invalid command: {cmd} {}", rest.join(" ")).into()),
                    };
                }

                _ => return Err(arg.unexpected()),
            }
        }

        Ok(Self::Run { config })
    }
}

fn start(app: &gtk4::Application, config: Config, shared: Rc<Shared>) {
    let Some(display) = gdk::Display::default() else {
        fatal!("No display available!");
    };

    let data_dir = glib::user_data_dir().join("wwwidgets");
    let cache_dir = glib::user_cache_dir().join("wwwidgets");

    debug!("Resolved data dir: {}", data_dir.display());
    debug!("Resolved cache dir: {}", cache_dir.display());

    let session = webkit6::NetworkSession::new(data_dir.to_str(), cache_dir.to_str());

    let mut pressure = webkit6::MemoryPressureSettings::new();

    pressure.set_memory_limit(150);
    pressure.set_strict_threshold(0.75);
    pressure.set_conservative_threshold(0.5);

    let ctx = webkit6::WebContext::builder()
        .memory_pressure_settings(&pressure)
        .build();

    ctx.set_cache_model(webkit6::CacheModel::DocumentViewer);

    let first = webkit6::WebView::builder()
        .network_session(&session)
        .web_context(&ctx)
        .build();

    if let Some(settings) = first.settings() {
        debug!("Devtools: {}", config.devtools);
        settings.set_enable_developer_extras(config.devtools);
        settings.set_allow_file_access_from_file_urls(true);
    }

    for widget in &config.widgets {
        for monitor in monitors_for(&display, &widget.monitors) {
            spawn_window(
                app,
                &first,
                Rc::clone(&shared),
                &config.dir,
                widget,
                &monitor,
            );
        }
    }

    let hold = app.hold();
    let app = app.clone();
    let widgets = config.widgets.clone();
    let dir = config.dir.clone();

    display
        .monitors()
        .connect_items_changed(move |list, position, _removed, added| {
            let _hold = &hold;
            for i in position..position + added {
                let Some(monitor) = list.item(i).and_downcast::<gdk::Monitor>() else {
                    continue;
                };

                for widget in &widgets {
                    if widget.wants(&monitor) {
                        spawn_window(&app, &first, Rc::clone(&shared), &dir, widget, &monitor);
                    }
                }
            }
        });
}

fn main() -> gtk4::glib::ExitCode {
    let _ = traccia::init(
        traccia::Config::default()
            .with_min_level(if cfg!(debug_assertions) {
                traccia::LevelFilter::Debug
            } else {
                traccia::LevelFilter::Info
            })
            .with_formatter(Formatter),
    );

    route_glib_logs();

    let shared = Rc::new(Shared::default());
    let app = gtk4::Application::builder()
        .application_id(APP_ID)
        .flags(gio::ApplicationFlags::HANDLES_COMMAND_LINE)
        .build();

    app.connect_command_line(move |app, cmdline| {
        let fail = |msg: &str| {
            cmdline.printerr_literal(&format!("{msg}\n"));
            glib::ExitCode::FAILURE
        };

        let command = match Command::parse(cmdline.arguments()) {
            Ok(c) => c,
            Err(e) => return fail(&e.to_string()),
        };

        if !cmdline.is_remote() && !matches!(command, Command::Run { .. }) {
            return fail("wwwidgets is not running");
        }

        match command {
            Command::Run { config } => {
                if !app.windows().is_empty() {
                    return fail("wwwidgets is already running");
                }

                let config = config.map(|p| cmdline.cwd().map(|d| d.join(&p)).unwrap_or(p));

                match Config::parse(config.as_ref()) {
                    Ok(config) => {
                        if config.notifications {
                            notifications::serve(Rc::clone(&shared));
                        }

                        if config.workspaces {
                            workspaces::serve(Rc::clone(&shared));
                        }

                        if config.tray {
                            tray::serve(Rc::clone(&shared));
                        }

                        if config.audio {
                            audio::serve(Rc::clone(&shared));
                        }

                        start(app, config, Rc::clone(&shared));
                    }
                    Err(e) => return fail(&e),
                }
            }
            Command::Get { name: Some(name) } => match shared.state.borrow().get(&name) {
                Some(value) => cmdline.print_literal(&format!("{value}\n")),
                None => return glib::ExitCode::FAILURE,
            },
            Command::Get { name: None } => {
                for (name, value) in shared.state.borrow().iter() {
                    cmdline.print_literal(&format!("{name} = {value}\n"));
                }
            }
            Command::Set { name, value } => {
                if let Err(e) = shared.set_state(name, value, None) {
                    return fail(&e);
                }
            }
            Command::Quit => app.quit(),
        }

        glib::ExitCode::SUCCESS
    });

    app.run()
}
