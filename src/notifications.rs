use std::cell::Cell;
use std::cell::RefCell;
use std::rc::Rc;

use gio::glib::variant::ToVariant;
use gio::glib::{self};
use serde::Serialize;
use traccia::info;
use traccia::warn;

use crate::Shared;

const XML: &str = r#"
<node>
    <interface name="org.freedesktop.Notifications">
        <method name="Notify">
            <arg name="app_name" type="s" direction="in"/>
            <arg name="replaces_id" type="u" direction="in"/>
            <arg name="app_icon" type="s" direction="in"/>
            <arg name="summary" type="s" direction="in"/>
            <arg name="body" type="s" direction="in"/>
            <arg name="actions" type="as" direction="in"/>
            <arg name="hints" type="a{sv}" direction="in"/>
            <arg name="expire_timeout" type="i" direction="in"/>
            <arg name="id" type="u" direction="out"/>
        </method>
        <method name="CloseNotification">
            <arg name="id" type="u" direction="in"/>
        </method>
        <method name="GetCapabilities">
            <arg name="capabilities" type="as" direction="out"/>
        </method>
        <method name="GetServerInformation">
            <arg name="name" type="s" direction="out"/>
            <arg name="vendor" type="s" direction="out"/>
            <arg name="version" type="s" direction="out"/>
            <arg name="spec_version" type="s" direction="out"/>
        </method>
        <signal name="NotificationClosed">
            <arg name="id" type="u"/>
            <arg name="reason" type="u"/>
        </signal>
        <signal name="ActionInvoked">
            <arg name="id" type="u"/>
            <arg name="action_key" type="s"/>
        </signal>
        <signal name="ActivationToken">
            <arg name="id" type="u"/>
            <arg name="activation_token" type="s"/>
        </signal>
    </interface>
</node>
"#;

#[derive(Serialize)]
#[derive(Clone)]
pub struct Action {
    pub key: String,
    pub label: String,
}

#[derive(Serialize)]
#[derive(Clone)]
pub struct Notification {
    pub id: u32,
    pub app: String,
    pub icon: String,
    pub summary: String,
    pub body: String,
    pub actions: Vec<Action>,
    pub urgency: u8,
    pub time: i64,
    pub timeout: i32,
}

#[derive(Default)]
pub struct Notifications {
    list: RefCell<Vec<Notification>>,
    next: Cell<u32>,
    conn: RefCell<Option<gio::DBusConnection>>,
}

impl Notifications {
    fn emit(&self, signal: &str, args: glib::Variant) {
        if let Some(conn) = self.conn.borrow().as_ref() {
            let _ = conn.emit_signal(
                None,
                "/org/freedesktop/Notifications",
                "org.freedesktop.Notifications",
                signal,
                Some(&args),
            );
        }
    }

    pub fn add(&self, mut n: Notification, replaces: u32) -> u32 {
        let mut list = self.list.borrow_mut();

        if replaces != 0
            && let Some(slot) = list.iter_mut().find(|x| x.id == replaces)
        {
            n.id = replaces;
            *slot = n;
            return replaces;
        }

        let id = self.next.get().wrapping_add(1).max(1);
        self.next.set(id);

        n.id = id;
        list.push(n);

        id
    }

    pub fn remove(&self, id: u32) -> bool {
        let mut list = self.list.borrow_mut();
        let len = list.len();

        list.retain(|n| n.id != id);
        list.len() != len
    }

    pub fn to_json(&self) -> String {
        serde_json::to_string(&*self.list.borrow()).unwrap_or_else(|_| "[]".into())
    }
}

fn publish(shared: &Shared) {
    let _ = shared.set_state("notifications".into(), shared.notifications.to_json(), None);
}

pub fn close(shared: &Shared, id: u32, reason: u32) {
    if shared.notifications.remove(id) {
        publish(shared);
        shared
            .notifications
            .emit("NotificationClosed", (id, reason).to_variant());
    }
}

pub fn invoke(shared: &Shared, id: u32, action: &str) {
    shared
        .notifications
        .emit("ActionInvoked", (id, action).to_variant());
    close(shared, id, 2);
}

pub fn register(conn: gio::DBusConnection, shared: Rc<Shared>) {
    let node = gio::DBusNodeInfo::for_xml(XML).unwrap();
    let iface = node
        .lookup_interface("org.freedesktop.Notifications")
        .unwrap();

    shared.notifications.conn.replace(Some(conn.clone()));

    let _ =
        conn.register_object("/org/freedesktop/Notifications", &iface)
            .method_call(
                move |_, _, _, _, method, params, invocation| match method {
                    "Notify" => {
                        let (app, replaces, icon, summary, body, actions, hints, timeout) = params
                            .get::<(
                                String,
                                u32,
                                String,
                                String,
                                String,
                                Vec<String>,
                                glib::VariantDict,
                                i32,
                            )>()
                            .unwrap();

                        let actions = actions
                            .chunks_exact(2)
                            .map(|c| Action {
                                key: c[0].clone(),
                                label: c[1].clone(),
                            })
                            .collect();

                        let urgency = hints.lookup::<u8>("urgency").ok().flatten().unwrap_or(1);
                        let id = shared.notifications.add(
                            Notification {
                                id: 0,
                                app,
                                icon,
                                summary,
                                body,
                                actions,
                                urgency,
                                time: glib::real_time() / 1000,
                                timeout,
                            },
                            replaces,
                        );

                        publish(&shared);

                        invocation.return_value(Some(&(id,).to_variant()));
                    }
                    "CloseNotification" => {
                        let (id,) = params.get::<(u32,)>().unwrap();
                        close(&shared, id, 3);
                        invocation.return_value(None);
                    }
                    "GetCapabilities" => invocation.return_value(Some(
                        &(vec!["body", "actions", "body-markup", "body-hyperlinks", "persistence"],).to_variant(),
                    )),
                    "GetServerInformation" => invocation
                        .return_value(Some(&("wwwidgets", "svscagn", "0.1.0", "1.2").to_variant())),
                    _ => invocation
                        .return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method),
                },
            )
            .build();
}

pub fn serve(shared: Rc<Shared>) {
    gio::bus_own_name(
        gio::BusType::Session,
        "org.freedesktop.Notifications",
        gio::BusNameOwnerFlags::NONE,
        move |conn, _| register(conn, Rc::clone(&shared)),
        |_, _| info!("Notification server running"),
        |_, _| warn!("Could not own org.freedesktop.Notifications, is another daemon running?"),
    );
}
