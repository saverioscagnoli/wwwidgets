use std::cell::Cell;
use std::cell::RefCell;
use std::rc::Rc;

use gio::glib;
use gio::glib::variant::ToVariant;
use gio::prelude::FileExt;
use gio::prelude::FileExtManual;
use gtk4::gdk;
use gtk4::gdk::prelude::TextureExt;
use serde::Serialize;
use serde_json::json;
use traccia::info;
use traccia::warn;

use crate::Shared;

const WATCHER_NAME: &str = "org.kde.StatusNotifierWatcher";
const WATCHER_PATH: &str = "/StatusNotifierWatcher";
const ITEM_IFACE: &str = "org.kde.StatusNotifierItem";
const MENU_IFACE: &str = "com.canonical.dbusmenu";
const PROPS_IFACE: &str = "org.freedesktop.DBus.Properties";

const ICON_SIZE: i32 = 32;

const XML: &str = r#"
<node>
    <interface name="org.kde.StatusNotifierWatcher">
        <method name="RegisterStatusNotifierItem">
            <arg name="service" type="s" direction="in"/>
        </method>
        <method name="RegisterStatusNotifierHost">
            <arg name="service" type="s" direction="in"/>
        </method>
        <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
        <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
        <property name="ProtocolVersion" type="i" access="read"/>
        <signal name="StatusNotifierItemRegistered">
            <arg name="service" type="s"/>
        </signal>
        <signal name="StatusNotifierItemUnregistered">
            <arg name="service" type="s"/>
        </signal>
        <signal name="StatusNotifierHostRegistered"/>
    </interface>
</node>
"#;

#[derive(Serialize)]
#[derive(Default)]
#[derive(Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct TrayItem {
    pub id: String,
    pub title: String,
    pub status: String,
    pub icon: Option<String>,
    pub attention_icon: Option<String>,
    pub tooltip: Option<String>,
    pub has_menu: bool,
    pub item_is_menu: bool,
}

struct Entry {
    bus: String,
    path: String,
    menu: Option<String>,
    item: TrayItem,
    unwatch: Box<dyn FnOnce()>,
    _signals: gio::SignalSubscription,
}

#[derive(Default)]
pub struct Tray {
    conn: RefCell<Option<gio::DBusConnection>>,
    entries: RefCell<Vec<Entry>>,
    is_watcher: Cell<bool>,
    _host: RefCell<Option<gio::SignalSubscription>>,
}

impl Tray {
    fn to_json(&self) -> String {
        let items = self
            .entries
            .borrow()
            .iter()
            .map(|e| e.item.clone())
            .collect::<Vec<_>>();

        serde_json::to_string(&items).unwrap_or_else(|_| "[]".into())
    }

    fn ids(&self) -> Vec<String> {
        self.entries
            .borrow()
            .iter()
            .map(|e| e.item.id.clone())
            .collect()
    }

    fn target(&self, id: &str) -> Option<(gio::DBusConnection, String, String, Option<String>)> {
        let conn = self.conn.borrow().clone()?;
        let entries = self.entries.borrow();
        let e = entries.iter().find(|e| e.item.id == id)?;

        Some((conn, e.bus.clone(), e.path.clone(), e.menu.clone()))
    }

    fn emit(&self, signal: &str, args: Option<glib::Variant>) {
        if !self.is_watcher.get() {
            return;
        }

        if let Some(conn) = self.conn.borrow().as_ref() {
            let _ = conn.emit_signal(None, WATCHER_PATH, WATCHER_NAME, signal, args.as_ref());
        }
    }
}

fn publish(shared: &Shared) {
    let _ = shared.set_state("tray".into(), shared.tray.to_json(), None);
}

fn parse_service(service: &str, sender: Option<&str>) -> Option<(String, String)> {
    if service.starts_with('/') {
        return Some((sender?.to_string(), service.to_string()));
    }

    match service.find('/') {
        Some(i) => Some((service[..i].to_string(), service[i..].to_string())),
        None => Some((service.to_string(), "/StatusNotifierItem".to_string())),
    }
}

fn data_url(mime: &str, bytes: &[u8]) -> String {
    format!("data:{mime};base64,{}", glib::base64_encode(bytes))
}

fn themed_icon(name: &str, theme_path: Option<&str>) -> Option<String> {
    let file = if name.starts_with('/') {
        gio::File::for_path(name)
    } else {
        let display = gdk::Display::default()?;
        let theme = gtk4::IconTheme::for_display(&display);

        if let Some(dir) = theme_path
            && !theme.search_path().iter().any(|p| p.as_os_str() == dir)
        {
            theme.add_search_path(dir);
        }

        if !theme.has_icon(name) {
            return None;
        }

        theme
            .lookup_icon(
                name,
                &[],
                ICON_SIZE,
                1,
                gtk4::TextDirection::None,
                gtk4::IconLookupFlags::empty(),
            )
            .file()?
    };

    let (bytes, _) = file.load_contents(gio::Cancellable::NONE).ok()?;
    let mime = if file.uri().ends_with(".svg") {
        "image/svg+xml"
    } else {
        "image/png"
    };

    Some(data_url(mime, &bytes))
}

fn pixmap_icon(pixmaps: &glib::Variant) -> Option<String> {
    let (w, h, data) = pixmaps
        .iter()
        .filter_map(|p| p.get::<(i32, i32, Vec<u8>)>())
        .filter(|(w, h, d)| *w > 0 && *h > 0 && d.len() == (*w * *h * 4) as usize)
        .min_by_key(|(w, _, _)| (w - ICON_SIZE).abs())?;

    let texture = gdk::MemoryTexture::new(
        w,
        h,
        gdk::MemoryFormat::A8r8g8b8,
        &glib::Bytes::from_owned(data),
        w as usize * 4,
    );

    Some(data_url("image/png", &texture.save_to_png_bytes()))
}

fn refresh(shared: &Rc<Shared>, id: &str) {
    let Some((conn, bus, path, _)) = shared.tray.target(id) else {
        return;
    };

    let shared = Rc::clone(shared);
    let id = id.to_string();

    conn.call(
        Some(&bus),
        &path,
        PROPS_IFACE,
        "GetAll",
        Some(&(ITEM_IFACE,).to_variant()),
        Some(glib::VariantTy::new("(a{sv})").unwrap()),
        gio::DBusCallFlags::NONE,
        5000,
        gio::Cancellable::NONE,
        move |res| {
            let props = match res {
                Ok(v) => glib::VariantDict::new(Some(&v.child_value(0))),
                Err(e) => {
                    warn!("tray: failed to read {id}: {e}");
                    return;
                }
            };

            let str = |key: &str| {
                props
                    .lookup::<String>(key)
                    .ok()
                    .flatten()
                    .filter(|s| !s.is_empty())
            };
            let theme_path = str("IconThemePath");

            let icon = str("IconName")
                .and_then(|n| themed_icon(&n, theme_path.as_deref()))
                .or_else(|| {
                    props
                        .lookup_value("IconPixmap", None)
                        .and_then(|v| pixmap_icon(&v))
                });

            let attention_icon = str("AttentionIconName")
                .and_then(|n| themed_icon(&n, theme_path.as_deref()))
                .or_else(|| {
                    props
                        .lookup_value("AttentionIconPixmap", None)
                        .and_then(|v| pixmap_icon(&v))
                });

            let tooltip = props.lookup_value("ToolTip", None).and_then(|v| {
                let title = v.try_child_value(2)?.str()?.to_string();
                let body = v.try_child_value(3)?.str()?.to_string();
                [title, body].into_iter().find(|s| !s.is_empty())
            });

            let menu = props
                .lookup::<glib::variant::ObjectPath>("Menu")
                .ok()
                .flatten()
                .map(|p| p.as_str().to_string())
                .filter(|p| p != "/");

            {
                let mut entries = shared.tray.entries.borrow_mut();
                let Some(e) = entries.iter_mut().find(|e| e.item.id == id) else {
                    return;
                };

                e.item = TrayItem {
                    id: id.clone(),
                    title: str("Title").or_else(|| str("Id")).unwrap_or_default(),
                    status: str("Status").unwrap_or_else(|| "Active".into()),
                    icon,
                    attention_icon,
                    tooltip,
                    has_menu: menu.is_some(),
                    item_is_menu: props
                        .lookup::<bool>("ItemIsMenu")
                        .ok()
                        .flatten()
                        .unwrap_or(false),
                };

                e.menu = menu;
            }

            publish(&shared);
        },
    );
}

fn remove_item(shared: &Shared, id: &str) {
    let removed = {
        let mut entries = shared.tray.entries.borrow_mut();
        let pos = entries.iter().position(|e| e.item.id == id);
        pos.map(|i| entries.remove(i))
    };

    let Some(entry) = removed else { return };

    (entry.unwatch)();

    shared
        .tray
        .emit("StatusNotifierItemUnregistered", Some((id,).to_variant()));

    publish(shared);
}

fn add_item(shared: &Rc<Shared>, conn: &gio::DBusConnection, bus: String, path: String) {
    let id = format!("{bus}{path}");

    if shared.tray.entries.borrow().iter().any(|e| e.item.id == id) {
        return;
    }

    let watch = gio::bus_watch_name_on_connection(
        conn,
        &bus,
        gio::BusNameWatcherFlags::NONE,
        |_, _, _| {},
        {
            let shared = Rc::clone(shared);
            let id = id.clone();
            move |_, _| remove_item(&shared, &id)
        },
    );

    let signals = conn.subscribe_to_signal(
        Some(&bus),
        Some(ITEM_IFACE),
        None,
        Some(&path),
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            let id = id.clone();
            move |_| refresh(&shared, &id)
        },
    );

    shared.tray.entries.borrow_mut().push(Entry {
        bus,
        path,
        menu: None,
        item: TrayItem {
            id: id.clone(),
            ..Default::default()
        },
        unwatch: Box::new(move || gio::bus_unwatch_name(watch)),
        _signals: signals,
    });

    shared.tray.emit(
        "StatusNotifierItemRegistered",
        Some((id.as_str(),).to_variant()),
    );

    refresh(shared, &id);
}

fn register_watcher(conn: &gio::DBusConnection, shared: Rc<Shared>) {
    let node = gio::DBusNodeInfo::for_xml(XML).unwrap();
    let iface = node.lookup_interface(WATCHER_NAME).unwrap();

    let _ =
        conn.register_object(WATCHER_PATH, &iface)
            .method_call({
                let shared = Rc::clone(&shared);
                move |conn, sender, _, _, method, params, invocation| match method {
                    "RegisterStatusNotifierItem" => {
                        let (service,) = params.get::<(String,)>().unwrap();

                        match parse_service(&service, sender) {
                            Some((bus, path)) => {
                                add_item(&shared, &conn, bus, path);
                                invocation.return_value(None);
                            }
                            None => invocation.return_dbus_error(
                                "org.freedesktop.DBus.Error.InvalidArgs",
                                "invalid service",
                            ),
                        }
                    }
                    "RegisterStatusNotifierHost" => {
                        shared.tray.emit("StatusNotifierHostRegistered", None);
                        invocation.return_value(None);
                    }
                    _ => invocation
                        .return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method),
                }
            })
            .property(move |_, _, _, _, prop| match prop {
                "RegisteredStatusNotifierItems" => shared.tray.ids().to_variant(),
                "IsStatusNotifierHostRegistered" => true.to_variant(),
                "ProtocolVersion" => 0i32.to_variant(),
                _ => glib::Variant::from_none(glib::VariantTy::VARIANT),
            })
            .build();
}

fn become_host(conn: gio::DBusConnection, shared: Rc<Shared>) {
    let host = format!("org.kde.StatusNotifierHost-{}", std::process::id());

    gio::bus_own_name_on_connection(
        &conn,
        &host,
        gio::BusNameOwnerFlags::NONE,
        |_, _| {},
        |_, _| {},
    );

    let subscription = conn.subscribe_to_signal(
        Some(WATCHER_NAME),
        Some(WATCHER_NAME),
        None,
        Some(WATCHER_PATH),
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(&shared);
            move |signal| {
                let Some((service,)) = signal.parameters.get::<(String,)>() else {
                    return;
                };

                match signal.signal_name {
                    "StatusNotifierItemRegistered" => {
                        if let Some((bus, path)) = parse_service(&service, None) {
                            add_item(&shared, signal.connection, bus, path);
                        }
                    }
                    "StatusNotifierItemUnregistered" => remove_item(&shared, &service),
                    _ => {}
                }
            }
        },
    );

    shared.tray._host.replace(Some(subscription));

    conn.call(
        Some(WATCHER_NAME),
        WATCHER_PATH,
        WATCHER_NAME,
        "RegisterStatusNotifierHost",
        Some(&(host.as_str(),).to_variant()),
        None,
        gio::DBusCallFlags::NONE,
        -1,
        gio::Cancellable::NONE,
        |_| {},
    );

    conn.clone().call(
        Some(WATCHER_NAME),
        WATCHER_PATH,
        PROPS_IFACE,
        "Get",
        Some(&(WATCHER_NAME, "RegisteredStatusNotifierItems").to_variant()),
        Some(glib::VariantTy::new("(v)").unwrap()),
        gio::DBusCallFlags::NONE,
        -1,
        gio::Cancellable::NONE,
        move |res| {
            let Ok(v) = res else {
                return;
            };

            let services = v
                .child_value(0)
                .as_variant()
                .and_then(|v| v.get::<Vec<String>>())
                .unwrap_or_default();

            for service in services {
                if let Some((bus, path)) = parse_service(&service, None) {
                    add_item(&shared, &conn, bus, path);
                }
            }
        },
    );
}

pub fn serve(shared: Rc<Shared>) {
    gio::bus_own_name(
        gio::BusType::Session,
        WATCHER_NAME,
        gio::BusNameOwnerFlags::DO_NOT_QUEUE,
        {
            let shared = Rc::clone(&shared);
            move |conn, _| {
                shared.tray.conn.replace(Some(conn.clone()));
                register_watcher(&conn, Rc::clone(&shared));
            }
        },
        {
            let shared = Rc::clone(&shared);
            move |_, _| {
                shared.tray.is_watcher.set(true);
                info!("Tray watcher running");
            }
        },
        move |conn, _| {
            let Some(conn) = conn else {
                warn!("tray: no session bus");
                return;
            };

            info!("Another tray watcher is running, attaching as host");
            become_host(conn, Rc::clone(&shared));
        },
    );
}

pub fn activate(
    shared: &Shared,
    id: &str,
    method: &str,
    args: glib::Variant,
) -> Result<(), String> {
    let (conn, bus, path, _) = shared.tray.target(id).ok_or("no such tray item")?;

    conn.call(
        Some(&bus),
        &path,
        ITEM_IFACE,
        method,
        Some(&args),
        None,
        gio::DBusCallFlags::NONE,
        -1,
        gio::Cancellable::NONE,
        |_| {},
    );

    Ok(())
}

fn strip_mnemonic(label: &str) -> String {
    let mut out = String::with_capacity(label.len());
    let mut chars = label.chars().peekable();

    while let Some(c) = chars.next() {
        if c != '_' {
            out.push(c);
        } else if chars.peek() == Some(&'_') {
            out.push('_');
            chars.next();
        }
    }

    out
}

fn layout_to_json(node: &glib::Variant) -> serde_json::Value {
    let id = node.child_value(0).get::<i32>().unwrap_or(0);
    let props = glib::VariantDict::new(Some(&node.child_value(1)));
    let str = |key: &str| props.lookup::<String>(key).ok().flatten();
    let bool = |key: &str, default| props.lookup::<bool>(key).ok().flatten().unwrap_or(default);

    let children = node
        .child_value(2)
        .iter()
        .filter_map(|c| c.as_variant())
        .map(|c| layout_to_json(&c))
        .filter(|c| c["visible"] == true)
        .collect::<Vec<_>>();

    json!({
        "id": id,
        "label": str("label").map(|l| strip_mnemonic(&l)).unwrap_or_default(),
        "type": str("type").unwrap_or_else(|| "standard".into()),
        "enabled": bool("enabled", true),
        "visible": bool("visible", true),
        "iconName": str("icon-name"),
        "toggleType": str("toggle-type"),
        "toggleState": props.lookup::<i32>("toggle-state").ok().flatten(),
        "children": children,
    })
}

pub fn menu<F>(shared: &Shared, id: &str, done: F)
where
    F: FnOnce(Result<serde_json::Value, String>) + 'static,
{
    let Some((conn, bus, _, Some(menu))) = shared.tray.target(id) else {
        done(Err("tray item has no menu".into()));
        return;
    };

    conn.call(
        Some(&bus),
        &menu,
        MENU_IFACE,
        "AboutToShow",
        Some(&(0i32,).to_variant()),
        None,
        gio::DBusCallFlags::NONE,
        -1,
        gio::Cancellable::NONE,
        |_| {},
    );

    conn.call(
        Some(&bus),
        &menu,
        MENU_IFACE,
        "GetLayout",
        Some(&(0i32, -1i32, Vec::<String>::new()).to_variant()),
        Some(glib::VariantTy::new("(u(ia{sv}av))").unwrap()),
        gio::DBusCallFlags::NONE,
        5000,
        gio::Cancellable::NONE,
        move |res| match res {
            Ok(v) => done(Ok(layout_to_json(&v.child_value(1)))),
            Err(e) => done(Err(e.to_string())),
        },
    );
}

pub fn menu_click(shared: &Shared, id: &str, item: i32) -> Result<(), String> {
    let (conn, bus, _, menu) = shared.tray.target(id).ok_or("no such tray item")?;
    let menu = menu.ok_or("tray item has no menu")?;
    let time = (glib::real_time() / 1000) as u32;

    conn.call(
        Some(&bus),
        &menu,
        MENU_IFACE,
        "Event",
        Some(&(item, "clicked", 0i32.to_variant(), time).to_variant()),
        None,
        gio::DBusCallFlags::NONE,
        -1,
        gio::Cancellable::NONE,
        |_| {},
    );

    Ok(())
}
