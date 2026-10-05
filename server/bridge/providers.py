"""Every model provider the bridge can talk to, in one table.

Both the sign-in page (accounts.py) and the model layer (llm.py) read this, so adding a
provider is one entry here plus, if it has a free tier, nothing else. Two wire shapes cover
all of them: Anthropic's Messages API, and OpenAI's chat-completions API, which every other
hosted provider (and Ollama, at /v1) copies closely enough to share one code path.

`kind`       "anthropic" or "openai" (chat-completions shape). Ollama is "openai" too.
`base`       the API root. Empty for the ones whose root comes from config (OpenAI's can be
             pointed elsewhere with OPENAI_BASE_URL; Ollama's is the URL the user signs in with).
`cfg_key`    the Config attribute holding the credential (or, for Ollama, the URL).
`key_prefix` what a real key starts with, used to catch a key pasted in the wrong box.
             Empty means "no fixed prefix".
`free`       a short, honest line about the free tier, shown on the page. Empty = paid only.
`speech`     ASR/TTS model names when the provider can also do hearing and voice.
"""

from __future__ import annotations

from typing import Dict, List

PROVIDERS: Dict[str, Dict] = {
    "anthropic": {
        "label": "Claude", "company": "Anthropic", "kind": "anthropic",
        "base": "https://api.anthropic.com", "cfg_key": "anthropic_key", "key_prefix": "sk-ant-",
        "keys_url": "https://console.anthropic.com/settings/keys",
        "free": "",
        "note": "A Claude Pro or Max plan doesn't include API use. The API is a separate account "
                "(the Anthropic Console) with its own billing, even if you already pay for claude.ai.",
        "preferred": ["claude-haiku-4-5", "claude-sonnet-5", "claude-opus-5"],
    },
    "openai": {
        "label": "ChatGPT", "company": "OpenAI", "kind": "openai",
        "base": "", "cfg_key": "openai_key", "key_prefix": "sk-",
        "keys_url": "https://platform.openai.com/api-keys",
        "free": "",
        "note": "ChatGPT and the API are separate accounts. A ChatGPT Plus plan doesn't include "
                "API use: you need an OpenAI Platform account (platform.openai.com) with its own "
                "billing, and the key comes from there, not from chatgpt.com.",
        "preferred": ["gpt-4o-mini", "gpt-4.1-mini", "gpt-5-mini", "gpt-4o"],
        "speech": {"asr": "whisper-1", "tts": "gpt-4o-mini-tts"},
    },
    "groq": {
        "label": "Groq", "company": "Groq", "kind": "openai",
        "base": "https://api.groq.com/openai/v1", "cfg_key": "groq_key", "key_prefix": "gsk_",
        "keys_url": "https://console.groq.com/keys",
        "free": "Free tier, no card needed. Very fast open models (Llama, GPT-OSS), rate-limited per day.",
        "note": "The only free option that can also hear and speak: Groq serves Whisper and a text-to-speech "
                "model too, so with Groq alone the robot works end to end. Voice needs the PlayAI "
                "terms accepted once in the Groq console.",
        "preferred": ["llama-3.1-8b-instant", "llama-3.3-70b-versatile", "openai/gpt-oss-20b"],
        "speech": {"asr": "whisper-large-v3-turbo", "tts": "playai-tts", "tts_voice": "Fritz-PlayAI"},
    },
    "gemini": {
        "label": "Gemini", "company": "Google", "kind": "openai",
        "base": "https://generativelanguage.googleapis.com/v1beta/openai", "cfg_key": "gemini_key",
        "key_prefix": "AIza",
        "keys_url": "https://aistudio.google.com/apikey",
        "free": "Free tier in Google AI Studio, no card needed. Rate-limited per minute and per day.",
        "note": "Free-tier conversations may be used by Google to improve their models. Use a paid "
                "project if that matters to you.",
        "preferred": ["gemini-2.5-flash-lite", "gemini-2.5-flash", "gemini-2.0-flash"],
    },
    "openrouter": {
        "label": "OpenRouter", "company": "OpenRouter", "kind": "openai",
        "base": "https://openrouter.ai/api/v1", "cfg_key": "openrouter_key", "key_prefix": "sk-or-",
        "keys_url": "https://openrouter.ai/keys",
        "free": "One key, many models. Only the models marked free are listed here; they cost nothing.",
        "note": "Free models come and go and can be slow at busy times. Ask for a paid one by typing "
                "its name if you have credit on the account.",
        "preferred": ["meta-llama/llama-3.3-70b-instruct:free", "google/gemma-3-27b-it:free",
                      "qwen/qwen3-235b-a22b:free"],
    },
    "cerebras": {
        "label": "Cerebras", "company": "Cerebras", "kind": "openai",
        "base": "https://api.cerebras.ai/v1", "cfg_key": "cerebras_key", "key_prefix": "csk-",
        "keys_url": "https://cloud.cerebras.ai/",
        "free": "Free tier, no card needed. Fastest replies of any provider here; a handful of open models.",
        "note": "",
        "preferred": ["llama3.1-8b", "llama-3.3-70b", "gpt-oss-120b", "qwen-3-32b"],
    },
    "mistral": {
        "label": "Mistral", "company": "Mistral AI", "kind": "openai",
        "base": "https://api.mistral.ai/v1", "cfg_key": "mistral_key", "key_prefix": "",
        "keys_url": "https://console.mistral.ai/api-keys",
        "free": "Free \"Experiment\" plan after phone verification. Rate-limited, fine for a desk robot.",
        "note": "",
        "preferred": ["mistral-small-latest", "open-mistral-nemo", "mistral-medium-latest"],
    },
    "ollama": {
        "label": "Ollama", "company": "your own computer", "kind": "openai",
        "base": "", "cfg_key": "ollama_base", "key_prefix": "",
        "keys_url": "https://ollama.com/download",
        "free": "Completely free and private: the model runs on a computer you own. No key, just its address.",
        "note": "Install Ollama, run \"ollama pull llama3.2\", then paste that computer's address. If it "
                "isn't this bridge's own machine, start Ollama with OLLAMA_HOST=0.0.0.0 so the bridge "
                "can reach it. Small models (3B to 8B) answer fast enough for talking.",
        "preferred": ["llama3.2", "llama3.1", "qwen2.5", "gemma3", "mistral"],
        "url_placeholder": "http://127.0.0.1:11434",
    },
}

# The order the page shows them in: the two big names, then the free ones.
PAGE_ORDER: List[str] = ["anthropic", "openai", "groq", "gemini", "openrouter", "cerebras", "mistral", "ollama"]

OLLAMA_DEFAULT_BASE = "http://127.0.0.1:11434"


def label(provider: str) -> str:
    return PROVIDERS.get(provider, {}).get("label", provider)


def kind(provider: str) -> str:
    """'anthropic' or 'openai'. Unknown names fall through to the OpenAI shape, which is
    what `openai-compatible` (env-only, OPENAI_BASE_URL) has always meant."""
    return PROVIDERS.get(provider, {}).get("kind", "openai")


def base_url(cfg, provider: str) -> str:
    """Where a provider's chat-completions (or Messages) API lives."""
    meta = PROVIDERS.get(provider)
    if provider == "ollama":
        return (cfg.ollama_base or OLLAMA_DEFAULT_BASE).rstrip("/") + "/v1"
    if meta and meta["base"]:
        return meta["base"]
    # openai, and openai-compatible: whatever OPENAI_BASE_URL points at.
    return cfg.openai_base.rstrip("/")


def credential(cfg, provider: str) -> str:
    """The key the request should carry. Ollama has none; a placeholder keeps the header
    well-formed for the odd proxy that insists on one."""
    if provider == "ollama":
        return "ollama"
    meta = PROVIDERS.get(provider)
    if meta:
        return getattr(cfg, meta["cfg_key"], "") or ""
    return cfg.openai_key


def wrong_box(provider: str, key: str) -> str:
    """Name the provider whose key this looks like, if it's clearly not `provider`'s."""
    best, best_len = "", 0
    for other, meta in PROVIDERS.items():
        prefix = meta["key_prefix"]
        if prefix and key.startswith(prefix) and len(prefix) > best_len:
            best, best_len = other, len(prefix)
    if best and best != provider:
        return best
    return ""
