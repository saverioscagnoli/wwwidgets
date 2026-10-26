export {};

declare global {
  namespace Wwwidgets {
    type Unsubscribe = () => void;
    type Json = string | number | boolean | null | Json[] | { [key: string]: Json };

    interface Rect {
      x: number;
      y: number;
      width: number;
      height: number;
    }

    type KeyboardMode = "none" | "on-demand" | "exclusive";

    interface Workspace {
      id: string;
      name: string;
      active: boolean;
      urgent: boolean;
      hidden: boolean;
    }

    interface NotificationAction {
      key: string;
      label: string;
    }

    interface Notification {
      id: number;
      app: string;
      icon: string;
      image: string;
      summary: string;
      body: string;
      urgency: 0 | 1 | 2;
      time: number;
      expired: boolean;
      actions: NotificationAction[];
    }

    interface TrayItem {
      id: number;
      appId: string;
      title: string;
      status: "Active" | "Passive" | "NeedsAttention" | (string & {});
      icon: string;
      tooltip: string;
      tooltipBody: string;
      itemIsMenu: boolean;
      hasMenu: boolean;
    }

    interface TrayMenuItem {
      id: number;
      label: string;
      type: "standard" | "separator" | (string & {});
      enabled: boolean;
      visible: boolean;
      toggleType: "" | "checkmark" | "radio" | (string & {});
      toggleState: number;
      children: TrayMenuItem[];
    }

    type AudioKind = "sink" | "source";

    interface AudioDevice {
      index: number;
      name: string;
      description: string;
      volume: number;
      muted: boolean;
      default: boolean;
    }

    interface AudioStream {
      index: number;
      app: string;
      title: string;
      icon: string;
      device: string;
      volume: number;
      muted: boolean;
      corked: boolean;
    }

    interface Audio {
      sink?: AudioDevice;
      source?: AudioDevice;
      sinks: AudioDevice[];
      sources: AudioDevice[];
      playback: AudioStream[];
      record: AudioStream[];
    }

    interface BluetoothDevice {
      path: string;
      name: string;
      address: string;
      icon: string;
      paired: boolean;
      connected: boolean;
      battery?: number;
    }

    interface Bluetooth {
      available: boolean;
      powered: boolean;
      discovering: boolean;
      devices: BluetoothDevice[];
    }

    type BluetoothRequest =
      | { type: "cancel" }
      | { type: "display" | "confirm"; device: string; name: string; code: string }
      | { type: "pin" | "passkey" | "authorize"; device: string; name: string };

    interface AccessPoint {
      path: string;
      ssid: string;
      strength: number;
      secure: boolean;
      active: boolean;
    }

    interface Network {
      available: boolean;
      state:
        | "unknown"
        | "asleep"
        | "disconnected"
        | "disconnecting"
        | "connecting"
        | "connected-local"
        | "connected-site"
        | "connected";
      connectivity: "unknown" | "none" | "portal" | "limited" | "full";
      wifiEnabled: boolean;
      wifiHardware: boolean;
      primary?: { name: string; type: string };
      accessPoints: AccessPoint[];
    }

    interface Battery {
      percent: number;
      state:
        | "unknown"
        | "charging"
        | "discharging"
        | "empty"
        | "full"
        | "pending charge"
        | "pending discharge";
      timeToEmpty: number;
      timeToFull: number;
    }

    interface Brightness {
      device: string;
      value: number;
      raw: number;
      max: number;
    }

    interface Toplevel {
      id: number;
      title: string;
      appId: string;
      activated: boolean;
      minimized: boolean;
    }

    interface ClipboardEntry {
      id: number;
      image: boolean;
      time: number;
      size: number;
      url?: string;
      text?: string;
    }

    interface MediaPlayer {
      id: number;
      name: string;
      identity: string;
      status: "Playing" | "Paused" | "Stopped" | (string & {});
      playing: boolean;
      title: string;
      artist: string;
      album: string;
      art: string;
      track: string;
      length: number;
      position: number;
      canPlay: boolean;
      canPause: boolean;
      canNext: boolean;
      canPrev: boolean;
      canSeek: boolean;
    }

    interface ExecResult {
      stdout: string;
      stderr: string;
      status: number;
    }

    interface SpawnOptions {
      onStdout?: (data: string) => void;
      onStderr?: (data: string) => void;
      onExit?: (status: number) => void;
    }

    interface Process {
      id: number;
      write(data: unknown): Promise<true>;
      kill(signal?: number): Promise<true>;
    }

    interface Api {
      monitor: string;
      window: {
        setInputRegion(rects: Rect[] | null): Promise<true>;
        setKeyboard(mode: KeyboardMode): Promise<true>;
      };
      state: {
        get<T extends Json = Json>(key: string): Promise<T | null>;
        set(key: string, value: Json | undefined): Promise<true>;
        toggle(key: string): Promise<boolean>;
        onChange<T extends Json = Json>(key: string, cb: (value: T) => void): Unsubscribe;
      };
      workspace: {
        list(): Promise<Workspace[]>;
        get(): Promise<Workspace | null>;
        set(id: string): Promise<true>;
        onChange(cb: (workspaces: Workspace[]) => void): Unsubscribe;
      };
      notifications: {
        list(): Promise<Notification[]>;
        close(id: number): Promise<true>;
        invoke(id: number, key: string): Promise<true>;
        onChange(cb: (notifications: Notification[]) => void): Unsubscribe;
      };
      tray: {
        list(): Promise<TrayItem[]>;
        activate(id: number): Promise<true>;
        secondaryActivate(id: number): Promise<true>;
        contextMenu(id: number): Promise<true>;
        scroll(id: number, delta: number, orientation?: "vertical" | "horizontal"): Promise<true>;
        menu(id: number): Promise<TrayMenuItem[]>;
        menuClick(id: number, item: number): Promise<true>;
        onChange(cb: (items: TrayItem[]) => void): Unsubscribe;
      };
      audio: {
        get(): Promise<Audio>;
        setVolume(device: AudioKind, volume: number): Promise<true>;
        setMute(device: AudioKind, muted: boolean): Promise<true>;
        setDefault(kind: AudioKind, name: string): Promise<true>;
        setDeviceVolume(kind: AudioKind, name: string, volume: number): Promise<true>;
        setDeviceMute(kind: AudioKind, name: string, muted: boolean): Promise<true>;
        setStreamVolume(kind: AudioKind, index: number, volume: number): Promise<true>;
        setStreamMute(kind: AudioKind, index: number, muted: boolean): Promise<true>;
        moveStream(kind: AudioKind, index: number, device: string): Promise<true>;
        onChange(cb: (audio: Audio) => void): Unsubscribe;
      };
      bluetooth: {
        get(): Promise<Bluetooth>;
        setPowered(powered: boolean): Promise<true>;
        scan(on?: boolean): Promise<true>;
        connect(path: string): Promise<true>;
        disconnect(path: string): Promise<true>;
        onChange(cb: (bluetooth: Bluetooth) => void): Unsubscribe;
        pair(path: string): Promise<true>;
        reply(value?: string | number): Promise<true>;
        reject(): Promise<true>;
        onRequest(cb: (request: BluetoothRequest) => void): Unsubscribe;
      };
      network: {
        get(): Promise<Network>;
        setWifi(enabled: boolean): Promise<true>;
        scan(): Promise<true>;
        connect(path: string, password?: string | null): Promise<true>;
        disconnect(): Promise<true>;
        onChange(cb: (network: Network) => void): Unsubscribe;
      };
      battery: {
        get(): Promise<Battery>;
        onChange(cb: (battery: Battery) => void): Unsubscribe;
      };
      brightness: {
        get(): Promise<Brightness>;
        set(value: number): Promise<true>;
        onChange(cb: (brightness: Brightness) => void): Unsubscribe;
      };
      toplevels: {
        list(): Promise<Toplevel[]>;
        activate(id: number): Promise<true>;
        close(id: number): Promise<true>;
        setMinimized(id: number, on: boolean): Promise<true>;
        onChange(cb: (toplevels: Toplevel[]) => void): Unsubscribe;
      };
      clipboard: {
        history(): Promise<ClipboardEntry[]>;
        set(text: unknown): Promise<true>;
        copy(id: number): Promise<true>;
        remove(id: number): Promise<true>;
        clear(): Promise<true>;
        onChange(cb: (history: ClipboardEntry[]) => void): Unsubscribe;
      };
      media: {
        list(): Promise<MediaPlayer[]>;
        playPause(id: number): Promise<true>;
        play(id: number): Promise<true>;
        pause(id: number): Promise<true>;
        stop(id: number): Promise<true>;
        next(id: number): Promise<true>;
        previous(id: number): Promise<true>;
        setPosition(id: number, position: number): Promise<true>;
        onChange(cb: (players: MediaPlayer[]) => void): Unsubscribe;
      };
      exec(command: string, args?: string[]): Promise<ExecResult>;
      spawn(command: string, args?: string[], options?: SpawnOptions): Promise<Process>;
    }
  }

  const wwwidgets: Wwwidgets.Api;

  interface Window {
    wwwidgets: Wwwidgets.Api;
  }
}
