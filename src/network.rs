use std::cell::RefCell;
use std::collections::BTreeMap;
use std::collections::HashMap;
use std::rc::Rc;
use std::time::Duration;

use gio::glib;
use gio::glib::variant::FromVariant;
use gio::glib::variant::ObjectPath;
use gio::glib::variant::ToVariant;
use serde::Serialize;
use traccia::info;
use traccia::warn;

use crate::Shared;

const NM: &str = "org.freedesktop.NetworkManager";
const NM_PATH: &str = "/org/freedesktop/NetworkManager";
const ROOT_PATH: &str = "/org/freedesktop";
const DEVICE_IFACE: &str = "org.freedesktop.NetworkManager.Device";
const WIRELESS_IFACE: &str = "org.freedesktop.NetworkManager.Device.Wireless";
const AP_IFACE: &str = "org.freedesktop.NetworkManager.AccessPoint";
const ACTIVE_IFACE: &str = "org.freedesktop.NetworkManager.Connection.Active";
const PROFILE_IFACE: &str = "org.freedesktop.NetworkManager.Settings.Connection";
const PROPS_IFACE: &str = "org.freedesktop.DBus.Properties";
const MANAGER_IFACE: &str = "org.freedesktop.DBus.ObjectManager";

const DEVICE_WIFI: u32 = 2;
const HIDDEN_DEVICES: [u32; 2] = [30, 32];

const ACTIVE_ACTIVATED: u32 = 2;
const ACTIVE_DEACTIVATED: u32 = 4;

const ACTIVATION_TIMEOUT: Duration = Duration::from_secs(90);

const AP_PRIVACY: u32 = 0x1;
const KEY_MGMT_PSK: u32 = 0x100;
const KEY_MGMT_8021X: u32 = 0x200;
const KEY_MGMT_SAE: u32 = 0x400;

fn get<T: FromVariant>(props: &glib::VariantDict, key: &str) -> Option<T> {
    props.lookup::<T>(key).ok().flatten()
}

fn set<T: FromVariant>(props: &glib::VariantDict, key: &str, field: &mut T) {
    if let Some(v) = get(props, key) {
        *field = v;
    }
}

fn set_path(props: &glib::VariantDict, key: &str, field: &mut String) {
    if let Some(v) = get::<ObjectPath>(props, key) {
        *field = v.as_str().into();
    }
}

fn entry<'a, T: Default>(
    map: &'a mut BTreeMap<String, T>,
    path: &str,
    create: bool,
) -> Option<&'a mut T> {
    if create {
        Some(map.entry(path.into()).or_default())
    } else {
        map.get_mut(path)
    }
}

fn state_name(state: u32) -> &'static str {
    match state {
        10 => "asleep",
        20 => "disconnected",
        30 => "disconnecting",
        40 => "connecting",
        50 => "connected-local",
        60 => "connected-site",
        70 => "connected",
        _ => "unknown",
    }
}

fn connectivity_name(connectivity: u32) -> &'static str {
    match connectivity {
        1 => "none",
        2 => "portal",
        3 => "limited",
        4 => "full",
        _ => "unknown",
    }
}

fn device_type_name(kind: u32) -> &'static str {
    match kind {
        1 => "ethernet",
        2 => "wifi",
        5 => "bluetooth",
        8 => "modem",
        10 => "bond",
        11 => "vlan",
        13 => "bridge",
        16 => "tun",
        29 => "wireguard",
        _ => "other",
    }
}

fn device_state_name(state: u32) -> &'static str {
    match state {
        10 => "unmanaged",
        20 => "unavailable",
        30 => "disconnected",
        40..=90 => "connecting",
        100 => "connected",
        110 => "disconnecting",
        120 => "failed",
        _ => "unknown",
    }
}

fn failure_reason(reason: u32) -> &'static str {
    match reason {
        2 => "disconnected by user",
        3 => "device disconnected",
        5 => "could not get an ip address",
        6 => "timed out",
        9 => "wrong password",
        10 => "login failed",
        11 => "connection removed",
        _ => "connection failed",
    }
}

#[derive(Default)]
struct Manager {
    wifi_enabled: bool,
    wifi_hardware: bool,
    state: u32,
    connectivity: u32,
    primary: String,
}

impl Manager {
    fn update(&mut self, props: &glib::VariantDict) {
        set(props, "WirelessEnabled", &mut self.wifi_enabled);
        set(props, "WirelessHardwareEnabled", &mut self.wifi_hardware);
        set(props, "State", &mut self.state);
        set(props, "Connectivity", &mut self.connectivity);
        set_path(props, "PrimaryConnection", &mut self.primary);
    }
}

#[derive(Default)]
struct Device {
    interface: String,
    kind: u32,
    state: u32,
    active: String,
    access_points: Vec<String>,
    active_ap: String,
}

impl Device {
    fn update(&mut self, iface: &str, props: &glib::VariantDict) {
        match iface {
            DEVICE_IFACE => {
                set(props, "Interface", &mut self.interface);
                set(props, "DeviceType", &mut self.kind);
                set(props, "State", &mut self.state);
                set_path(props, "ActiveConnection", &mut self.active);
            }
            WIRELESS_IFACE => {
                set_path(props, "ActiveAccessPoint", &mut self.active_ap);

                if let Some(aps) = get::<Vec<ObjectPath>>(props, "AccessPoints") {
                    self.access_points = aps.iter().map(|p| p.as_str().into()).collect();
                }
            }
            _ => {}
        }
    }
}

#[derive(Default)]
struct AccessPoint {
    ssid: String,
    strength: u8,
    frequency: u32,
    flags: u32,
    wpa: u32,
    rsn: u32,
}

impl AccessPoint {
    fn update(&mut self, props: &glib::VariantDict) {
        if let Some(ssid) = get::<Vec<u8>>(props, "Ssid") {
            self.ssid = String::from_utf8_lossy(&ssid).into();
        }

        set(props, "Strength", &mut self.strength);
        set(props, "Frequency", &mut self.frequency);
        set(props, "Flags", &mut self.flags);
        set(props, "WpaFlags", &mut self.wpa);
        set(props, "RsnFlags", &mut self.rsn);
    }

    fn secure(&self) -> bool {
        self.flags & AP_PRIVACY != 0 || self.wpa != 0 || self.rsn != 0
    }

    fn key_mgmt(&self) -> Result<Option<&'static str>, String> {
        let mgmt = self.wpa | self.rsn;

        if !self.secure() {
            Ok(None)
        } else if mgmt & KEY_MGMT_8021X != 0 {
            Err("enterprise networks are not supported".into())
        } else if mgmt & KEY_MGMT_SAE != 0 && mgmt & KEY_MGMT_PSK == 0 {
            Ok(Some("sae"))
        } else if mgmt & KEY_MGMT_PSK != 0 {
            Ok(Some("wpa-psk"))
        } else {
            Err("wep networks are not supported".into())
        }
    }
}

#[derive(Default)]
struct Profile {
    ssid: Option<String>,
}

#[derive(Default)]
struct Active {
    id: String,
    kind: String,
    state: u32,
}

impl Active {
    fn update(&mut self, props: &glib::VariantDict) {
        set(props, "Id", &mut self.id);
        set(props, "Type", &mut self.kind);
        set(props, "State", &mut self.state);
    }
}

struct Pending {
    created: Option<String>,
    done: Box<dyn FnOnce(Result<(), String>)>,
}

#[derive(Serialize)]
struct PrimaryOut<'a> {
    id: &'a str,
    #[serde(rename = "type")]
    kind: &'a str,
}

#[derive(Serialize)]
struct DeviceOut<'a> {
    id: &'a str,
    interface: &'a str,
    #[serde(rename = "type")]
    kind: &'static str,
    state: &'static str,
    connection: Option<&'a str>,
}

#[derive(Serialize)]
struct NetworkOut<'a> {
    id: &'a str,
    device: &'a str,
    ssid: &'a str,
    strength: u8,
    frequency: u32,
    secure: bool,
    known: bool,
    active: bool,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Snapshot<'a> {
    wifi_enabled: bool,
    wifi_hardware_enabled: bool,
    state: &'static str,
    connectivity: &'static str,
    primary: Option<PrimaryOut<'a>>,
    devices: Vec<DeviceOut<'a>>,
    networks: Vec<NetworkOut<'a>>,
}

#[derive(Default)]
pub struct Network {
    conn: RefCell<Option<gio::DBusConnection>>,
    manager: RefCell<Manager>,
    devices: RefCell<BTreeMap<String, Device>>,
    access_points: RefCell<BTreeMap<String, AccessPoint>>,
    profiles: RefCell<BTreeMap<String, Profile>>,
    active: RefCell<BTreeMap<String, Active>>,
    pending: RefCell<HashMap<String, Pending>>,
    signals: RefCell<Vec<gio::SignalSubscription>>,
}

impl Network {
    fn to_json(&self) -> String {
        let manager = self.manager.borrow();
        let devices = self.devices.borrow();
        let aps = self.access_points.borrow();
        let profiles = self.profiles.borrow();
        let active = self.active.borrow();

        let primary = active.get(&manager.primary).map(|a| PrimaryOut {
            id: &a.id,
            kind: &a.kind,
        });

        let device_list = devices
            .iter()
            .filter(|(_, d)| !HIDDEN_DEVICES.contains(&d.kind))
            .map(|(path, d)| DeviceOut {
                id: path,
                interface: &d.interface,
                kind: device_type_name(d.kind),
                state: device_state_name(d.state),
                connection: active.get(&d.active).map(|a| a.id.as_str()),
            })
            .collect();

        let mut best: BTreeMap<(&str, &str), NetworkOut> = BTreeMap::new();

        for (device_path, device) in devices.iter().filter(|(_, d)| d.kind == DEVICE_WIFI) {
            for ap_path in &device.access_points {
                let Some(ap) = aps.get(ap_path).filter(|ap| !ap.ssid.is_empty()) else {
                    continue;
                };

                let network = NetworkOut {
                    id: ap_path,
                    device: device_path,
                    ssid: &ap.ssid,
                    strength: ap.strength,
                    frequency: ap.frequency,
                    secure: ap.secure(),
                    known: profiles
                        .values()
                        .any(|p| p.ssid.as_deref() == Some(ap.ssid.as_str())),
                    active: device.active_ap == *ap_path,
                };

                let key = (device_path.as_str(), ap.ssid.as_str());
                let replace = best
                    .get(&key)
                    .is_none_or(|b| !b.active && (network.active || network.strength > b.strength));

                if replace {
                    best.insert(key, network);
                }
            }
        }

        let mut networks = best.into_values().collect::<Vec<_>>();
        networks.sort_by(|a, b| b.active.cmp(&a.active).then(b.strength.cmp(&a.strength)));

        let snapshot = Snapshot {
            wifi_enabled: manager.wifi_enabled,
            wifi_hardware_enabled: manager.wifi_hardware,
            state: state_name(manager.state),
            connectivity: connectivity_name(manager.connectivity),
            primary,
            devices: device_list,
            networks,
        };

        serde_json::to_string(&snapshot).unwrap_or_else(|_| "{}".into())
    }

    fn apply(&self, path: &str, iface: &str, props: &glib::VariantDict, create: bool) -> bool {
        match iface {
            NM if path == NM_PATH => self.manager.borrow_mut().update(props),
            DEVICE_IFACE | WIRELESS_IFACE => {
                if let Some(device) = entry(&mut self.devices.borrow_mut(), path, create) {
                    device.update(iface, props);
                }
            }
            AP_IFACE => {
                if let Some(ap) = entry(&mut self.access_points.borrow_mut(), path, create) {
                    ap.update(props);
                }
            }
            ACTIVE_IFACE => {
                if let Some(active) = entry(&mut self.active.borrow_mut(), path, create) {
                    active.update(props);
                }
            }
            PROFILE_IFACE if create => {
                return self
                    .profiles
                    .borrow_mut()
                    .insert(path.into(), Profile::default())
                    .is_none();
            }
            _ => {}
        }

        false
    }

    fn remove(&self, path: &str, ifaces: &[String]) -> Option<Pending> {
        let mut pending = None;

        for iface in ifaces {
            match iface.as_str() {
                DEVICE_IFACE => _ = self.devices.borrow_mut().remove(path),
                AP_IFACE => _ = self.access_points.borrow_mut().remove(path),
                PROFILE_IFACE => _ = self.profiles.borrow_mut().remove(path),
                ACTIVE_IFACE => {
                    self.active.borrow_mut().remove(path);
                    pending = self.pending.borrow_mut().remove(path);
                }
                _ => {}
            }
        }

        pending
    }

    fn clear(&self) {
        self.conn.take();
        self.signals.borrow_mut().clear();
        *self.manager.borrow_mut() = Manager::default();
        self.devices.borrow_mut().clear();
        self.access_points.borrow_mut().clear();
        self.profiles.borrow_mut().clear();
        self.active.borrow_mut().clear();

        for (_, pending) in self.pending.borrow_mut().drain() {
            (pending.done)(Err("networkmanager stopped".into()));
        }
    }
}

fn publish(shared: &Shared) {
    let _ = shared.set_state("network".into(), shared.network.to_json(), None);
}

fn fetch_profile(shared: &Rc<Shared>, conn: &gio::DBusConnection, path: &str) {
    let shared = Rc::clone(shared);
    let key = path.to_string();

    conn.call(
        Some(NM),
        path,
        PROFILE_IFACE,
        "GetSettings",
        None,
        Some(glib::VariantTy::new("(a{sa{sv}})").unwrap()),
        gio::DBusCallFlags::NONE,
        5000,
        gio::Cancellable::NONE,
        move |res| {
            let settings = match res {
                Ok(v) => v.child_value(0),
                Err(e) => {
                    warn!("network: failed to read profile {key}: {e}");
                    return;
                }
            };

            let ssid = settings
                .iter()
                .find(|e| e.child_value(0).str() == Some("802-11-wireless"))
                .map(|e| glib::VariantDict::new(Some(&e.child_value(1))))
                .and_then(|w| get::<Vec<u8>>(&w, "ssid"))
                .map(|ssid| String::from_utf8_lossy(&ssid).into_owned());

            if let Some(profile) = shared.network.profiles.borrow_mut().get_mut(&key) {
                profile.ssid = ssid;
            }

            publish(&shared);
        },
    );
}

fn ingest(shared: &Rc<Shared>, conn: &gio::DBusConnection, path: &str, ifaces: &glib::Variant) {
    for entry in ifaces.iter() {
        let Some(iface) = entry.child_value(0).str().map(String::from) else {
            continue;
        };

        let props = glib::VariantDict::new(Some(&entry.child_value(1)));

        if shared.network.apply(path, &iface, &props, true) {
            fetch_profile(shared, conn, path);
        }
    }
}

fn load(shared: &Rc<Shared>, conn: &gio::DBusConnection) {
    let shared = Rc::clone(shared);
    let conn = conn.clone();

    conn.clone().call(
        Some(NM),
        ROOT_PATH,
        MANAGER_IFACE,
        "GetManagedObjects",
        None,
        Some(glib::VariantTy::new("(a{oa{sa{sv}}})").unwrap()),
        gio::DBusCallFlags::NONE,
        5000,
        gio::Cancellable::NONE,
        move |res| {
            let objects = match res {
                Ok(v) => v.child_value(0),
                Err(e) => {
                    warn!("network: failed to list objects: {e}");
                    return;
                }
            };

            for object in objects.iter() {
                if let Some(path) = object.child_value(0).str() {
                    ingest(&shared, &conn, path, &object.child_value(1));
                }
            }

            publish(&shared);
        },
    );
}

fn settle(shared: &Shared, path: &str, state: u32, reason: u32) {
    if state != ACTIVE_ACTIVATED && state != ACTIVE_DEACTIVATED {
        return;
    }

    let Some(pending) = shared.network.pending.borrow_mut().remove(path) else {
        return;
    };

    if state == ACTIVE_ACTIVATED {
        (pending.done)(Ok(()));
        return;
    }

    if let Some(created) = pending.created {
        call(shared, &created, PROFILE_IFACE, "Delete", None, |_| {});
    }

    (pending.done)(Err(failure_reason(reason).into()));
}

fn subscribe(shared: &Rc<Shared>, conn: &gio::DBusConnection) {
    let added = conn.subscribe_to_signal(
        Some(NM),
        Some(MANAGER_IFACE),
        Some("InterfacesAdded"),
        Some(ROOT_PATH),
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            move |signal| {
                if let Some(path) = signal.parameters.child_value(0).str() {
                    ingest(
                        &shared,
                        signal.connection,
                        path,
                        &signal.parameters.child_value(1),
                    );
                    publish(&shared);
                }
            }
        },
    );

    let removed = conn.subscribe_to_signal(
        Some(NM),
        Some(MANAGER_IFACE),
        Some("InterfacesRemoved"),
        Some(ROOT_PATH),
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            move |signal| {
                let Some((path, ifaces)) = signal.parameters.get::<(ObjectPath, Vec<String>)>()
                else {
                    return;
                };

                if let Some(pending) = shared.network.remove(path.as_str(), &ifaces) {
                    (pending.done)(Err("connection failed".into()));
                }

                publish(&shared);
            }
        },
    );

    let changed = conn.subscribe_to_signal(
        Some(NM),
        Some(PROPS_IFACE),
        Some("PropertiesChanged"),
        None,
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            move |signal| {
                let Some(iface) = signal.parameters.child_value(0).str().map(String::from) else {
                    return;
                };

                let props = glib::VariantDict::new(Some(&signal.parameters.child_value(1)));

                shared
                    .network
                    .apply(signal.object_path, &iface, &props, false);
                publish(&shared);
            }
        },
    );

    let active = conn.subscribe_to_signal(
        Some(NM),
        Some(ACTIVE_IFACE),
        Some("StateChanged"),
        None,
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            move |signal| {
                if let Some((state, reason)) = signal.parameters.get::<(u32, u32)>() {
                    settle(&shared, signal.object_path, state, reason);
                }
            }
        },
    );

    shared
        .network
        .signals
        .replace(vec![added, removed, changed, active]);
}

pub fn serve(shared: Rc<Shared>) {
    gio::bus_watch_name(
        gio::BusType::System,
        NM,
        gio::BusNameWatcherFlags::NONE,
        {
            let shared = Rc::clone(&shared);
            move |conn, _, _| {
                info!("Network service enabled");

                shared.network.conn.replace(Some(conn.clone()));
                subscribe(&shared, &conn);
                load(&shared, &conn);
            }
        },
        move |_, _| {
            warn!("network: networkmanager not running");

            shared.network.clear();
            publish(&shared);
        },
    );
}

fn call<F>(
    shared: &Shared,
    path: &str,
    iface: &str,
    method: &str,
    args: Option<glib::Variant>,
    done: F,
) where
    F: FnOnce(Result<glib::Variant, String>) + 'static,
{
    let Some(conn) = shared.network.conn.borrow().clone() else {
        done(Err("networkmanager not available".into()));
        return;
    };

    conn.call(
        Some(NM),
        path,
        iface,
        method,
        args.as_ref(),
        None,
        gio::DBusCallFlags::NONE,
        -1,
        gio::Cancellable::NONE,
        move |res| done(res.map_err(|e| e.to_string())),
    );
}

fn object_path(path: &str) -> ObjectPath {
    ObjectPath::try_from(path.to_string()).unwrap()
}

fn wait_for<F>(shared: &Rc<Shared>, active: String, created: Option<String>, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    let state = shared.network.active.borrow().get(&active).map(|a| a.state);

    match state {
        Some(ACTIVE_ACTIVATED) => return done(Ok(())),
        Some(ACTIVE_DEACTIVATED) => return done(Err("connection failed".into())),
        _ => {}
    }

    shared.network.pending.borrow_mut().insert(
        active.clone(),
        Pending {
            created,
            done: Box::new(done),
        },
    );

    let shared = Rc::clone(shared);

    glib::timeout_add_local_once(ACTIVATION_TIMEOUT, move || {
        settle(&shared, &active, ACTIVE_DEACTIVATED, 6);
    });
}

pub fn set_wifi_enabled<F>(shared: &Shared, on: bool, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    let args = (NM, "WirelessEnabled", on.to_variant()).to_variant();
    call(shared, NM_PATH, PROPS_IFACE, "Set", Some(args), |res| {
        done(res.map(|_| ()))
    });
}

pub fn scan<F>(shared: &Shared, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    let wifi = shared
        .network
        .devices
        .borrow()
        .iter()
        .filter(|(_, d)| d.kind == DEVICE_WIFI)
        .map(|(path, _)| path.clone())
        .collect::<Vec<_>>();

    if wifi.is_empty() {
        return done(Err("no wifi device".into()));
    }

    for path in wifi {
        let args = (HashMap::<String, glib::Variant>::new(),).to_variant();
        call(
            shared,
            &path,
            WIRELESS_IFACE,
            "RequestScan",
            Some(args),
            |res| {
                if let Err(e) = res {
                    warn!("network: scan failed: {e}");
                }
            },
        );
    }

    done(Ok(()));
}

pub fn connect<F>(shared: &Rc<Shared>, id: &str, password: Option<String>, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    let device = shared
        .network
        .devices
        .borrow()
        .iter()
        .find(|(_, d)| d.access_points.iter().any(|ap| ap == id))
        .map(|(path, _)| path.clone());

    let Some(device) = device else {
        return done(Err(format!("no network {id}")));
    };

    let (ssid, key_mgmt) = match shared.network.access_points.borrow().get(id) {
        Some(ap) => match ap.key_mgmt() {
            Ok(key_mgmt) => (ap.ssid.clone(), key_mgmt),
            Err(e) => return done(Err(e)),
        },
        None => return done(Err(format!("no network {id}"))),
    };

    let profile = shared
        .network
        .profiles
        .borrow()
        .iter()
        .find(|(_, p)| p.ssid.as_deref() == Some(ssid.as_str()))
        .map(|(path, _)| path.clone());

    let shared_rc = Rc::clone(shared);

    if let Some(profile) = profile.filter(|_| password.is_none()) {
        let args = (object_path(&profile), object_path(&device), object_path(id)).to_variant();

        call(
            shared,
            NM_PATH,
            NM,
            "ActivateConnection",
            Some(args),
            move |res| match res {
                Ok(v) => match v.child_value(0).str() {
                    Some(active) => wait_for(&shared_rc, active.into(), None, done),
                    None => done(Err("unexpected reply".into())),
                },
                Err(e) => done(Err(e)),
            },
        );

        return;
    }

    let mut settings = HashMap::<&str, HashMap<&str, glib::Variant>>::new();

    if let Some(key_mgmt) = key_mgmt {
        let Some(password) = password.filter(|p| !p.is_empty()) else {
            return done(Err("password required".into()));
        };

        settings.insert(
            "802-11-wireless-security",
            HashMap::from([
                ("key-mgmt", key_mgmt.to_variant()),
                ("psk", password.to_variant()),
            ]),
        );
    }

    let args = (settings, object_path(&device), object_path(id)).to_variant();

    call(
        shared,
        NM_PATH,
        NM,
        "AddAndActivateConnection",
        Some(args),
        move |res| match res {
            Ok(v) => {
                let created = v.child_value(0).str().map(String::from);

                match v.child_value(1).str() {
                    Some(active) => wait_for(&shared_rc, active.into(), created, done),
                    None => done(Err("unexpected reply".into())),
                }
            }
            Err(e) => done(Err(e)),
        },
    );
}

pub fn disconnect<F>(shared: &Shared, id: &str, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    if !shared.network.devices.borrow().contains_key(id) {
        return done(Err(format!("no device {id}")));
    }

    call(shared, id, DEVICE_IFACE, "Disconnect", None, |res| {
        done(res.map(|_| ()))
    });
}

pub fn forget<F>(shared: &Shared, ssid: &str, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    let profiles = shared
        .network
        .profiles
        .borrow()
        .iter()
        .filter(|(_, p)| p.ssid.as_deref() == Some(ssid))
        .map(|(path, _)| path.clone())
        .collect::<Vec<_>>();

    if profiles.is_empty() {
        return done(Err(format!("no saved network {ssid}")));
    }

    for path in profiles {
        call(shared, &path, PROFILE_IFACE, "Delete", None, |res| {
            if let Err(e) = res {
                warn!("network: failed to forget: {e}");
            }
        });
    }

    done(Ok(()));
}
