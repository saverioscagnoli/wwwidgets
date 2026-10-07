use std::fs;
use std::io;
use std::path::Path;
use std::path::PathBuf;
use std::sync::LazyLock;

use gtk4::glib;

use gtk4::glib::object::IsA;
use gtk4::pango::BidiType::B;
use gtk4_layer_shell::Edge;
use gtk4_layer_shell::LayerShell;
use serde::Deserialize;
use traccia::debug;
use traccia::info;

// ~/.config/wwwidgets
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
    pub fn resolve(&self, total: i32) -> i32 {
        match self {
            Self::Px(px) => *px as i32,
            Self::Percent(s) => s
                .trim_end_matches('%')
                .parse::<f64>()
                .map(|p| (total as f64 * p / 100.0) as i32)
                .unwrap_or(0),
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
    top: u32,
    right: u32,
    bottom: u32,
    left: u32,
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

#[derive(Deserialize)]
#[derive(Debug, Clone)]
pub struct WidgetConfig {
    pub path: PathBuf,

    #[serde(default)]
    pub monitors: Monitors,

    #[serde(default)]
    pub width: Size,

    #[serde(default)]
    pub height: Size,

    #[serde(default)]
    pub layer: Layer,

    #[serde(default)]
    pub anchor: Anchor,

    #[serde(default)]
    pub margin: Margin,

    #[serde(default)]
    pub exclusive: Exclusivity,
}

impl WidgetConfig {
    fn expand_tilde(path: &Path) -> PathBuf {
        match path.strip_prefix("~") {
            Ok(rest) => glib::home_dir().join(rest),
            Err(_) => path.to_path_buf(),
        }
    }

    pub fn resolve_path(&self) -> PathBuf {
        let path = CONFIG_DIR.join(Self::expand_tilde(&self.path));

        if path.is_dir() {
            path.join("index.html")
        } else {
            path
        }
    }

    pub fn apply_layer<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        window.set_layer(self.layer.to_gtk());
    }

    pub fn apply_anchor<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        for edge in self.anchor.edges() {
            window.set_anchor(edge, true);
        }
    }

    pub fn apply_margin<W>(&self, window: &W)
    where
        W: IsA<gtk4::Window>,
    {
        match self.margin {
            Margin::Named(m) => {
                window.set_margin(Edge::Top, m.top as i32);
                window.set_margin(Edge::Right, m.right as i32);
                window.set_margin(Edge::Bottom, m.bottom as i32);
                window.set_margin(Edge::Left, m.left as i32);
            }
            Margin::Number(n) => {
                let n = n as i32;

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
        match self.exclusive {
            Exclusivity::Number(n) => window.set_exclusive_zone(n),
            Exclusivity::Bool(true) => window.auto_exclusive_zone_enable(),
            Exclusivity::Bool(false) => {}
        }
    }
}

#[derive(Deserialize)]
#[derive(Debug, Clone)]
pub struct Config {
    pub widgets: Vec<WidgetConfig>,
}

impl Config {
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
