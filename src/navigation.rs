use std::path::Path;

use gtk4::gio;
use gtk4::glib;
use gtk4::glib::object::Cast;

use traccia::debug;
use traccia::warn;

use webkit6::prelude::PolicyDecisionExt;
use webkit6::prelude::WebViewExt;

fn is_trusted(base: &str, candidate: &str) -> bool {
    if candidate == "about:blank" {
        return true;
    }

    let (Ok(base), Ok(candidate)) = (
        glib::Uri::parse(base, glib::UriFlags::NONE),
        glib::Uri::parse(candidate, glib::UriFlags::NONE),
    ) else {
        return false;
    };

    if base.scheme() != candidate.scheme() {
        return false;
    }

    if base.scheme() == "file" {
        let base = Path::new(base.path().as_str()).to_path_buf();
        let root = base.parent().unwrap_or(&base);

        return Path::new(candidate.path().as_str()).starts_with(root);
    }

    base.host() == candidate.host() && base.port() == candidate.port()
}

pub fn pin(webview: &webkit6::WebView, base: &str) {
    let base = base.to_owned();

    webview.connect_decide_policy(move |_, decision, kind| match kind {
        webkit6::PolicyDecisionType::NavigationAction => {
            let Some(action) = decision
                .downcast_ref::<webkit6::NavigationPolicyDecision>()
                .and_then(|d| d.navigation_action())
            else {
                return false;
            };

            let Some(uri) = action.request().and_then(|r| r.uri()) else {
                return false;
            };

            if is_trusted(&base, &uri) {
                return false;
            }

            decision.ignore();

            if action.navigation_type() == webkit6::NavigationType::LinkClicked {
                debug!("Opening {uri} externally");

                if let Err(e) =
                    gio::AppInfo::launch_default_for_uri(&uri, gio::AppLaunchContext::NONE)
                {
                    warn!("Failed to open {uri}: {e}");
                }
            } else {
                warn!("Blocked navigation to {uri}");
            }

            true
        }
        webkit6::PolicyDecisionType::NewWindowAction => {
            decision.ignore();
            true
        }
        _ => false,
    });
}
