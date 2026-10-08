use std::fs;
use std::io;
use std::path::Path;
use std::path::PathBuf;
use std::sync::LazyLock;

use gtk4::gdk;
use gtk4::gdk::prelude::MonitorExt;
use gtk4::gio;
use gtk4::gio::prelude::FileExt;
use gtk4::glib;

use gtk4_layer_shell::Edge;
use serde::Deserialize;
use traccia::debug;
use traccia::info;

use crate::util;

pub static CONFIG_DIR: LazyLock<PathBuf> =
    LazyLock::new(|| glib::user_config_dir().join("wwwidgets"));

// ~/.config/wwwidgets/config.json
// ~/.config/wwwidgets/config.jsonc
// ~/.config/wwwidgets/config.json5
pub static POSSIBLE_CONFIG_PATHS: LazyLock<Vec<PathBuf>> = LazyLock::new(|| {
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
#[derive(Debug, Clone, Copy)]
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
    pub top: i32,

    #[serde(default)]
    pub right: i32,

    #[serde(default)]
    pub bottom: i32,

    #[serde(default)]
    pub left: i32,
}

#[derive(Deserialize)]
#[derive(Debug, Clone, Copy)]
#[serde(untagged)]
pub enum Margin {
    Number(i32),
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

    pub fn resolve_uri(&self) -> String {
        if glib::Uri::peek_scheme(&self.path).is_some() {
            return self.path.clone();
        }

        let path = CONFIG_DIR.join(util::expand_tilde(Path::new(&self.path)));
        let path = if path.is_dir() {
            path.join("index.html")
        } else {
            path
        };

        gio::File::for_path(path).uri().into()
    }

    pub fn bridge_enabled(&self, uri: &str) -> bool {
        self.bridge
            .unwrap_or_else(|| glib::Uri::peek_scheme(uri).is_some_and(|s| s == "file"))
    }

    pub fn wants(&self, monitor: &gdk::Monitor) -> bool {
        match &self.monitors {
            Monitors::Preset(MonitorPreset::All) => true,
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
