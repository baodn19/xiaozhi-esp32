import pytest
import requests

pytestmark = pytest.mark.integration


@pytest.fixture(autouse=True)
def _require_integration(run_integration):
    if not run_integration:
        pytest.skip("pass --run-integration to hit live LotusAI API")


def test_required_only(base_url):
    r = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"ingredients": ["chicken", "rice"], "top_k": 3},
        timeout=60,
    )
    assert r.status_code == 200
    body = r.json()
    assert isinstance(body, list)
    assert len(body) <= 3
    assert all(rec.get("title") for rec in body)


def test_missing_ingredients_returns_error(base_url):
    r = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"top_k": 3},
        timeout=30,
    )
    # Production may return 200 with an empty list or 4xx depending on deployment.
    assert r.status_code in (200, 400, 422)
    if r.status_code == 200:
        assert isinstance(r.json(), list)


def test_allergens_reduce_or_change_results(base_url):
    baseline = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"ingredients": ["chicken"], "top_k": 10},
        timeout=60,
    ).json()
    filtered = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"ingredients": ["chicken"], "allergens": ["peanut"], "top_k": 10},
        timeout=60,
    ).json()
    assert len(filtered) <= len(baseline)
    for rec in filtered:
        assert "peanut" not in " ".join(
            str(i) for i in (rec.get("ingredients") or [])
        ).lower()


def test_allergen_alias_resolves(base_url):
    r = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"ingredients": ["chicken"], "allergens": ["dairy"], "top_k": 3},
        timeout=60,
    )
    assert r.status_code == 200


def test_unrecognized_allergen_returns_422(base_url):
    r = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"ingredients": ["chicken"], "allergens": ["notarealallergen"], "top_k": 3},
        timeout=30,
    )
    assert r.status_code == 422


def test_excluded_ingredients_filter_results(base_url):
    baseline = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"ingredients": ["chicken", "tomato"], "top_k": 10},
        timeout=60,
    ).json()
    filtered = requests.post(
        f"{base_url}/api/recommend/hard",
        json={
            "ingredients": ["chicken", "tomato"],
            "excluded_ingredients": ["mushrooms"],
            "top_k": 10,
        },
        timeout=60,
    ).json()
    assert len(filtered) <= len(baseline)
    for rec in filtered:
        ing_text = " ".join(
            str(i) for i in (rec.get("ingredients") or [])
        ).lower()
        assert "mushroom" not in ing_text


def test_excluded_ingredient_conflict_with_basket_returns_422(base_url):
    r = requests.post(
        f"{base_url}/api/recommend/hard",
        json={
            "ingredients": ["chicken", "cilantro"],
            "excluded_ingredients": ["cilantro"],
        },
        timeout=30,
    )
    assert r.status_code == 422
    assert "conflict" in r.json().get("detail", "").lower()


def test_plant_based_filters_results(base_url):
    baseline = requests.post(
        f"{base_url}/api/recommend/hard",
        json={"ingredients": ["tofu", "spinach"], "top_k": 10},
        timeout=60,
    ).json()
    plant = requests.post(
        f"{base_url}/api/recommend/hard",
        json={
            "ingredients": ["tofu", "spinach"],
            "plant_based": True,
            "top_k": 10,
        },
        timeout=60,
    ).json()
    assert len(plant) <= len(baseline)


def test_xiaozhi_forwards_preferences(base_url):
    body = {
        "ingredients": ["chicken"],
        "allergens": ["peanut"],
        "excluded_ingredients": ["mushrooms"],
        "plant_based": False,
        "top_k": 10,
    }
    hard_r = requests.post(f"{base_url}/api/recommend/hard", json=body, timeout=60)
    xiaozhi_r = requests.post(f"{base_url}/api/xiaozhi/recommend", json=body, timeout=60)
    if xiaozhi_r.status_code in (404, 405):
        pytest.skip("xiaozhi router not deployed on this host")
    assert hard_r.status_code == 200
    assert xiaozhi_r.status_code == 200
    hard_titles = {r["title"] for r in hard_r.json()}
    xiaozhi_titles = {r["title"] for r in xiaozhi_r.json()["recipes"]}
    assert xiaozhi_titles == hard_titles
