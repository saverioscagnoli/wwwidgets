mod bridge;
mod config;
mod ext;
mod navigation;
mod util;

use gtk4::gdk;
use gtk4::gdk::prelude::DisplayExt;
use gtk4::gdk::prelude::MonitorExt;
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

use crate::config::Config;
use crate::config::MonitorPreset;
use crate::config::Monitors;
use crate::config::WidgetConfig;
use crate::ext::LayerWindowExt;

const APP_ID: &str = "dev.svscagn.wwwidgets";

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

    let uri = widget.resolve_uri();

    navigation::pin(&webview, &uri);

    if widget.bridge_enabled(&uri) {
        bridge::setup(&webview);
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

    debug!("Parsing config...");

    let config = match Config::parse() {
        Ok(c) => c,
        Err(e) => fatal!("{e}"),
    };

    let app = gtk4::Application::builder().application_id(APP_ID).build();

    app.connect_activate(move |app| {
        if !app.windows().is_empty() {
            debug!("App was launched a second time. Skipping.");
            return;
        }

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
        }

        for widget in &config.widgets {
            for monitor in monitors_for(&display, &widget.monitors) {
                spawn_window(app, &first, widget, &monitor);
            }
        }

        let hold = app.hold();
        let app = app.clone();
        let widgets = config.widgets.clone();

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
                            spawn_window(&app, &first, widget, &monitor);
                        }
                    }
                }
            });
    });

    // Don't let gtk try to parse cli args
    app.run_with_args::<&str>(&[])
}
