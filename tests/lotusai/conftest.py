"""Shared pytest fixtures for LotusAI API tests."""

import json
import os
from pathlib import Path

import pytest
import requests

FIXTURES_DIR = Path(__file__).parent / "fixtures"


def pytest_addoption(parser):
    parser.addoption(
        "--run-integration",
        action="store_true",
        default=False,
        help="Run tests against live LotusAI API",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        "integration: tests that require live LotusAI API (use --run-integration)",
    )


@pytest.fixture(scope="session")
def run_integration(request) -> bool:
    return bool(request.config.getoption("--run-integration"))


@pytest.fixture(scope="session")
def base_url() -> str:
    return os.environ.get("LOTUSAI_BASE_URL", "https://lotusfoodasmedicine.com").rstrip("/")


@pytest.fixture(scope="session")
def allergen_options(base_url: str) -> list[str]:
    try:
        r = requests.get(f"{base_url}/api/config", timeout=15)
        if r.status_code != 200:
            return []
        data = r.json()
        options = data.get("allergenOptions") or []
        return [str(o.get("code") or o.get("value") or o) for o in options if o]
    except requests.RequestException:
        return []


def load_golden_fixtures() -> list[dict]:
    path = FIXTURES_DIR / "golden_recipes.json"
    with open(path, encoding="utf-8") as f:
        return json.load(f)
