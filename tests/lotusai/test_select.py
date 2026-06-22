import pytest
import requests

pytestmark = pytest.mark.integration


@pytest.fixture(autouse=True)
def _require_integration(run_integration):
    if not run_integration:
        pytest.skip("pass --run-integration to hit live LotusAI API")


def test_select_returns_qr(base_url):
    rec_resp = requests.post(
        f"{base_url}/api/xiaozhi/recommend",
        json={"ingredients": ["chicken"], "top_k": 1},
        timeout=60,
    )
    if rec_resp.status_code in (404, 405):
        pytest.skip("xiaozhi router not deployed on this host")
    assert rec_resp.status_code == 200
    recipes = rec_resp.json().get("recipes") or []
    assert recipes, "recommend returned no recipes"
    pdf_key = recipes[0].get("pdf_key")
    assert pdf_key

    sel_resp = requests.post(
        f"{base_url}/api/xiaozhi/select",
        json={"pdf_key": pdf_key},
        timeout=60,
    )
    assert sel_resp.status_code == 200
    body = sel_resp.json()
    assert body.get("qr_base64")
    assert len(body["qr_base64"]) > 100
    assert body.get("spoken_confirm")
    assert body.get("title")
