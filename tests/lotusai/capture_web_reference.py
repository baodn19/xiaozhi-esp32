#!/usr/bin/env python3
"""Capture recipe titles from the API into golden_recipes.json fixtures."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

import requests

FIXTURES_PATH = Path(__file__).parent / "fixtures" / "golden_recipes.json"

SCENARIOS = [
    {
        "inputs": {"ingredients": ["chicken", "rice"], "top_k": 3},
        "notes": "baseline — no preference filters",
    },
    {
        "inputs": {
            "ingredients": ["chicken", "rice"],
            "allergens": ["peanut"],
            "top_k": 3,
        },
        "notes": "peanut allergen filter",
    },
    {
        "inputs": {
            "ingredients": ["chicken", "tomato"],
            "excluded_ingredients": ["mushrooms"],
            "top_k": 3,
        },
        "notes": "mushroom exclusion on chicken+tomato basket",
    },
    {
        "inputs": {
            "ingredients": ["tofu", "spinach"],
            "plant_based": True,
            "top_k": 3,
        },
        "notes": "plant-based filter",
    },
    {
        "inputs": {
            "ingredients": ["chicken"],
            "cooking_tools": ["stove"],
            "top_k": 3,
        },
        "notes": "cooking tool filter",
    },
]


def capture(base_url: str) -> list[dict]:
    fixtures: list[dict] = []
    baseline_titles: set[str] | None = None

    for scenario in SCENARIOS:
        inputs = scenario["inputs"]
        r = requests.post(
            f"{base_url}/api/recommend/hard",
            json=inputs,
            timeout=60,
        )
        r.raise_for_status()
        recipes = r.json()
        titles = [rec.get("title", "") for rec in recipes if rec.get("title")]

        entry = {
            "inputs": inputs,
            "expected_titles": titles,
            "notes": scenario["notes"],
        }

        if inputs.get("allergens") and baseline_titles is not None:
            excluded = baseline_titles - set(titles)
            if excluded:
                entry["excluded_titles"] = sorted(excluded)[:5]

        if inputs.get("excluded_ingredients"):
            entry["excluded_titles"] = [
                t for t in titles
                for excl in inputs["excluded_ingredients"]
                for rec in recipes
                if rec.get("title") == t
                and excl.lower() in " ".join(rec.get("ingredients") or []).lower()
            ]

        fixtures.append(entry)

        if "allergens" not in inputs and "excluded_ingredients" not in inputs:
            baseline_titles = set(titles)

    return fixtures


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base-url",
        default=os.environ.get("LOTUSAI_BASE_URL", "https://lotusfoodasmedicine.com"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=FIXTURES_PATH,
    )
    args = parser.parse_args()
    base_url = args.base_url.rstrip("/")

    fixtures = capture(base_url)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(fixtures, f, indent=2)
        f.write("\n")
    print(f"Wrote {len(fixtures)} fixtures to {args.output}")


if __name__ == "__main__":
    main()
