use std::cell::RefCell;
use std::collections::BTreeMap;
use std::rc::Rc;

use gio::glib::variant::FromVariant;
use gio::glib::variant::ObjectPath;
use gio::glib::variant::ToVariant;
use gio::glib::{self};
use serde::Serialize;
use traccia::info;
use traccia::warn;

use crate::Shared;

const BLUEZ: &str = "org.bluez";
const ADAPTER_IFACE: &str = "org.bluez.Adapter1";
const DEVICE_IFACE: &str = "org.bluez.Device1";
const BATTERY_IFACE: &str = "org.bluez.Battery1";
const PROPS_IFACE: &str = "org.freedesktop.DBus.Properties";
const MANAGER_IFACE: &str = "org.freedesktop.DBus.ObjectManager";
const AGENT_MANAGER_IFACE: &str = "org.bluez.AgentManager1";
const AGENT_IFACE: &str = "org.bluez.Agent1";
const AGENT_PATH: &str = "/dev/svscagn/wwwidgets/agent";
const AGENT_CAPABILITY: &str = "KeyboardDisplay";

const REJECTED: &str = "org.bluez.Error.Rejected";
const CANCELED: &str = "org.bluez.Error.Canceled";

const PAIR_TIMEOUT: i32 = 60_000;

const AGENT_XML: &str = r#"
<node>
    <interface name="org.bluez.Agent1">
        <method name="Release"/>
        <method name="RequestPinCode">
            <arg name="device" type="o" direction="in"/>
            <arg name="pincode" type="s" direction="out"/>
        </method>
        <method name="DisplayPinCode">
            <arg name="device" type="o" direction="in"/>
            <arg name="pincode" type="s" direction="in"/>
        </method>
        <method name="RequestPasskey">
            <arg name="device" type="o" direction="in"/>
            <arg name="passkey" type="u" direction="out"/>
        </method>
        <method name="DisplayPasskey">
            <arg name="device" type="o" direction="in"/>
            <arg name="passkey" type="u" direction="in"/>
            <arg name="entered" type="q" direction="in"/>
        </method>
        <method name="RequestConfirmation">
            <arg name="device" type="o" direction="in"/>
            <arg name="passkey" type="u" direction="in"/>
        </method>
        <method name="RequestAuthorization">
            <arg name="device" type="o" direction="in"/>
        </method>
        <method name="AuthorizeService">
            <arg name="device" type="o" direction="in"/>
            <arg name="uuid" type="s" direction="in"/>
        </method>
        <method name="Cancel"/>
    </interface>
</node>
"#;

fn get<T: FromVariant>(props: &glib::VariantDict, key: &str) -> Option<T> {
    props.lookup::<T>(key).ok().flatten()
}

fn set<T: FromVariant>(props: &glib::VariantDict, key: &str, field: &mut T) {
    if let Some(v) = get(props, key) {
        *field = v;
    }
}

#[derive(Serialize)]
#[derive(Default)]
#[derive(Debug, Clone)]
#[serde(rename_all = "camelCase")]
struct Adapter {
    id: String,
    name: String,
    address: String,
    powered: bool,
    discovering: bool,
}

impl Adapter {
    fn update(&mut self, props: &glib::VariantDict) {
        set(props, "Alias", &mut self.name);
        set(props, "Address", &mut self.address);
        set(props, "Powered", &mut self.powered);
        set(props, "Discovering", &mut self.discovering);
    }
}

#[derive(Serialize)]
#[derive(Default)]
#[derive(Debug, Clone)]
#[serde(rename_all = "camelCase")]
struct Device {
    id: String,
    adapter: String,
    name: String,
    address: String,
    icon: Option<String>,
    paired: bool,
    trusted: bool,
    connected: bool,
    battery: Option<u8>,
}

impl Device {
    fn update(&mut self, iface: &str, props: &glib::VariantDict) {
        match iface {
            DEVICE_IFACE => {
                if let Some(adapter) = get::<ObjectPath>(props, "Adapter") {
                    self.adapter = adapter.as_str().into();
                }

                set(props, "Alias", &mut self.name);
                set(props, "Address", &mut self.address);
                set(props, "Paired", &mut self.paired);
                set(props, "Trusted", &mut self.trusted);
                set(props, "Connected", &mut self.connected);

                if let Some(icon) = get(props, "Icon") {
                    self.icon = Some(icon);
                }
            }
            BATTERY_IFACE => {
                if let Some(percentage) = get(props, "Percentage") {
                    self.battery = Some(percentage);
                }
            }
            _ => {}
        }
    }
}

#[derive(Serialize)]
#[derive(Debug, Clone, Copy, PartialEq)]
#[serde(rename_all = "lowercase")]
enum RequestKind {
    Pin,
    Passkey,
    Confirm,
    Authorize,
    Display,
}

#[derive(Serialize)]
#[derive(Debug, Clone)]
#[serde(rename_all = "camelCase")]
struct Request {
    device: String,
    name: Option<String>,
    kind: RequestKind,
    code: Option<String>,
    entered: Option<u16>,
    service: Option<String>,
}

struct Pending {
    request: Request,
    invocation: Option<gio::DBusMethodInvocation>,
}

#[derive(Serialize)]
struct Snapshot<'a> {
    adapters: Vec<&'a Adapter>,
    devices: Vec<&'a Device>,
    request: Option<&'a Request>,
}

#[derive(Default)]
pub struct Bluetooth {
    conn: RefCell<Option<gio::DBusConnection>>,
    adapters: RefCell<BTreeMap<String, Adapter>>,
    devices: RefCell<BTreeMap<String, Device>>,
    signals: RefCell<Vec<gio::SignalSubscription>>,
    owner: RefCell<Option<String>>,
    agent: RefCell<Option<gio::RegistrationId>>,
    pending: RefCell<Option<Pending>>,
}

impl Bluetooth {
    fn to_json(&self) -> String {
        let adapters = self.adapters.borrow();
        let devices = self.devices.borrow();
        let pending = self.pending.borrow();
        let snapshot = Snapshot {
            adapters: adapters.values().collect(),
            devices: devices.values().collect(),
            request: pending.as_ref().map(|p| &p.request),
        };

        serde_json::to_string(&snapshot).unwrap_or_else(|_| "{}".into())
    }

    fn add(&self, path: &str, ifaces: &glib::Variant) {
        for entry in ifaces.iter() {
            let Some(iface) = entry.child_value(0).str().map(String::from) else {
                continue;
            };

            let props = glib::VariantDict::new(Some(&entry.child_value(1)));

            match iface.as_str() {
                ADAPTER_IFACE => self
                    .adapters
                    .borrow_mut()
                    .entry(path.into())
                    .or_insert_with(|| Adapter {
                        id: path.into(),
                        ..Default::default()
                    })
                    .update(&props),
                DEVICE_IFACE | BATTERY_IFACE => self
                    .devices
                    .borrow_mut()
                    .entry(path.into())
                    .or_insert_with(|| Device {
                        id: path.into(),
                        ..Default::default()
                    })
                    .update(&iface, &props),
                _ => {}
            }
        }
    }

    fn remove(&self, path: &str, ifaces: &[String]) {
        for iface in ifaces {
            match iface.as_str() {
                ADAPTER_IFACE => _ = self.adapters.borrow_mut().remove(path),
                DEVICE_IFACE => _ = self.devices.borrow_mut().remove(path),
                BATTERY_IFACE => {
                    if let Some(device) = self.devices.borrow_mut().get_mut(path) {
                        device.battery = None;
                    }
                }
                _ => {}
            }
        }
    }

    fn changed(&self, path: &str, iface: &str, props: &glib::VariantDict) {
        match iface {
            ADAPTER_IFACE => {
                if let Some(adapter) = self.adapters.borrow_mut().get_mut(path) {
                    adapter.update(props);
                }
            }
            DEVICE_IFACE | BATTERY_IFACE => {
                let paired = match self.devices.borrow_mut().get_mut(path) {
                    Some(device) => {
                        device.update(iface, props);
                        device.paired
                    }
                    None => false,
                };

                if paired {
                    self.settle(path);
                }
            }
            _ => {}
        }
    }

    fn settle(&self, path: &str) {
        let mut pending = self.pending.borrow_mut();

        if pending
            .as_ref()
            .is_some_and(|p| p.request.device == path && p.invocation.is_none())
        {
            pending.take();
        }
    }

    fn name_of(&self, path: &str) -> Option<String> {
        self.devices.borrow().get(path).map(|d| d.name.clone())
    }

    fn clear(&self) {
        self.conn.take();
        self.owner.take();
        self.pending.take();
        self.signals.borrow_mut().clear();
        self.adapters.borrow_mut().clear();
        self.devices.borrow_mut().clear();
    }
}

fn publish(shared: &Shared) {
    let _ = shared.set_state("bluetooth".into(), shared.bluetooth.to_json(), None);
}

fn load(shared: &Rc<Shared>, conn: &gio::DBusConnection) {
    let shared = Rc::clone(shared);

    conn.call(
        Some(BLUEZ),
        "/",
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
                    warn!("bluetooth: failed to list objects: {e}");
                    return;
                }
            };

            for object in objects.iter() {
                if let Some(path) = object.child_value(0).str() {
                    shared.bluetooth.add(path, &object.child_value(1));
                }
            }

            publish(&shared);
        },
    );
}

fn subscribe(shared: &Rc<Shared>, conn: &gio::DBusConnection) {
    let added = conn.subscribe_to_signal(
        Some(BLUEZ),
        Some(MANAGER_IFACE),
        Some("InterfacesAdded"),
        None,
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            move |signal| {
                if let Some(path) = signal.parameters.child_value(0).str() {
                    shared
                        .bluetooth
                        .add(path, &signal.parameters.child_value(1));
                    publish(&shared);
                }
            }
        },
    );

    let removed = conn.subscribe_to_signal(
        Some(BLUEZ),
        Some(MANAGER_IFACE),
        Some("InterfacesRemoved"),
        None,
        None,
        gio::DBusSignalFlags::NONE,
        {
            let shared = Rc::clone(shared);
            move |signal| {
                if let Some((path, ifaces)) = signal.parameters.get::<(ObjectPath, Vec<String>)>() {
                    shared.bluetooth.remove(path.as_str(), &ifaces);
                    publish(&shared);
                }
            }
        },
    );

    let changed = conn.subscribe_to_signal(
        Some(BLUEZ),
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

                shared.bluetooth.changed(signal.object_path, &iface, &props);
                publish(&shared);
            }
        },
    );

    shared
        .bluetooth
        .signals
        .replace(vec![added, removed, changed]);
}

fn ask(shared: &Shared, request: Request, invocation: Option<gio::DBusMethodInvocation>) {
    let old = shared.bluetooth.pending.replace(Some(Pending {
        request,
        invocation,
    }));

    if let Some(invocation) = old.and_then(|p| p.invocation) {
        invocation.return_dbus_error(CANCELED, "superseded by a new request");
    }

    publish(shared);
}

fn dismiss(shared: &Shared) {
    let old = shared.bluetooth.pending.take();

    if let Some(invocation) = old.and_then(|p| p.invocation) {
        invocation.return_dbus_error(CANCELED, "canceled");
    }

    publish(shared);
}

fn agent_call(
    shared: &Shared,
    sender: Option<&str>,
    method: &str,
    params: &glib::Variant,
    invocation: gio::DBusMethodInvocation,
) {
    if sender.is_none() || shared.bluetooth.owner.borrow().as_deref() != sender {
        invocation.return_dbus_error(REJECTED, "not bluez");
        return;
    }

    let arg = |i: usize| params.try_child_value(i);
    let device = arg(0)
        .and_then(|v| v.str().map(String::from))
        .unwrap_or_default();
    let request = |kind| Request {
        name: shared.bluetooth.name_of(&device),
        device: device.clone(),
        kind,
        code: None,
        entered: None,
        service: None,
    };
    let passkey = || {
        arg(1)
            .and_then(|v| v.get::<u32>())
            .map(|p| format!("{p:06}"))
    };

    match method {
        "RequestPinCode" => ask(shared, request(RequestKind::Pin), Some(invocation)),
        "RequestPasskey" => ask(shared, request(RequestKind::Passkey), Some(invocation)),
        "RequestConfirmation" => ask(
            shared,
            Request {
                code: passkey(),
                ..request(RequestKind::Confirm)
            },
            Some(invocation),
        ),
        "RequestAuthorization" => ask(shared, request(RequestKind::Authorize), Some(invocation)),
        "AuthorizeService" => ask(
            shared,
            Request {
                service: arg(1).and_then(|v| v.str().map(String::from)),
                ..request(RequestKind::Authorize)
            },
            Some(invocation),
        ),
        "DisplayPasskey" => {
            invocation.return_value(None);
            ask(
                shared,
                Request {
                    code: passkey(),
                    entered: arg(2).and_then(|v| v.get::<u16>()),
                    ..request(RequestKind::Display)
                },
                None,
            );
        }
        "DisplayPinCode" => {
            invocation.return_value(None);
            ask(
                shared,
                Request {
                    code: arg(1).and_then(|v| v.str().map(String::from)),
                    ..request(RequestKind::Display)
                },
                None,
            );
        }
        "Cancel" | "Release" => {
            invocation.return_value(None);
            dismiss(shared);
        }
        _ => invocation.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method),
    }
}

fn register_agent(shared: &Rc<Shared>, conn: &gio::DBusConnection) {
    if shared.bluetooth.agent.borrow().is_none() {
        let node = gio::DBusNodeInfo::for_xml(AGENT_XML).unwrap();
        let iface = node.lookup_interface(AGENT_IFACE).unwrap();

        let registration = conn
            .register_object(AGENT_PATH, &iface)
            .method_call({
                let shared = Rc::clone(shared);
                move |_, sender, _, _, method, params, invocation| {
                    agent_call(&shared, sender, method, &params, invocation)
                }
            })
            .build();

        match registration {
            Ok(id) => _ = shared.bluetooth.agent.replace(Some(id)),
            Err(e) => {
                warn!("bluetooth: failed to export agent: {e}");
                return;
            }
        }
    }

    let path = ObjectPath::try_from(AGENT_PATH.to_string()).unwrap();
    let conn = conn.clone();

    conn.clone().call(
        Some(BLUEZ),
        "/org/bluez",
        AGENT_MANAGER_IFACE,
        "RegisterAgent",
        Some(&(path.clone(), AGENT_CAPABILITY).to_variant()),
        None,
        gio::DBusCallFlags::NONE,
        -1,
        gio::Cancellable::NONE,
        move |res| {
            if let Err(e) = res {
                warn!("bluetooth: failed to register agent: {e}");
                return;
            }

            conn.call(
                Some(BLUEZ),
                "/org/bluez",
                AGENT_MANAGER_IFACE,
                "RequestDefaultAgent",
                Some(&(path,).to_variant()),
                None,
                gio::DBusCallFlags::NONE,
                -1,
                gio::Cancellable::NONE,
                |res| {
                    if let Err(e) = res {
                        warn!("bluetooth: failed to become default agent: {e}");
                    }
                },
            );
        },
    );
}

pub fn respond(shared: &Shared, accept: bool, value: Option<String>) -> Result<(), String> {
    let kind = shared
        .bluetooth
        .pending
        .borrow()
        .as_ref()
        .map(|p| p.request.kind)
        .ok_or("no pending request")?;

    let reply = match (accept, kind) {
        (true, RequestKind::Pin) => {
            let pin = value
                .filter(|v| (1..=16).contains(&v.len()))
                .ok_or("pin must be 1 to 16 characters")?;

            Some((pin,).to_variant())
        }
        (true, RequestKind::Passkey) => {
            let passkey = value
                .and_then(|v| v.trim().parse::<u32>().ok())
                .filter(|v| *v <= 999_999)
                .ok_or("passkey must be a number up to 999999")?;

            Some((passkey,).to_variant())
        }
        _ => None,
    };

    let pending = shared.bluetooth.pending.take();

    if let Some(invocation) = pending.and_then(|p| p.invocation) {
        if accept {
            invocation.return_value(reply.as_ref());
        } else {
            invocation.return_dbus_error(REJECTED, "rejected by user");
        }
    }

    publish(shared);

    Ok(())
}

pub fn serve(shared: Rc<Shared>) {
    gio::bus_watch_name(
        gio::BusType::System,
        BLUEZ,
        gio::BusNameWatcherFlags::NONE,
        {
            let shared = Rc::clone(&shared);
            move |conn, _, owner| {
                info!("Bluetooth service enabled");

                shared.bluetooth.conn.replace(Some(conn.clone()));
                shared.bluetooth.owner.replace(Some(owner.into()));
                subscribe(&shared, &conn);
                register_agent(&shared, &conn);
                load(&shared, &conn);
            }
        },
        move |_, _| {
            warn!("bluetooth: service not running");

            shared.bluetooth.clear();
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
    timeout: i32,
    done: F,
) where
    F: FnOnce(Result<(), String>) + 'static,
{
    let Some(conn) = shared.bluetooth.conn.borrow().clone() else {
        done(Err("bluetooth not available".into()));
        return;
    };

    conn.call(
        Some(BLUEZ),
        path,
        iface,
        method,
        args.as_ref(),
        None,
        gio::DBusCallFlags::NONE,
        timeout,
        gio::Cancellable::NONE,
        move |res| done(res.map(|_| ()).map_err(|e| e.to_string())),
    );
}

fn set_property<F>(shared: &Shared, path: &str, iface: &str, name: &str, value: bool, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    let args = (iface, name, value.to_variant()).to_variant();
    call(shared, path, PROPS_IFACE, "Set", Some(args), -1, done);
}

fn has_adapter(shared: &Shared, id: &str) -> bool {
    shared.bluetooth.adapters.borrow().contains_key(id)
}

fn has_device(shared: &Shared, id: &str) -> bool {
    shared.bluetooth.devices.borrow().contains_key(id)
}

pub fn set_powered<F>(shared: &Shared, id: &str, on: bool, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    if !has_adapter(shared, id) {
        return done(Err(format!("no adapter {id}")));
    }

    set_property(shared, id, ADAPTER_IFACE, "Powered", on, done);
}

pub fn set_discovering<F>(shared: &Shared, id: &str, on: bool, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    if !has_adapter(shared, id) {
        return done(Err(format!("no adapter {id}")));
    }

    let method = if on {
        "StartDiscovery"
    } else {
        "StopDiscovery"
    };
    call(shared, id, ADAPTER_IFACE, method, None, -1, done);
}

pub fn connect<F>(shared: &Shared, id: &str, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    if !has_device(shared, id) {
        return done(Err(format!("no device {id}")));
    }

    call(shared, id, DEVICE_IFACE, "Connect", None, -1, done);
}

pub fn disconnect<F>(shared: &Shared, id: &str, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    if !has_device(shared, id) {
        return done(Err(format!("no device {id}")));
    }

    call(shared, id, DEVICE_IFACE, "Disconnect", None, -1, done);
}

pub fn pair<F>(shared: &Rc<Shared>, id: &str, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    if !has_device(shared, id) {
        return done(Err(format!("no device {id}")));
    }

    let trust = {
        let shared = Rc::clone(shared);
        let id = id.to_string();

        move |res: Result<(), String>| match res {
            Ok(()) => set_property(&shared, &id, DEVICE_IFACE, "Trusted", true, done),
            Err(e) => done(Err(e)),
        }
    };

    call(shared, id, DEVICE_IFACE, "Pair", None, PAIR_TIMEOUT, trust);
}

pub fn forget<F>(shared: &Shared, id: &str, done: F)
where
    F: FnOnce(Result<(), String>) + 'static,
{
    let adapter = shared
        .bluetooth
        .devices
        .borrow()
        .get(id)
        .map(|d| d.adapter.clone());

    let Some(adapter) = adapter else {
        return done(Err(format!("no device {id}")));
    };

    let Ok(path) = ObjectPath::try_from(id.to_string()) else {
        return done(Err(format!("invalid device {id}")));
    };

    call(
        shared,
        &adapter,
        ADAPTER_IFACE,
        "RemoveDevice",
        Some((path,).to_variant()),
        -1,
        done,
    );
}
