"""TP-Link Tapo smart plugs for the Windows daemon (Smart Plug app on the board).

The board can't reach the plugs itself (no Wi-Fi in this firmware), so the
daemon talks to them over the local network with the `tapo` package and relays
state/commands over BLE characteristic ...0006:

    daemon -> board (write):  {"pl":[{"n":"Desk lamp","on":1,"ok":1}, ...], "err":""}
    board -> daemon (notify): {"cmd":"set","i":0,"on":1}

Config (%LOCALAPPDATA%\\Clawdmeter\\config):

    tapo_username = you@example.com      # your Tapo app (TP-Link ID) login
    tapo_password = your-password
    plugs = 192.168.1.50, 192.168.1.51, 192.168.1.52
    # optional — otherwise the names from the Tapo app are used
    plug_names = Lamp, Fan, Dehumidifier

Works with P100/P105/P110/P115 (all use the same API). If the `tapo` package is
missing or nothing is configured, the board shows a hint instead of buttons.
"""
from __future__ import annotations

import asyncio
import json
import time
from pathlib import Path

try:
    from tapo import ApiClient as _ApiClient
    AVAILABLE = True
except Exception:  # ImportError, or a broken wheel
    _ApiClient = None
    AVAILABLE = False

REFRESH_S = 10        # re-read plug states this often
HEARTBEAT_S = 15      # resend unchanged state so the board knows we're alive
MAX_PLUGS = 3
MAX_NAME = 16
TIMEOUT_S = 6


def _read_config(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    try:
        if path.exists():
            for raw in path.read_text(encoding="utf-8-sig").splitlines():
                line = raw.strip()
                # Only whole-line comments: passwords may contain '#'.
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, val = line.split("=", 1)
                out[key.strip().lower()] = val.strip()
    except (OSError, UnicodeDecodeError):
        pass
    return out


def _split(val: str) -> list[str]:
    return [p.strip() for p in val.replace(";", ",").split(",") if p.strip()]


class PlugController:
    def __init__(self, log, config_path: Path) -> None:
        self._log = log
        self._config_path = config_path
        self._cfg_key: tuple | None = None
        self._client = None
        self._ips: list[str] = []
        self._names: list[str] = []
        self._devices: dict[int, object] = {}
        self._state: list[dict] = []
        self._err = ""
        self._last_refresh = 0.0
        self._last_sent = 0.0
        self._last_payload = ""
        self._dirty = True
        self._busy: asyncio.Task | None = None
        self._lock = asyncio.Lock()
        self._missing_logged = False
        self._have_state = False

    # ---- config ---------------------------------------------------------
    def _load_config(self) -> None:
        cfg = _read_config(self._config_path)
        user = cfg.get("tapo_username", "")
        pw = cfg.get("tapo_password", "")
        ips = _split(cfg.get("plugs", ""))[:MAX_PLUGS]
        names = _split(cfg.get("plug_names", ""))
        key = (user, pw, tuple(ips), tuple(names))
        if key == self._cfg_key:
            return
        self._cfg_key = key
        self._ips, self._names = ips, names
        self._devices.clear()
        self._client = None
        self._state = [self._entry(i, None, False) for i in range(len(ips))]
        self._err = ""
        self._last_refresh = 0.0
        self._have_state = False
        self._dirty = True
        if not AVAILABLE:
            self._err = "電腦缺少 tapo 套件"
            if not self._missing_logged:
                self._log("Smart plugs: `tapo` package not installed (pip install tapo)")
                self._missing_logged = True
            return
        if not ips:
            return  # board shows "not configured"
        if not user or not pw:
            self._err = "請設定 Tapo 帳號密碼"
            return
        try:
            self._client = _ApiClient(user, pw, timeout_s=TIMEOUT_S)
        except TypeError:   # older tapo without timeout_s
            self._client = _ApiClient(user, pw)
        self._log(f"Smart plugs: {len(ips)} configured ({', '.join(ips)})")

    def _entry(self, i: int, info, ok: bool) -> dict:
        name = self._names[i] if i < len(self._names) else ""
        if not name and info is not None:
            name = getattr(info, "nickname", "") or ""
        if not name:
            prev = self._state[i]["n"] if i < len(self._state) else ""
            name = prev or f"插座 {i + 1}"
        on = bool(getattr(info, "device_on", False)) if info is not None else (
            self._state[i]["on"] if i < len(self._state) else 0)
        return {"n": name[:MAX_NAME], "on": 1 if on else 0, "ok": 1 if ok else 0}

    # ---- device I/O -----------------------------------------------------
    async def _device(self, i: int):
        dev = self._devices.get(i)
        if dev is None:
            dev = await asyncio.wait_for(self._client.p100(self._ips[i]), TIMEOUT_S + 2)
            self._devices[i] = dev
        return dev

    async def _refresh_one(self, i: int) -> None:
        try:
            dev = await self._device(i)
            info = await asyncio.wait_for(dev.get_device_info(), TIMEOUT_S + 2)
            new = self._entry(i, info, True)
            if self._err and "密碼" in self._err:
                self._err = ""
        except Exception as e:  # network, auth, timeouts — all mean "offline"
            self._devices.pop(i, None)
            msg = str(e)
            if "credential" in msg.lower() or "1501" in msg:
                self._err = "Tapo 帳號或密碼錯誤"
            was_ok = not self._have_state or (i < len(self._state) and self._state[i]["ok"])
            if was_ok:  # log the transition only, not every 10 s
                self._log(f"Smart plug {self._ips[i]} unreachable: {type(e).__name__}: {msg[:120]}")
            new = self._entry(i, None, False)
        if i < len(self._state) and new != self._state[i]:
            self._state[i] = new
            self._dirty = True

    async def _refresh_all(self) -> None:
        async with self._lock:
            await asyncio.gather(*(self._refresh_one(i) for i in range(len(self._ips))))
            self._last_refresh = time.time()
            self._have_state = True
            self._dirty = True

    async def _set(self, i: int, on: bool) -> None:
        async with self._lock:
            try:
                dev = await self._device(i)
                await asyncio.wait_for(dev.on() if on else dev.off(), TIMEOUT_S + 2)
                self._log(f"Smart plug {self._ips[i]} -> {'on' if on else 'off'}")
            except Exception as e:
                self._devices.pop(i, None)
                self._log(f"Smart plug {self._ips[i]} set failed: {type(e).__name__}: {str(e)[:120]}")
            await self._refresh_one(i)
            self._dirty = True

    def _spawn(self, coro) -> None:
        if self._busy and not self._busy.done():
            # Commands queue behind the lock; refreshes are just skipped.
            asyncio.ensure_future(coro)
            return
        self._busy = asyncio.ensure_future(coro)

    # ---- public API -----------------------------------------------------
    def on_command(self, data: bytes) -> None:
        """BLE notify callback (runs on the event loop)."""
        try:
            msg = json.loads(bytes(data).decode("utf-8"))
            i = int(msg.get("i", -1))
            on = bool(msg.get("on"))
        except (ValueError, UnicodeDecodeError, AttributeError, TypeError):
            return
        if msg.get("cmd") != "set" or not self._client or not (0 <= i < len(self._ips)):
            return
        self._spawn(self._set(i, on))

    def force_resend(self) -> None:
        self._dirty = True

    def reset(self) -> None:
        """New BLE connection: send the current state right away."""
        self._dirty = True
        self._last_sent = 0.0

    async def poll(self) -> dict | None:
        """Called about once a second. Returns a payload to send, or None."""
        self._load_config()
        now = time.time()
        if self._client and now - self._last_refresh >= REFRESH_S:
            self._last_refresh = now  # don't stack refreshes while one runs
            self._spawn(self._refresh_all())
        if self._client and not self._have_state:
            return None  # first read still running; avoid flashing "offline"
        if not self._dirty and now - self._last_sent < HEARTBEAT_S:
            return None
        payload = {"pl": self._state if self._client and not self._err else [], "err": self._err}
        self._dirty = False
        self._last_sent = now
        return payload
