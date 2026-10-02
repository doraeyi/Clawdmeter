"""Now-playing source for the Windows daemon.

Reads Windows' Global System Media Transport Controls (the media flyout that
appears when you press a volume/media key). Browsers report YouTube and
YouTube Music there, as do Spotify and most players, so this works without any
browser extension.

Requires the pywinrt packages listed in requirements-windows.txt
(winrt-Windows.Media.Control and friends). If they are missing the watcher
disables itself and the rest of the daemon keeps working.
"""
from __future__ import annotations

import datetime as _dt
import time

try:
    from winrt.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as _MediaManager,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as _Status,
    )
    AVAILABLE = True
except Exception:  # ImportError, or winrt present but broken
    _MediaManager = None
    _Status = None
    AVAILABLE = False

HEARTBEAT_S = 15          # resend unchanged state so the device knows we're alive
MAX_TITLE = 70            # characters; keeps the BLE payload well under 512 bytes
MAX_ARTIST = 45

_APP_NAMES = {
    "chrome": "Chrome",
    "msedge": "Edge",
    "firefox": "Firefox",
    "brave": "Brave",
    "opera": "Opera",
    "spotify": "Spotify",
    "vivaldi": "Vivaldi",
}


def _app_name(aumid: str) -> str:
    """Turn an AppUserModelID like 'Chrome' / 'MSEdge' / 'Spotify.exe' into a label."""
    low = (aumid or "").lower()
    for key, name in _APP_NAMES.items():
        if key in low:
            return name
    base = (aumid or "").split("!")[0].split("\\")[-1]
    if base.lower().endswith(".exe"):
        base = base[:-4]
    return base[:24]


def _seconds(value) -> float:
    if value is None:
        return 0.0
    if isinstance(value, _dt.timedelta):
        return value.total_seconds()
    # Older pywinrt builds expose TimeSpan.duration in 100 ns ticks.
    ticks = getattr(value, "duration", None)
    return ticks / 10_000_000 if ticks is not None else 0.0


def _clip(text: str, n: int) -> str:
    text = (text or "").strip()
    return text if len(text) <= n else text[: n - 1] + "…"


class NowPlayingWatcher:
    """Call poll() about once a second; it returns a payload dict to send, or None."""

    def __init__(self, log=print) -> None:
        self._log = log
        self._mgr = None
        self._last_key = None
        self._last_sent = 0.0
        self._last_pos = None
        self._warned = False

    async def _snapshot(self) -> dict:
        if self._mgr is None:
            self._mgr = await _MediaManager.request_async()
        session = self._mgr.get_current_session()
        if session is None:
            return {"np": 1, "st": "none"}

        props = await session.try_get_media_properties_async()
        info = session.get_playback_info()
        status = info.playback_status if info else None
        if status == _Status.PLAYING:
            st = "playing"
        elif status == _Status.PAUSED:
            st = "paused"
        elif status in (_Status.STOPPED, _Status.CLOSED):
            st = "stopped"
        else:
            st = "none"

        payload = {
            "np": 1,
            "ti": _clip(props.title if props else "", MAX_TITLE),
            "ar": _clip(props.artist if props else "", MAX_ARTIST),
            "app": _app_name(session.source_app_user_model_id),
            "st": st,
        }

        try:
            tl = session.get_timeline_properties()
            dur = _seconds(tl.end_time) - _seconds(tl.start_time)
            pos = _seconds(tl.position)
            # Position is reported as of last_updated_time; advance it while playing.
            updated = getattr(tl, "last_updated_time", None)
            if st == "playing" and isinstance(updated, _dt.datetime):
                now = _dt.datetime.now(updated.tzinfo or _dt.timezone.utc)
                pos += max(0.0, (now - updated).total_seconds())
            if dur > 0:
                payload["dur"] = int(dur)
                payload["pos"] = int(min(max(pos, 0.0), dur))
        except Exception:
            pass  # timeline is optional; the device hides the progress bar
        return payload

    async def poll(self) -> dict | None:
        if not AVAILABLE:
            if not self._warned:
                self._warned = True
                self._log("Now playing disabled: pip install winrt-Windows.Media.Control "
                          "winrt-Windows.Foundation winrt-runtime")
            return None
        try:
            payload = await self._snapshot()
        except Exception as e:  # WinRT can throw if the session vanishes mid-read
            self._mgr = None
            self._log(f"Now playing read failed: {type(e).__name__}: {e}")
            return None

        # Position changes every second; only title/artist/state/app (and a
        # big position jump, i.e. a seek) count as a change worth sending now.
        key = (payload.get("ti"), payload.get("ar"), payload.get("st"), payload.get("app"),
               payload.get("dur"))
        now = time.time()
        seeked = False
        if "pos" in payload and self._last_pos is not None:
            expected = self._last_pos + ((now - self._last_sent) if payload["st"] == "playing" else 0)
            seeked = abs(payload["pos"] - expected) > 3
        if key != self._last_key or seeked or now - self._last_sent >= HEARTBEAT_S:
            self._last_key = key
            self._last_sent = now
            self._last_pos = payload.get("pos")
            return payload
        return None

    def force_resend(self) -> None:
        self._last_key = None
