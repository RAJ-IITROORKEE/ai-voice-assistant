from __future__ import annotations

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    # InsForge / auth
    insforge_base_url: str = "https://ts4hxi45.us-east.insforge.app"
    insforge_anon_key: str = ""
    # Direct Postgres DSN (InsForge database) for durable memory.
    database_url: str = ""

    # Azure AI Foundry OpenAI-compatible LLM
    azure_openai_endpoint: str = "https://datumm-agent-resource.services.ai.azure.com/openai/v1"
    azure_openai_api_key: str = ""
    azure_openai_model: str = "gpt-5.6-luna"

    # Agent behavior
    system_prompt: str = (
        "You are Luna, a concise, friendly voice assistant. "
        "Answer briefly and clearly. You remember prior turns in the conversation."
    )
    max_history_messages: int = 40

    cors_origins: str = "*"


settings = Settings()
