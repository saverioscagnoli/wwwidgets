use std::cell::RefCell;
use std::rc::Rc;

use gio::glib::variant::FromVariant;
use gio::glib::variant::ToVariant;
use gio::glib::{self};
use serde::Serialize;
use traccia::info;
use traccia::warn;

use crate::Shared;

const UPOWER: &str = "org.freedesktop.UPower";
const UPOWER_PATH: &str = "/org/freedesktop/UPower";
const DEVICE_PATH: &str = "/org/freedesktop/UPower/devices/DisplayDevice";
const DEVICE_IFACE: &str = "org.freedesktop.UPower.Device";
const PROPS_IFACE: &str = "org.freedesktop.DBus.Properties";

fn get<T: FromVariant>(props: &glib::VariantDict, key: &str) -> Option<T> {
    props.lookup::<T>(key).ok().flatten()
}

fn set<T: FromVariant>(props: &glib::VariantDict, key: &str, field: &mut T) {
    if let Some(v) = get(props, key) {
        *field = v;
    }
}

fn state_name(state: u32) -> &'static str {
    match state {
        1 => "charging",
        2 => "discharging",
        3 => "empty",
        4 => "full",
        5 => "pending-charge",
        6 => "pending-discharge",
        _ => "unknown",
    }
}

fn warning_name(level: u32) -> &'static str {
    match level {
        1 => "none",
        2 => "discharging",
        3 => "low",
        4 => "critical",
        5 => "action",
        _ => "unknown",
    }
}

#[derive(Serialize)]
#[derive(Default)]
#[derive(Debug, Clone)]
#[serde(rename_all = "camelCase")]
struct Info {
    present: bool,
    on_battery: bool,
    percentage: f64,
    state: &'static str,
    warning: &'static str,
    time_to_empty: Option<i64>,
    time_to_full: Option<i64>,
    energy_rate: f64,
    icon: String,
}

impl Info {
    fn update(&mut self, iface: &str, props: &glib::VariantDict) {
        match iface {
            UPOWER => set(props, "OnBattery", &mut self.on_battery),
            DEVICE_IFACE => {
                set(props, "IsPresent", &mut self.present);
                set(props, "Percentage", &mut self.percentage);
                set(props, "EnergyRate", &mut self.energy_rate);
                set(props, "IconName", &mut self.icon);

                if let Some(state) = get::<u32>(props, "State") {
                    self.state = state_name(state);
                }

                if let Some(level) = get::<u32>(props, "WarningLevel") {
                    self.warning = warning_name(level);
                }

                if let Some(t) = get::<i64>(props, "TimeToEmpty") {
                    self.time_to_empty = (t > 0).then_some(t);
                }

                if let Some(t) = get::<i64>(props, "TimeToFull") {
                    self.time_to_full = (t > 0).then_some(t);
                }
            }

            _ => {}
        }
    }
}

#[derive(Default)]
pub struct Battery {
    info: RefCell<Info>,
    signals: RefCell<Option<gio::SignalSubscription>>,
}

fn publish(shared: &Shared) {
    let json =
        serde_json::to_string(&*shared.battery.info.borrow()).unwrap_or_else(|_| "{}".into());
    let _ = shared.set_state("battery".into(), json, None);
}

fn load(shared: &Rc<Shared>, conn: &gio::DBusConnection, path: &str, iface: &'static str) {
    let shared = Rc::clone(shared);

    conn.call(
        Some(UPOWER),
        path,
        PROPS_IFACE,
        "GetAll",
        Some(&(iface,).to_variant()),
        Some(glib::VariantTy::new("(a{sv})").unwrap()),
        gio::DBusCallFlags::NONE,
        5000,
        gio::Cancellable::NONE,
        move |res| match res {
            Ok(v) => {
                let props = glib::VariantDict::new(Some(&v.child_value(0)));
                shared.battery.info.borrow_mut().update(iface, &props);
                publish(&shared);
            }
            Err(e) => warn!("battery: failed to read {iface}: {e}"),
        },
    )
}

fn subscribe(shared: &Rc<Shared>, conn: &gio::DBusConnection) {
    let subscription = conn.subscribe_to_signal(
        Some(UPOWER),
        Some(PROPS_IFACE),
        Some("PropertiesChanged"),
        None,
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            move |signal| {
                if signal.object_path != DEVICE_PATH && signal.object_path != UPOWER_PATH {
                    return;
                };

                let Some(iface) = signal.parameters.child_value(0).str().map(String::from) else {
                    return;
                };

                let props = glib::VariantDict::new(Some(&signal.parameters.child_value(1)));

                shared.battery.info.borrow_mut().update(&iface, &props);
                publish(&shared);
            }
        },
    );

    shared.battery.signals.replace(Some(subscription));
}

pub fn serve(shared: Rc<Shared>) {
    gio::bus_watch_name(
        gio::BusType::System,
        UPOWER,
        gio::BusNameWatcherFlags::NONE,
        {
            let shared = Rc::clone(&shared);
            move |conn, _, _| {
                info!("Battery service enabled");

                subscribe(&shared, &conn);
                load(&shared, &conn, DEVICE_PATH, DEVICE_IFACE);
                load(&shared, &conn, UPOWER_PATH, UPOWER);
            }
        },
        move |_, _| {
            warn!("battery: upower not running");

            shared.battery.signals.take();
            shared.battery.info.replace(Info::default());
            publish(&shared);
        },
    );
}
