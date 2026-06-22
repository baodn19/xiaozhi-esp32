import pytest
import requests

from conftest import load_golden_fixtures

pytestmark = pytest.mark.integration


@pytest.fixture(autouse=True)
def _require_integration(run_integration):
    if not run_integration:
        pytest.skip("pass --run-integration to hit live LotusAI API")


def _titles(response_json: list) -> set[str]:
    return {rec["title"] for rec in response_json if isinstance(rec, dict) and rec.get("title")}


def _ingredient_text(rec: dict) -> str:
    ingredients = rec.get("ingredients") or []
    if isinstance(ingredients, list):
        return " ".join(str(i) for i in ingredients).lower()
    return str(ingredients).lower()


@pytest.mark.parametrize("fixture", load_golden_fixtures(), ids=lambda f: f.get("notes", "fixture"))
def test_recipes_match_web(base_url, fixture):
    inputs = fixture["inputs"]
    top_k = inputs.get("top_k", 3)

    r = requests.post(
        f"{base_url}/api/recommend/hard",
        json=inputs,
        timeout=60,
    )
    assert r.status_code == 200
    recipes = r.json()
    assert isinstance(recipes, list)
    assert 1 <= len(recipes) <= top_k

    titles = _titles(recipes)
    assert titles

    for excluded_title in fixture.get("excluded_titles") or []:
        assert excluded_title not in titles

    allergens = inputs.get("allergens") or []
    if allergens:
        for rec in recipes:
            text = _ingredient_text(rec)
            for code in allergens:
                if code.lower() in ("peanut", "peanuts"):
                    assert "peanut" not in text

    for excl in inputs.get("excluded_ingredients") or []:
        for rec in recipes:
            assert excl.lower() not in _ingredient_text(rec)
