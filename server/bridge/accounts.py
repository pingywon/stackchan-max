"""Sign-in for every provider, kept on the bridge.

The robot never holds a key, so an end user signs in on the bridge's own page
(`GET /`, see accounts_page.html): paste an API key, the bridge checks it against the
provider's model list, and saves it. Any number of providers can be signed in at once;
`active` records which one answers. Keys set in bridge.env still work and show as signed
in, but can only be removed by editing that file. Ollama is the odd one out: it has no
key, so "signing in" means giving the address of the computer running it.

There is no "Sign in with Claude/ChatGPT" button because neither company offers a login a
third-party app can use for API access, and neither subscription includes API usage.

Anthropic has two kinds of key. One made inside a workspace just works. One made at the
organisation level is refused until every request names a workspace, so the page asks for
the workspace ID when Anthropic says so, and llm.py sends it as a header from then on.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import time
from typing import Dict, List, Optional, Tuple

import aiohttp

import providers
from providers import PROVIDERS, PAGE_ORDER

log = logging.getLogger("bridge.accounts")

_OPENAI_CHAT_PREFIXES = ("gpt-", "chatgpt-", "o1", "o3", "o4")
# Words that mean "not a chat model" anywhere...
_NOT_CHAT = ("audio", "realtime", "transcribe", "tts", "image", "embed", "moderation",
             "whisper", "guard", "playai", "orpheus", "ocr", "imagen", "veo", "-live",
             "learnlm", "aqa")
# ...and ones that only mean it at OpenAI (open models are routinely called "-instruct").
_OPENAI_NOT_CHAT = ("search", "instruct", "codex", "computer-use")

WORKSPACE_HINT = "anthropic-workspace-id"


class SignInError(Exception):
    def __init__(self, message: str, needs: str = ""):
        super().__init__(message)
        self.needs = needs   # "workspace" when the page should ask for a workspace ID


def is_reasoning_model(model: str) -> bool:
    return model.startswith(("o1", "o3", "o4", "gpt-5"))


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _error_text(body) -> str:
    if isinstance(body, dict):
        err = body.get("error")
        if isinstance(err, dict) and err.get("message"):
            return str(err["message"])
        if isinstance(err, str):
            return err
    return ""


def _rank(provider: str, model_id: str) -> Tuple[int, str]:
    preferred = PROVIDERS[provider]["preferred"]
    for i, name in enumerate(preferred):
        if model_id == name or model_id.startswith(name + "-") or model_id.startswith(name + ":"):
            return i, model_id
    return len(preferred), model_id


def _is_chat_model(provider: str, row: dict, model_id: str) -> bool:
    low = model_id.lower()
    if provider == "openai":
        if not low.startswith(_OPENAI_CHAT_PREFIXES) or any(w in low for w in _OPENAI_NOT_CHAT):
            return False
    if provider == "gemini" and not low.startswith("gemini-"):
        return False
    if provider == "openrouter" and not low.endswith(":free"):
        return False
    if provider == "mistral":
        caps = row.get("capabilities")
        if isinstance(caps, dict) and caps.get("completion_chat") is False:
            return False
    return not any(word in low for word in _NOT_CHAT)


def parse_models(provider: str, body) -> List[Dict[str, str]]:
    rows = body.get("data") if isinstance(body, dict) else None
    if not isinstance(rows, list):
        return []
    out = []
    seen = set()
    for row in rows:
        if not isinstance(row, dict) or not row.get("id"):
            continue
        model_id = str(row["id"])
        if provider == "gemini" and model_id.startswith("models/"):
            model_id = model_id[len("models/"):]
        if model_id in seen or not _is_chat_model(provider, row, model_id):
            continue
        seen.add(model_id)
        name = str(row.get("display_name") or row.get("name") or model_id)
        if provider == "openrouter" and name.endswith(" (free)"):
            name = name[:-len(" (free)")]
        out.append({"id": model_id, "name": name,
                    "reasoning": provider == "openai" and is_reasoning_model(model_id)})
    if provider == "anthropic":
        # Anthropic lists newest first; keep that order, but lift the preferred models up.
        out.sort(key=lambda m: _rank(provider, m["id"])[0])
    else:
        out.sort(key=lambda m: _rank(provider, m["id"]))
    return out


def default_model(provider: str, models: List[Dict[str, str]]) -> str:
    if models:
        return models[0]["id"]
    return PROVIDERS[provider]["preferred"][0]


async def verify_key(http: aiohttp.ClientSession, cfg, provider: str, key: str,
                     workspace: str = "") -> Tuple[List[Dict[str, str]], str]:
    """Check a key by listing models, which costs nothing. Returns (models, note).

    For Ollama `key` is the address of the machine running it."""
    meta = PROVIDERS[provider]
    company = meta["company"]
    params = None
    if provider == "anthropic":
        url = "https://api.anthropic.com/v1/models"
        params = {"limit": "1000"}
        headers = {"x-api-key": key, "anthropic-version": "2023-06-01"}
        if workspace:
            headers[WORKSPACE_HINT] = workspace
    elif provider == "ollama":
        url = key.rstrip("/") + "/v1/models"
        headers = {}
        company = "Ollama at " + key
    else:
        url = providers.base_url(cfg, provider) + "/models"
        headers = {"Authorization": f"Bearer {key}"}

    try:
        async with http.get(url, params=params, headers=headers,
                            timeout=aiohttp.ClientTimeout(total=15)) as r:
            status = r.status
            try:
                body = await r.json()
            except (aiohttp.ContentTypeError, ValueError):
                body = {}
    except (aiohttp.ClientError, asyncio.TimeoutError) as exc:
        if provider == "ollama":
            raise SignInError(f"Couldn't reach {company} ({exc.__class__.__name__}). Is Ollama "
                              "running there, and started with OLLAMA_HOST=0.0.0.0 if it's another "
                              "computer? Nothing was saved.")
        raise SignInError(f"Couldn't reach {company} to check the key "
                          f"({exc.__class__.__name__}). Nothing was saved.")

    if status == 200:
        models = parse_models(provider, body)
        if provider == "ollama" and not models:
            return [], "Ollama answered but has no models yet. Run \"ollama pull llama3.2\" there."
        if provider == "openrouter" and not models:
            return [], "Key accepted, but OpenRouter listed no free models right now; type a model name."
        return models, ""
    if status == 401:
        raise SignInError(f"{company} says that key isn't valid. Copy the whole key and try again.")
    detail = _error_text(body)
    if provider == "anthropic" and status == 400 and WORKSPACE_HINT in detail:
        if workspace:
            raise SignInError("Anthropic didn't accept that workspace ID. It looks like "
                              "wrkspc_… and is under Settings → Workspaces in the Console.", "workspace")
        raise SignInError("This is an organisation-level key, so Anthropic needs to know which "
                          "workspace to bill. Paste the workspace ID (Settings → Workspaces in the "
                          "Console, it starts with wrkspc_), or make a key inside a workspace instead.",
                          "workspace")
    if status == 403 and provider == "openai":
        # Restricted OpenAI project keys can be valid for chat but not allowed to list models.
        return [], "Key accepted, but it isn't allowed to list models, so the default model is used."
    raise SignInError(f"{company} answered {status}: {detail[:160] or 'no details'}. Nothing was saved.")


class AccountStore:
    def __init__(self, cfg, path: Optional[str] = None):
        self.cfg = cfg
        self.path = path or cfg.accounts_file
        self.env_keys = {p: getattr(cfg, m["cfg_key"]) for p, m in PROVIDERS.items()}
        self.env_workspace = cfg.anthropic_workspace
        self.saved: Dict[str, Dict] = {}
        self.workspace = ""          # page-entered Anthropic workspace ID
        self.active: Optional[Dict[str, str]] = None
        self.robot_pref: Optional[Dict[str, str]] = None
        self._load()
        self._apply()

    def _load(self):
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except FileNotFoundError:
            return
        except (OSError, ValueError) as exc:
            log.error("could not read %s, starting signed out: %s", self.path, exc)
            return
        for provider in PROVIDERS:
            entry = (data.get("accounts") or {}).get(provider)
            if isinstance(entry, dict) and entry.get("key"):
                self.saved[provider] = entry
        self.workspace = str(data.get("anthropic_workspace") or "")
        active = data.get("active")
        if isinstance(active, dict) and active.get("provider") in PROVIDERS:
            self.active = {"provider": active["provider"], "model": str(active.get("model") or "")}

    def _save(self):
        payload = {"accounts": self.saved, "active": self.active,
                   "anthropic_workspace": self.workspace}
        directory = os.path.dirname(os.path.abspath(self.path))
        tmp = os.path.join(directory, ".accounts.json.tmp")
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
        os.chmod(tmp, 0o600)
        os.replace(tmp, self.path)

    def _apply(self):
        for provider, meta in PROVIDERS.items():
            key = (self.saved.get(provider) or {}).get("key") or self.env_keys[provider]
            setattr(self.cfg, meta["cfg_key"], key)
        self.cfg.anthropic_workspace = self.workspace or self.env_workspace

    def signed_in(self, provider: str) -> bool:
        return bool(getattr(self.cfg, PROVIDERS[provider]["cfg_key"]))

    def source(self, provider: str) -> Optional[str]:
        if self.saved.get(provider):
            return "page"
        if self.env_keys[provider]:
            return "env"
        return None

    def _check_box(self, provider: str, key: str):
        other = providers.wrong_box(provider, key)
        if other:
            raise SignInError(f"That looks like a {providers.label(other)} "
                              f"({PROVIDERS[other]['company']}) key. Paste it in the "
                              f"{providers.label(other)} box instead.")

    async def sign_in(self, http, provider: str, key: str, workspace: str = "") -> str:
        if provider not in PROVIDERS:
            raise SignInError("Unknown provider.")
        key = "".join(key.split())
        workspace = "".join((workspace or "").split())
        meta = PROVIDERS[provider]
        if not key:
            if provider == "ollama":
                raise SignInError("Enter the address of the computer running Ollama first.")
            raise SignInError(f"Paste your {meta['label']} API key first.")
        if provider == "ollama":
            if not key.startswith(("http://", "https://")):
                key = "http://" + key
            if key.count(":") < 2 and not key.endswith("/"):
                key += ":11434"
        else:
            self._check_box(provider, key)

        if provider == "anthropic" and not workspace:
            workspace = self.workspace or self.env_workspace
        models, note = await verify_key(http, self.cfg, provider, key, workspace)
        self.saved[provider] = {"key": key, "since": _now(), "models": models}
        if provider == "anthropic" and workspace:
            self.workspace = workspace
        if self.active is None or not self.signed_in(self.active["provider"]):
            self.active = {"provider": provider, "model": default_model(provider, models)}
        self._apply()
        self._save()
        log.info("signed in to %s (%d models)", meta["label"], len(models))
        return note

    async def set_workspace(self, http, workspace: str) -> str:
        """Attach a workspace ID to the Claude key already on file (page or bridge.env)."""
        workspace = "".join((workspace or "").split())
        if not workspace:
            raise SignInError("Paste the workspace ID first. It starts with wrkspc_.")
        key = (self.saved.get("anthropic") or {}).get("key") or self.env_keys["anthropic"]
        if not key:
            raise SignInError("Paste the Claude API key first, then the workspace ID.")
        models, note = await verify_key(http, self.cfg, "anthropic", key, workspace)
        self.workspace = workspace
        entry = self.saved.get("anthropic") or {"key": key, "since": _now()}
        entry["models"] = models
        self.saved["anthropic"] = entry
        if self.active is None or not self.signed_in(self.active["provider"]):
            self.active = {"provider": "anthropic", "model": default_model("anthropic", models)}
        self._apply()
        self._save()
        log.info("Claude workspace set (%d models)", len(models))
        return note

    def sign_out(self, provider: str):
        if provider not in PROVIDERS:
            raise SignInError("Unknown provider.")
        label = PROVIDERS[provider]["label"]
        if not self.saved.get(provider):
            if self.env_keys[provider]:
                raise SignInError(f"{label}'s key is set in bridge.env on the server. "
                                  "Remove it there and restart the bridge.")
            raise SignInError(f"{label} isn't signed in.")
        del self.saved[provider]
        if provider == "anthropic":
            self.workspace = ""
        self._apply()
        if self.active and self.active["provider"] == provider:
            others = [p for p in PAGE_ORDER if p != provider and self.signed_in(p)]
            self.active = ({"provider": others[0], "model": default_model(others[0], self.models(others[0]))}
                           if others else None)
        self._save()
        log.info("signed out of %s", label)

    def choose(self, provider: str, model: str = ""):
        if provider not in PROVIDERS:
            raise SignInError("Unknown provider.")
        if not self.signed_in(provider):
            raise SignInError(f"Sign in to {PROVIDERS[provider]['label']} first.")
        self.active = {"provider": provider,
                       "model": model.strip() or default_model(provider, self.models(provider))}
        self._save()
        log.info("%s / %s answers now", PROVIDERS[provider]["label"], self.active["model"])

    def models(self, provider: str) -> List[Dict[str, str]]:
        return list((self.saved.get(provider) or {}).get("models") or [])

    def resolve(self, provider: str, model: str) -> Tuple[str, str]:
        """The page's choice wins over the robot's own preference and the env default."""
        if self.active:
            return self.active["provider"], self.active["model"]
        return provider, model

    def note_robot_pref(self, provider: str, model: str):
        self.robot_pref = {"provider": provider or "", "model": model or "", "seen": _now()}

    def speech_source(self) -> Dict[str, Optional[str]]:
        """Who does hearing (ASR) and the voice (TTS) right now — mirrors speech.py."""
        cfg = self.cfg
        asr = tts = None
        if cfg.asr_key:
            asr = "custom"
        elif cfg.openai_key:
            asr = "openai"
        elif cfg.groq_key:
            asr = "groq"
        if cfg.tts_key:
            tts = "custom"
        elif cfg.openai_key:
            tts = "openai"
        elif cfg.groq_key:
            tts = "groq"
        return {"asr": asr, "tts": tts}

    def status(self) -> Dict:
        out_providers = {}
        for provider in PAGE_ORDER:
            meta = PROVIDERS[provider]
            key = getattr(self.cfg, meta["cfg_key"])
            saved = self.saved.get(provider) or {}
            if provider == "ollama":
                hint = key
            else:
                hint = key[-4:] if len(key) > 12 else ""
            out_providers[provider] = {
                "label": meta["label"],
                "company": meta["company"],
                "free": meta["free"],
                "note": meta["note"],
                "keys_url": meta["keys_url"],
                "key_prefix": meta["key_prefix"],
                "url_placeholder": meta.get("url_placeholder", ""),
                "speech": bool(meta.get("speech")),
                "signed_in": bool(key),
                "source": self.source(provider),
                "key_hint": hint,
                "since": saved.get("since", ""),
                "models": self.models(provider) or [
                    {"id": m, "name": m, "reasoning": provider == "openai" and is_reasoning_model(m)}
                    for m in meta["preferred"]],
            }
        out_providers["anthropic"]["workspace"] = self.cfg.anthropic_workspace
        speech = self.speech_source()
        return {
            "order": list(PAGE_ORDER),
            "providers": out_providers,
            "active": self.active,
            "speech_ready": bool(speech["asr"] and speech["tts"]),
            "speech": speech,
            "robot": self.robot_pref,
            "locked": bool(self.cfg.token),
            "ws_path": self.cfg.path,
        }
