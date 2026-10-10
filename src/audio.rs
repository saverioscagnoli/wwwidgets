use std::cell::RefCell;
use std::collections::BTreeMap;
use std::mem;
use std::rc::Rc;

use libpulse_binding::callbacks::ListResult;
use libpulse_binding::context::Context;
use libpulse_binding::context::FlagSet;
use libpulse_binding::context::State;
use libpulse_binding::context::introspect::Introspector;
use libpulse_binding::context::introspect::SinkInfo;
use libpulse_binding::context::subscribe::Facility;
use libpulse_binding::context::subscribe::InterestMaskSet;
use libpulse_binding::context::subscribe::Operation;
use libpulse_binding::volume::ChannelVolumes;
use libpulse_binding::volume::Volume;
use libpulse_glib_binding::Mainloop;
use serde::Serialize;
use traccia::error;

use traccia::warn;

use crate::Shared;

const MAX_VOLUME: f64 = 1.5; // * 100

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Sink {
    id: u32,
    name: String,
    description: String,
    volume: f64,
    muted: bool,
    #[serde(skip)]
    channels: ChannelVolumes,
}

impl From<&SinkInfo<'_>> for Sink {
    fn from(info: &SinkInfo<'_>) -> Self {
        Self {
            id: info.index,
            name: info.name.as_deref().unwrap_or_default().into(),
            description: info.description.as_deref().unwrap_or_default().into(),
            volume: info.volume.avg().0 as f64 / Volume::NORMAL.0 as f64,
            muted: info.mute,
            channels: info.volume,
        }
    }
}

#[derive(Serialize)]
struct Snapshot<'a> {
    default: Option<u32>,
    sinks: Vec<&'a Sink>,
}

#[derive(Default)]
#[derive(Serialize)]
struct Inner {
    default: Option<String>,
    sinks: BTreeMap<u32, Sink>,
}

type Introspect = Rc<RefCell<Introspector>>;

pub struct Audio {
    _mainloop: Mainloop,
    _context: Rc<RefCell<Context>>,
    introspect: Introspect,
    inner: Rc<RefCell<Inner>>,
}

impl Audio {
    pub fn set_volume(&self, id: u32, volume: f64) -> Result<(), String> {
        let mut channels = self
            .inner
            .borrow()
            .sinks
            .get(&id)
            .map(|s| s.channels)
            .ok_or_else(|| format!("no sink with id {id}"))?;

        let target = volume.clamp(0.0, MAX_VOLUME) * Volume::NORMAL.0 as f64;

        channels.scale(Volume(target.round() as u32));
        self.introspect
            .borrow_mut()
            .set_sink_volume_by_index(id, &channels, None);

        Ok(())
    }

    pub fn set_mute(&self, id: u32, muted: bool) -> Result<(), String> {
        if !self.inner.borrow().sinks.contains_key(&id) {
            return Err(format!("no sink with id {id}"));
        }

        self.introspect
            .borrow_mut()
            .set_sink_mute_by_index(id, muted, None);

        Ok(())
    }
}

fn publish(shared: &Shared, inner: &RefCell<Inner>) {
    let inner = inner.borrow();
    let snapshot = Snapshot {
        default: inner
            .sinks
            .values()
            .find(|s| inner.default.as_ref() == Some(&s.name))
            .map(|s| s.id),
        sinks: inner.sinks.values().collect(),
    };

    match serde_json::to_string(&snapshot) {
        Ok(json) => {
            let _ = shared.set_state("audio".into(), json, None);
        }
        Err(e) => error!("audio: {e}"),
    }
}

fn load_server(introspect: &Introspect, inner: &Rc<RefCell<Inner>>, shared: &Rc<Shared>) {
    let inner = Rc::clone(inner);
    let shared = Rc::clone(shared);

    introspect.borrow().get_server_info(move |info| {
        inner.borrow_mut().default = info.default_sink_name.as_deref().map(Into::into);
        publish(&shared, &inner)
    });
}

fn load_sinks(introspect: &Introspect, inner: &Rc<RefCell<Inner>>, shared: &Rc<Shared>) {
    let inner = Rc::clone(inner);
    let shared = Rc::clone(shared);

    let mut sinks = BTreeMap::new();

    introspect
        .borrow()
        .get_sink_info_list(move |res| match res {
            ListResult::Item(info) => {
                sinks.insert(info.index, Sink::from(info));
            }
            ListResult::End => {
                inner.borrow_mut().sinks = mem::take(&mut sinks);
                publish(&shared, &inner);
            }
            ListResult::Error => warn!("audio: failed to list sinks"),
        });
}

fn load_sink(introspect: &Introspect, id: u32, inner: &Rc<RefCell<Inner>>, shared: &Rc<Shared>) {
    let inner = Rc::clone(inner);
    let shared = Rc::clone(shared);

    introspect.borrow().get_sink_info_by_index(id, move |res| {
        if let ListResult::Item(info) = res {
            inner
                .borrow_mut()
                .sinks
                .insert(info.index, Sink::from(info));
            publish(&shared, &inner);
        }
    });
}

fn connect(shared: &Rc<Shared>) -> Result<Audio, String> {
    let mainloop = Mainloop::new(None).ok_or("failed to create mainloop")?;
    let mut context = Context::new(&mainloop, "wwwidgets").ok_or("failed to create context")?;
    let introspect: Introspect = Rc::new(RefCell::new(context.introspect()));
    let inner = Rc::<RefCell<Inner>>::default();

    {
        let introspect = Rc::clone(&introspect);
        let inner = Rc::clone(&inner);
        let shared = Rc::clone(shared);

        context.set_subscribe_callback(Some(Box::new(move |facility, op, id| {
            match (facility, op) {
                (Some(Facility::Sink), Some(Operation::Removed)) => {
                    inner.borrow_mut().sinks.remove(&id);
                    publish(&shared, &inner);
                }
                (Some(Facility::Sink), _) => load_sink(&introspect, id, &inner, &shared),
                (Some(Facility::Server), _) => load_server(&introspect, &inner, &shared),
                _ => {}
            }
        })));
    }

    let context = Rc::new(RefCell::new(context));

    {
        let weak = Rc::downgrade(&context);
        let introspect = Rc::clone(&introspect);
        let inner = Rc::clone(&inner);
        let shared = Rc::clone(shared);

        context
            .borrow_mut()
            .set_state_callback(Some(Box::new(move || {
                let Some(context) = weak.upgrade() else {
                    return;
                };

                let Ok(mut context) = context.try_borrow_mut() else {
                    return;
                };

                match context.get_state() {
                    State::Ready => {
                        context.subscribe(InterestMaskSet::SINK | InterestMaskSet::SERVER, |_| {});
                        load_server(&introspect, &inner, &shared);
                        load_sinks(&introspect, &inner, &shared);
                    }
                    State::Failed | State::Terminated => {
                        error!("audio: lost connection to sound server")
                    }
                    _ => {}
                }
            })));
    }

    context
        .borrow_mut()
        .connect(None, FlagSet::NOFAIL, None)
        .map_err(|e| format!("{e}"))?;

    Ok(Audio {
        _mainloop: mainloop,
        _context: context,
        introspect,
        inner,
    })
}

pub fn serve(shared: Rc<Shared>) {
    match connect(&shared) {
        Ok(audio) => *shared.audio.borrow_mut() = Some(audio),
        Err(e) => error!("audio: {e}"),
    }
}
