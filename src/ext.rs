use std::cell::Cell;

use gtk4::cairo;
use gtk4::gdk;
use gtk4::gdk::prelude::SurfaceExt;
use gtk4::glib::object::IsA;
use gtk4::prelude::GtkWindowExt;
use gtk4::prelude::NativeExt;
use gtk4::prelude::WidgetExt;
use gtk4_layer_shell::Edge;
use gtk4_layer_shell::KeyboardMode as GtkKeyboardMode;
use gtk4_layer_shell::LayerShell;
use webkit6::prelude::WebViewExt;

use crate::config::Anchor;
use crate::config::Exclusivity;
use crate::config::KeyboardMode;
use crate::config::Layer;
use crate::config::Margin;

thread_local! {
    static TRANSPARENCY_CSS_LOADED: Cell<bool> = const { Cell::new(false) };
}

pub trait LayerWindowExt: IsA<gtk4::Window> {
    fn apply_namespace(&self, namespace: &str) {
        self.set_namespace(Some(namespace));
    }

    fn apply_visibility(&self, visible: bool) {
        if visible {
            self.present();
        }
    }

    fn apply_transparency(&self, transparent: bool, webview: &webkit6::WebView) {
        if !transparent {
            return;
        }

        let window = self.as_ref();

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

    fn apply_click_through(&self, ct: bool) {
        let window = self.as_ref();
        let apply = move |surface: &gdk::Surface| {
            surface.set_input_region(ct.then(cairo::Region::create).as_ref());
        };

        match window.surface() {
            Some(surface) => apply(&surface),
            None => {
                window.connect_realize(move |window| {
                    if let Some(surface) = window.surface() {
                        apply(&surface);
                    }
                });
            }
        }
    }

    fn apply_layer(&self, layer: Layer) {
        self.set_layer(layer.to_gtk());
    }

    fn apply_anchor(&self, anchor: &Anchor) {
        self.set_anchor(Edge::Top, false);
        self.set_anchor(Edge::Right, false);
        self.set_anchor(Edge::Bottom, false);
        self.set_anchor(Edge::Left, false);

        for edge in anchor.edges() {
            self.set_anchor(edge, true);
        }
    }

    fn apply_margin(&self, margin: Margin) {
        match margin {
            Margin::Named(m) => {
                self.set_margin(Edge::Top, m.top);
                self.set_margin(Edge::Right, m.right);
                self.set_margin(Edge::Bottom, m.bottom);
                self.set_margin(Edge::Left, m.left);
            }
            Margin::Number(n) => {
                self.set_margin(Edge::Top, n);
                self.set_margin(Edge::Right, n);
                self.set_margin(Edge::Bottom, n);
                self.set_margin(Edge::Left, n);
            }
        }
    }

    fn apply_exclusivity(&self, exclusive: Exclusivity) {
        match exclusive {
            Exclusivity::Number(n) => self.set_exclusive_zone(n),
            Exclusivity::Bool(true) => self.auto_exclusive_zone_enable(),
            Exclusivity::Bool(false) => self.set_exclusive_zone(0),
        }
    }

    fn apply_keyboard_mode(&self, mode: KeyboardMode) {
        match mode {
            KeyboardMode::None => self.set_keyboard_mode(GtkKeyboardMode::None),
            KeyboardMode::OnDemand => self.set_keyboard_mode(GtkKeyboardMode::OnDemand),
            KeyboardMode::Exclusive => self.set_keyboard_mode(GtkKeyboardMode::Exclusive),
        }
    }
}

impl<T: IsA<gtk4::Window>> LayerWindowExt for T {}
