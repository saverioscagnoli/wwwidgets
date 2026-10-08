use std::cell::Cell;
use std::fs;
use std::io;
use std::path::Path;
use std::path::PathBuf;
use std::sync::LazyLock;

use gtk4::cairo;
use gtk4::gdk;
use gtk4::gdk::prelude::MonitorExt;
use gtk4::gdk::prelude::SurfaceExt;
use gtk4::gio;
use gtk4::gio::prelude::FileExt;
use gtk4::glib;

use gtk4::glib::object::IsA;
use gtk4::prelude::GtkWindowExt;
use gtk4::prelude::NativeExt;
use gtk4::prelude::WidgetExt;
use gtk4_layer_shell::Edge;
use gtk4_layer_shell::LayerShell;
use serde::Deserialize;
use traccia::debug;
use traccia::info;
use webkit6::prelude::WebViewExt;

// ~/.config/wwwidgets
thread_local! {
    static TRANSPARENCY_CSS_LOADED: Cell<bool> = const { Cell::new(false) };
}

static CONFIG_DIR: LazyLock<PathBuf> = LazyLock::new(|| glib::user_config_dir().join("wwwidgets"));

// ~/.config/wwwidgets/config.json
// ~/.config/wwwidgets/config.jsonc
// ~/.config/wwwidgets/config.json5
static POSSIBLE_CONFIG_PATHS: LazyLock<Vec<PathBuf>> = LazyLock::new(|| {
    vec![
        CONFIG_DIR.join("config.json"),
        CONFIG_DIR.join("config.jsonc"),
        CONFIG_DIR.join("config.json5"),
    ]
});

#[derive(Deserialize)]
#[derive(Debug, Clone)]
#[serde(rename_all = "lowercase")]
pub enum MonitorPreset {
    All,
    Primary,
}

#[derive(Deserialize)]
#[derive(Debug, Clone)]
#[serde(untagged)]
pub enum Monitors {
    Preset(MonitorPreset),
    List(Vec<String>),
}

impl Default for Monitors {
    fn default() -> Self {
        Self::Preset(MonitorPreset::All)
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone)]
#[serde(untagged)]
pub enum Size {
    Px(u32),
    Percent(String),
}

impl Default for Size {
    fn default() -> Self {
        Self::Percent(String::from("30%"))
    }
}

impl Size {
    pub fn resolve(&self, total: i32) -> Result<i32, String> {
        match self {
            Self::Px(px) => Ok(*px as i32),
            Self::Percent(s) => s
                .trim_end_matches('%')
                .parse::<f64>()
                .map(|p| (total as f64 * p / 100.0) as i32)
                .map_err(|e| e.to_string()),
        }
    }
}

#[derive(Default)]
#[derive(Deserialize)]
#[derive(Debug, Clone)]
#[serde(rename_all = "lowercase")]
pub enum Layer {
    Background,
    Bottom,
    #[default]
    Top,
    Overlay,
}

impl Layer {
    pub fn to_gtk(&self) -> gtk4_layer_shell::Layer {
        match self {
            Self::Background => gtk4_layer_shell::Layer::Background,
            Self::Bottom => gtk4_layer_shell::Layer::Bottom,
            Self::Top => gtk4_layer_shell::Layer::Top,
            Self::Overlay => gtk4_layer_shell::Layer::Overlay,
        }
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone, Copy)]
#[serde(rename_all = "lowercase")]
pub enum AnchorPreset {
    Top,
    Bottom,
    Left,
    Right,
}

impl AnchorPreset {
    pub fn to_gtk(&self) -> Edge {
        match self {
            Self::Top => Edge::Top,
            Self::Bottom => Edge::Bottom,
            Self::Left => Edge::Left,
            Self::Right => Edge::Right,
        }
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone, Copy)]
#[serde(rename_all = "kebab-case")]
pub enum AnchorShorthand {
    Center,
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
}

impl AnchorShorthand {
    pub fn edges(&self) -> &'static [Edge] {
        match self {
            Self::Center => &[],
            Self::Top => &[Edge::Top],
            Self::Bottom => &[Edge::Bottom],
            Self::Left => &[Edge::Left],
            Self::Right => &[Edge::Right],
            Self::TopLeft => &[Edge::Top, Edge::Left],
            Self::TopRight => &[Edge::Top, Edge::Right],
            Self::BottomLeft => &[Edge::Bottom, Edge::Left],
            Self::BottomRight => &[Edge::Bottom, Edge::Right],
        }
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone)]
#[serde(untagged)]
pub enum Anchor {
    Presets(Vec<AnchorPreset>),
    Shorthand(AnchorShorthand),
}

impl Default for Anchor {
    fn default() -> Self {
        Self::Shorthand(AnchorShorthand::Center)
    }
}

impl Anchor {
    pub fn edges(&self) -> Vec<Edge> {
        match self {
            Self::Presets(presets) => presets.iter().map(AnchorPreset::to_gtk).collect(),
            Self::Shorthand(shorthand) => shorthand.edges().to_vec(),
        }
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone, Copy)]
pub struct NamedMargin {
    #[serde(default)]
    pub top: u32,

    #[serde(default)]
    pub right: u32,

    #[serde(default)]
    pub bottom: u32,

    #[serde(default)]
    pub left: u32,
}

#[derive(Deserialize)]
#[derive(Debug, Clone, Copy)]
#[serde(untagged)]
pub enum Margin {
    Number(u32),
    Named(NamedMargin),
}

impl Default for Margin {
    fn default() -> Self {
        Self::Number(0)
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone, Copy)]
#[serde(untagged)]
pub enum Exclusivity {
    Bool(bool),
    Number(i32),
}

impl Default for Exclusivity {
    fn default() -> Self {
        Self::Bool(false)
    }
}

#[derive(Default)]
#[derive(Deserialize)]
#[derive(Debug, Clone, Copy)]
#[serde(rename_all = "kebab-case")]
pub enum KeyboardMode {
    #[default]
    None,
    OnDemand,
    Exclusive,
}

#[derive(Deserialize)]
#[derive(Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct WidgetConfig {
    pub path: String,

    #[serde(default = "WidgetConfig::default_namespace")]
    pub namespace: String,

    #[serde(default)]
    pub monitors: Monitors,

    #[serde(default)]
    pub width: Size,

    #[serde(default)]
    pub height: Size,

    #[serde(default = "WidgetConfig::default_visible")]
    pub visible: bool,

    #[serde(default = "WidgetConfig::default_transparent")]
    pub transparent: bool,

    #[serde(
        rename = "click-through",
        default = "WidgetConfig::default_click_through"
    )]
    pub click_through: bool,

    #[serde(default)]
    pub layer: Layer,

    #[serde(default)]
    pub anchor: Anchor,

    #[serde(default)]
    pub margin: Margin,

    #[serde(default)]
    pub exclusive: Exclusivity,

    #[serde(default)]
    pub keyboard: KeyboardMode,

    #[serde(default)]
    pub bridge: Option<bool>,
}

impl WidgetConfig {
    fn default_namespace() -> String {
        String::from("wwwidgets")
    }

    const fn default_visible() -> bool {
        true
    }

    const fn default_transparent() -> bool {
        false
    }

    const fn default_click_through() -> bool {
        false
    }

    fn expand_tilde(path: &Path) -> PathBuf {
        match path.strip_prefix("~") {
            Ok(rest) => glib::home_dir().join(rest),
            Err(_) => path.to_path_buf(),
        }
    }

    pub fn resolve_uri(&self) -> String {
        if glib::Uri::peek_scheme(&self.path).is_some() {
            return self.path.clone();
        }

        let path = CONFIG_DIR.join(Self::expand_tilde(Path::new(&self.path)));
        let path = if path.is_dir() {
            path.join("index.html")
        } else {
            path
        };

        gio::File::for_path(path).uri().into()
    }

    pub fn apply_namespace<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        debug!("Setting namespace to '{}'", self.namespace);
        window.set_namespace(Some(&self.namespace));
    }

    pub fn apply_visibility<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        debug!("Setting visibility to {}", self.visible);
        if self.visible {
            window.present();
        }
    }

    pub fn apply_transparency<W>(&self, window: &W, webview: &webkit6::WebView)
    where
        W: IsA<gtk4::Window>,
    {
        if !self.transparent {
            return;
        }

        debug!("Making window transparent");

        let window: &gtk4::Window = window.as_ref();

        if !TRANSPARENCY_CSS_LOADED.replace(true) {
            let provider = gtk4::CssProvider::new();
            provider.load_from_string("window.wwwidgets-transparent { background: transparent; }");

            gtk4::style_context_add_provider_for_display(
                &window.display(),
                &provider,
                gtk4::STYLE_PROVIDER_PRIORITY_APPLICATION,
            );
        }

        window.add_css_class("wwwidgets-transparent");
        webview.set_background_color(&gdk::RGBA::TRANSPARENT);
    }

    pub fn apply_click_through<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        if !self.click_through {
            return;
        }

        let window: &gtk4::Window = window.as_ref();

        window.connect_realize(|window| {
            if let Some(surface) = window.surface() {
                surface.set_input_region(Some(&cairo::Region::create()));
            }
        });
    }

    pub fn apply_layer<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        debug!("Setting layer to {:?}", self.layer);
        window.set_layer(self.layer.to_gtk());
    }

    pub fn apply_anchor<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        for edge in self.anchor.edges() {
            debug!("Setting anchor {:?}", edge);
            window.set_anchor(edge, true);
        }
    }

    pub fn apply_margin<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        match self.margin {
            Margin::Named(m) => {
                debug!("Setting margin {:?}", m);
                window.set_margin(Edge::Top, m.top as i32);
                window.set_margin(Edge::Right, m.right as i32);
                window.set_margin(Edge::Bottom, m.bottom as i32);
                window.set_margin(Edge::Left, m.left as i32);
            }
            Margin::Number(n) => {
                let n = n as i32;

                debug!("Setting margin to {}", n);
                window.set_margin(Edge::Top, n);
                window.set_margin(Edge::Right, n);
                window.set_margin(Edge::Bottom, n);
                window.set_margin(Edge::Left, n);
            }
        }
    }

    pub fn apply_exclusivity<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        debug!("Setting exclusivity to {:?}", self.exclusive);
        match self.exclusive {
            Exclusivity::Number(n) => window.set_exclusive_zone(n),
            Exclusivity::Bool(true) => window.auto_exclusive_zone_enable(),
            Exclusivity::Bool(false) => {}
        }
    }

    pub fn apply_keyboard_mode<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        debug!("Setting keyboard mode to {:?}", self.keyboard);
        match self.keyboard {
            KeyboardMode::None => window.set_keyboard_mode(gtk4_layer_shell::KeyboardMode::None),
            KeyboardMode::OnDemand => {
                window.set_keyboard_mode(gtk4_layer_shell::KeyboardMode::OnDemand)
            }
            KeyboardMode::Exclusive => {
                window.set_keyboard_mode(gtk4_layer_shell::KeyboardMode::Exclusive)
            }
        }
    }

    pub fn bridge_enabled(&self, uri: &str) -> bool {
        self.bridge
            .unwrap_or_else(|| glib::Uri::peek_scheme(uri).is_some_and(|s| s == "file"))
    }

    pub fn wants(&self, monitor: &gdk::Monitor) -> bool {
        match &self.monitors {
            Monitors::Preset(MonitorPreset::All) => true,
            Monitors::Preset(MonitorPreset::Primary) => false,
            Monitors::List(names) => monitor
                .connector()
                .is_some_and(|c| names.iter().any(|n| n == c.as_str())),
        }
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct Config {
    #[serde(default = "Config::default_devtools")]
    pub devtools: bool,
    pub widgets: Vec<WidgetConfig>,
}

impl Config {
    const fn default_devtools() -> bool {
        cfg!(debug_assertions)
    }

    pub fn parse() -> Result<Self, String> {
        for path in POSSIBLE_CONFIG_PATHS.iter() {
            debug!("Trying path: {}", path.display());

            let content = match fs::read_to_string(path) {
                Ok(s) => s,
                Err(e) if e.kind() == io::ErrorKind::NotFound => continue,
                Err(e) => return Err(format!("failed to read {}: {e}", path.display())),
            };

            info!("Using config file at: {}", path.display());
            return json5::from_str(&content).map_err(|e| format!("Invalid config: {e}"));
        }

        Err("Tried all possible paths, but no config file was found.".into())
    }
}
