use std::cell::RefCell;
use std::collections::HashMap;
use std::io;
use std::os::fd::AsRawFd;
use std::rc::Rc;

use gio::glib;
use serde::Serialize;
use traccia::error;
use traccia::warn;
use wayland_client::Connection;
use wayland_client::Dispatch;
use wayland_client::EventQueue;
use wayland_client::Proxy;
use wayland_client::QueueHandle;
use wayland_client::WEnum;
use wayland_client::backend::ObjectId;
use wayland_client::backend::WaylandError;
use wayland_client::event_created_child;
use wayland_client::globals::GlobalListContents;
use wayland_client::globals::registry_queue_init;
use wayland_client::protocol::wl_output;
use wayland_client::protocol::wl_output::WlOutput;
use wayland_client::protocol::wl_registry;
use wayland_client::protocol::wl_registry::WlRegistry;
use wayland_protocols::ext::workspace::v1::client::ext_workspace_group_handle_v1;
use wayland_protocols::ext::workspace::v1::client::ext_workspace_group_handle_v1::ExtWorkspaceGroupHandleV1;
use wayland_protocols::ext::workspace::v1::client::ext_workspace_handle_v1;
use wayland_protocols::ext::workspace::v1::client::ext_workspace_handle_v1::ExtWorkspaceHandleV1;
use wayland_protocols::ext::workspace::v1::client::ext_workspace_manager_v1;
use wayland_protocols::ext::workspace::v1::client::ext_workspace_manager_v1::ExtWorkspaceManagerV1;

use crate::Shared;

#[derive(Serialize)]
#[derive(Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Workspace {
    pub id: String,
    pub name: String,
    pub coordinates: Vec<u32>,
    pub outputs: Vec<String>,
    pub active: bool,
    pub urgent: bool,
    pub hidden: bool,
    pub can_activate: bool,
}

struct WorkspaceData {
    handle: ExtWorkspaceHandleV1,
    protocol_id: Option<String>,
    name: String,
    coordinates: Vec<u32>,
    active: bool,
    urgent: bool,
    hidden: bool,
    can_activate: bool,
}

impl WorkspaceData {
    fn new(handle: ExtWorkspaceHandleV1) -> Self {
        Self {
            handle,
            protocol_id: None,
            name: String::new(),
            coordinates: Vec::new(),
            active: false,
            urgent: false,
            hidden: false,
            can_activate: false,
        }
    }

    fn public_id(&self) -> String {
        match &self.protocol_id {
            Some(id) => id.clone(),
            None => self.handle.id().protocol_id().to_string(),
        }
    }
}

#[derive(Default)]
struct Group {
    outputs: Vec<ObjectId>,
    workspaces: Vec<ObjectId>,
}

type OnChange = Box<dyn FnMut(Vec<Workspace>)>;

struct State {
    manager: ExtWorkspaceManagerV1,
    output_globals: HashMap<u32, WlOutput>,
    outputs: HashMap<ObjectId, Option<String>>,
    groups: HashMap<ObjectId, Group>,
    workspaces: HashMap<ObjectId, WorkspaceData>,
    on_change: OnChange,
    finished: bool,
}

impl State {
    fn bind_output(
        &mut self,
        registry: &WlRegistry,
        name: u32,
        version: u32,
        qh: &QueueHandle<Self>,
    ) {
        let output = registry.bind::<WlOutput, _, _>(name, version, qh, ());

        self.outputs.insert(output.id(), None);
        self.output_globals.insert(name, output);
    }

    fn snapshot(&self) -> Vec<Workspace> {
        let mut list = self
            .workspaces
            .iter()
            .map(|(oid, ws)| {
                let outputs = self
                    .groups
                    .values()
                    .filter(|g| g.workspaces.contains(oid))
                    .flat_map(|g| g.outputs.iter())
                    .filter_map(|o| self.outputs.get(o).cloned().flatten())
                    .collect();

                Workspace {
                    id: ws.public_id(),
                    name: ws.name.clone(),
                    coordinates: ws.coordinates.clone(),
                    outputs,
                    active: ws.active,
                    urgent: ws.urgent,
                    hidden: ws.hidden,
                    can_activate: ws.can_activate,
                }
            })
            .collect::<Vec<_>>();

        list.sort_by(|a, b| {
            a.coordinates
                .cmp(&b.coordinates)
                .then_with(|| a.name.cmp(&b.name))
        });

        list
    }

    fn activate(&self, id: &str) -> Result<(), String> {
        let ws = self
            .workspaces
            .values()
            .find(|w| w.public_id() == id)
            .ok_or_else(|| format!("no workspace with id {id}"))?;

        if !ws.can_activate {
            return Err(format!("workspace {id} cannot be activated"));
        }

        ws.handle.activate();
        self.manager.commit();

        Ok(())
    }
}

impl Dispatch<WlRegistry, GlobalListContents> for State {
    fn event(
        state: &mut Self,
        proxy: &WlRegistry,
        event: <WlRegistry as Proxy>::Event,
        _: &GlobalListContents,
        _: &wayland_client::Connection,
        qhandle: &QueueHandle<Self>,
    ) {
        match event {
            wl_registry::Event::Global {
                name,
                interface,
                version,
            } if interface == WlOutput::interface().name => {
                state.bind_output(proxy, name, version.min(4), qhandle);
            }
            wl_registry::Event::GlobalRemove { name } => {
                if let Some(output) = state.output_globals.remove(&name) {
                    state.outputs.remove(&output.id());

                    if output.version() >= 3 {
                        output.release();
                    }
                }
            }
            _ => {}
        }
    }
}

impl Dispatch<WlOutput, ()> for State {
    fn event(
        state: &mut Self,
        proxy: &WlOutput,
        event: <WlOutput as Proxy>::Event,
        _: &(),
        _: &wayland_client::Connection,
        _: &QueueHandle<Self>,
    ) {
        if let wl_output::Event::Name { name } = event {
            state.outputs.insert(proxy.id(), Some(name));
        }
    }
}

impl Dispatch<ExtWorkspaceManagerV1, ()> for State {
    fn event(
        state: &mut Self,
        _: &ExtWorkspaceManagerV1,
        event: <ExtWorkspaceManagerV1 as Proxy>::Event,
        _: &(),
        _: &wayland_client::Connection,
        _: &QueueHandle<Self>,
    ) {
        use ext_workspace_manager_v1::Event;

        match event {
            Event::WorkspaceGroup { workspace_group } => {
                state.groups.insert(workspace_group.id(), Group::default());
            }
            Event::Workspace { workspace } => {
                state
                    .workspaces
                    .insert(workspace.id(), WorkspaceData::new(workspace));
            }
            Event::Done => {
                let snapshot = state.snapshot();
                (state.on_change)(snapshot)
            }
            Event::Finished => state.finished = true,
            _ => {}
        }
    }

    // the manager's events create new group and workspace objects
    event_created_child!(State, ExtWorkspaceManagerV1, [
        ext_workspace_manager_v1::EVT_WORKSPACE_GROUP_OPCODE => (ExtWorkspaceGroupHandleV1, ()),
        ext_workspace_manager_v1::EVT_WORKSPACE_OPCODE => (ExtWorkspaceHandleV1, ()),
    ]);
}

impl Dispatch<ExtWorkspaceGroupHandleV1, ()> for State {
    fn event(
        state: &mut Self,
        proxy: &ExtWorkspaceGroupHandleV1,
        event: <ExtWorkspaceGroupHandleV1 as Proxy>::Event,
        _: &(),
        _: &wayland_client::Connection,
        _: &QueueHandle<Self>,
    ) {
        use ext_workspace_group_handle_v1::Event;

        if let Event::Removed = event {
            state.groups.remove(&proxy.id());
            proxy.destroy();
            return;
        }

        let Some(g) = state.groups.get_mut(&proxy.id()) else {
            return;
        };

        match event {
            Event::OutputEnter { output } => g.outputs.push(output.id()),
            Event::OutputLeave { output } => g.outputs.retain(|o| *o != output.id()),
            Event::WorkspaceEnter { workspace } => g.workspaces.push(workspace.id()),
            Event::WorkspaceLeave { workspace } => g.workspaces.retain(|w| *w != workspace.id()),
            _ => {}
        }
    }
}

impl Dispatch<ExtWorkspaceHandleV1, ()> for State {
    fn event(
        state: &mut Self,
        proxy: &ExtWorkspaceHandleV1,
        event: <ExtWorkspaceHandleV1 as Proxy>::Event,
        _: &(),
        _: &wayland_client::Connection,
        _: &QueueHandle<Self>,
    ) {
        use ext_workspace_handle_v1::Event;
        use ext_workspace_handle_v1::State as Flags;
        use ext_workspace_handle_v1::WorkspaceCapabilities as Caps;

        if let Event::Removed = event {
            state.workspaces.remove(&proxy.id());

            for g in state.groups.values_mut() {
                g.workspaces.retain(|w| *w != proxy.id());
            }

            proxy.destroy();
            return;
        }

        let Some(ws) = state.workspaces.get_mut(&proxy.id()) else {
            return;
        };

        match event {
            Event::Id { id } => ws.protocol_id = Some(id),
            Event::Name { name } => ws.name = name,
            Event::Coordinates { coordinates } => {
                ws.coordinates = coordinates
                    .chunks_exact(4)
                    .map(|c| u32::from_ne_bytes([c[0], c[1], c[2], c[3]]))
                    .collect();
            }
            Event::State {
                state: WEnum::Value(flags),
            } => {
                ws.active = flags.contains(Flags::Active);
                ws.urgent = flags.contains(Flags::Urgent);
                ws.hidden = flags.contains(Flags::Hidden);
            }
            Event::Capabilities {
                capabilities: WEnum::Value(caps),
            } => {
                ws.can_activate = caps.contains(Caps::Activate);
            }
            _ => {}
        }
    }
}

struct Inner {
    queue: EventQueue<State>,
    state: State,
}

pub struct ExtWorkspaces {
    conn: Connection,
    inner: Rc<RefCell<Inner>>,
}

impl ExtWorkspaces {
    pub fn connect<F>(on_change: F) -> Option<(Self, glib::SourceId)>
    where
        F: FnMut(Vec<Workspace>) + 'static,
    {
        let conn = Connection::connect_to_env().ok()?;
        let (globals, mut queue) = registry_queue_init::<State>(&conn).ok()?;
        let qh = queue.handle();

        let manager_global = globals.contents().with_list(|list| {
            list.iter()
                .any(|g| g.interface == ExtWorkspaceManagerV1::interface().name)
        });

        if !manager_global {
            return None;
        }

        let output_globals = globals.contents().with_list(|list| {
            list.iter()
                .filter(|g| g.interface == WlOutput::interface().name)
                .map(|g| {
                    let output = globals.registry().bind::<WlOutput, _, _>(
                        g.name,
                        g.version.min(4),
                        &qh,
                        (),
                    );
                    (g.name, output)
                })
                .collect::<HashMap<_, _>>()
        });

        let outputs = output_globals.values().map(|o| (o.id(), None)).collect();
        let manager = globals
            .bind::<ExtWorkspaceManagerV1, _, _>(&qh, 1..=1, ())
            .ok()?;

        let mut state = State {
            manager,
            output_globals,
            outputs,
            groups: HashMap::new(),
            workspaces: HashMap::new(),
            on_change: Box::new(on_change),
            finished: false,
        };

        queue.roundtrip(&mut state).ok()?;

        let inner = Rc::new(RefCell::new(Inner { queue, state }));
        let fd = conn.backend().poll_fd().as_raw_fd();

        let source = glib_unix::unix_fd_add_local(
            fd,
            glib::IOCondition::IN | glib::IOCondition::HUP | glib::IOCondition::ERR,
            {
                let conn = conn.clone();
                let inner = Rc::clone(&inner);

                move |_, _| {
                    let mut inner = inner.borrow_mut();
                    let Inner { queue, state } = &mut *inner;

                    if let Some(guard) = conn.prepare_read() {
                        match guard.read() {
                            Ok(_) => {}
                            Err(WaylandError::Io(e)) if e.kind() == io::ErrorKind::WouldBlock => {}
                            Err(e) => {
                                error!("ext-workspace: connection lost: {e}");
                                return glib::ControlFlow::Break;
                            }
                        }
                    }

                    if let Err(e) = queue.dispatch_pending(state) {
                        error!("ext-workspace: dispatch failed: {e}");
                        return glib::ControlFlow::Break;
                    }

                    let _ = conn.flush();

                    if state.finished {
                        return glib::ControlFlow::Break;
                    }

                    glib::ControlFlow::Continue
                }
            },
        );

        Some((Self { conn, inner }, source))
    }

    pub fn workspaces(&self) -> Vec<Workspace> {
        self.inner.borrow().state.snapshot()
    }

    pub fn activate(&self, id: &str) -> Result<(), String> {
        let inner = self
            .inner
            .try_borrow()
            .map_err(|_| "activate during dispatch".to_string())?;

        inner.state.activate(id)?;
        self.conn.flush().map_err(|e| e.to_string())
    }
}

pub fn serve(shared: Rc<Shared>) {
    let weak = Rc::downgrade(&shared);

    let Some((ws, _source)) = ExtWorkspaces::connect(move |list| {
        let Some(shared) = weak.upgrade() else {
            return;
        };

        if let Ok(json) = serde_json::to_string(&list) {
            let _ = shared.set_state("workspaces".into(), json, None);
        }
    }) else {
        warn!("ext-workspace-v1 is not supported by this compositor");
        return;
    };

    *shared.workspaces.borrow_mut() = Some(ws);
}
