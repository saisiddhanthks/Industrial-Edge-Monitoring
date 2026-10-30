"""Generate a grounded maintenance report (and optional Q&A) from the device log.

The LLM receives only computed statistics and recent readings and is told not to
invent values. Without a GROQ_API_KEY the script writes a deterministic report
built from the same statistics, so it always produces output.

Usage:
    export GROQ_API_KEY=...            # optional
    python ai_report.py                # writes reports/industrial_ai_report.{txt,pdf}
    python ai_report.py --chat         # interactive Q&A over the same evidence
"""
from __future__ import annotations

import argparse
import os
from datetime import datetime, timezone
from pathlib import Path

import pandas as pd
import requests

DATA = Path(__file__).resolve().parents[1] / "data" / "industrial_dataset.csv"
OUT_DIR = Path(__file__).resolve().parent / "reports"
GROQ_URL = "https://api.groq.com/openai/v1/chat/completions"
MODELS = ["llama-3.3-70b-versatile", "llama-3.1-8b-instant", "qwen/qwen3-32b"]


def evidence(df: pd.DataFrame) -> str:
    latest = df.iloc[-1]
    return f"""SUMMARY ({len(df)} readings)
- Mean temperature: {df['Temp'].mean():.2f} C (max {df['Temp'].max():.2f})
- Mean humidity: {df['Humidity'].mean():.2f} %
- Mean vibration intensity: {df['VibrationIntensity'].mean():.2f} (max {df['VibrationIntensity'].max()})
- CRITICAL readings: {(df['Level'] == 'CRITICAL').sum()}  |  NORMAL readings: {(df['Level'] == 'NORMAL').sum()}
- Peak risk score: {df['Risk'].max()}

LATEST READING
Temp {latest['Temp']} C, humidity {latest['Humidity']} %, vibration {latest['VibrationIntensity']}, risk {latest['Risk']}, level {latest['Level']}

RECENT READINGS
{df.tail(10).to_string(index=False)}"""


def ask_groq(system: str, user: str) -> str | None:
    key = os.getenv("GROQ_API_KEY", "").strip()
    if not key:
        return None
    headers = {"Authorization": f"Bearer {key}", "Content-Type": "application/json"}
    for model in MODELS:
        payload = {"model": model, "temperature": 0.3,
                   "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}]}
        try:
            r = requests.post(GROQ_URL, json=payload, headers=headers, timeout=60)
            if r.status_code == 200:
                return r.json()["choices"][0]["message"]["content"]
            print(f"{model}: HTTP {r.status_code}")
        except requests.RequestException as exc:
            print(f"{model}: {exc}")
    return None


def fallback_report(df: pd.DataFrame) -> str:
    crit = (df["Level"] == "CRITICAL").mean() * 100
    return (f"Executive summary\n{crit:.1f}% of {len(df)} readings were CRITICAL. "
            f"Mean temperature {df['Temp'].mean():.1f} C, peak {df['Temp'].max():.1f} C; "
            f"peak vibration intensity {df['VibrationIntensity'].max()}.\n\n"
            "Recommendations\nInspect mounting and bearings on machines with repeated vibration alarms, "
            "verify cooling when temperature exceeds 30 C, and review the sensitivity setting if alarms are frequent.")


def write_pdf(text: str, path: Path) -> None:
    from reportlab.lib.pagesizes import A4
    from reportlab.lib.styles import getSampleStyleSheet
    from reportlab.lib.units import inch
    from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer

    styles = getSampleStyleSheet()
    story = [Paragraph("Industrial AI Monitoring Report", styles["Title"]),
             Paragraph(f"Generated {datetime.now(timezone.utc):%Y-%m-%d %H:%M} UTC", styles["Normal"]),
             Spacer(1, 0.25 * inch)]
    for para in filter(None, (p.strip() for p in text.split("\n\n"))):
        safe = para.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\n", "<br/>")
        story += [Paragraph(safe, styles["Normal"]), Spacer(1, 0.12 * inch)]
    SimpleDocTemplate(str(path), pagesize=A4).build(story)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chat", action="store_true", help="interactive Q&A over the device evidence")
    args = parser.parse_args()
    df = pd.read_csv(DATA)
    ev = evidence(df)
    system = "You are a precise industrial condition-monitoring analyst. Use only the data provided. Never invent values."

    if args.chat:
        print("Industrial assistant ready (type 'exit' to stop).")
        while (q := input("You: ").strip()).lower() not in {"exit", "quit"}:
            if q:
                print("\nAssistant:", ask_groq(system, f"{ev}\n\nQuestion: {q}") or "Set GROQ_API_KEY to enable answers.", "\n")
        return

    prompt = ("Write a maintenance report with sections: Executive Summary, System Performance, "
              "Risk Assessment, Key Observations, Recommendations, Outlook.\n\n" + ev)
    text = ask_groq(system, prompt) or fallback_report(df)
    OUT_DIR.mkdir(exist_ok=True)
    (OUT_DIR / "industrial_ai_report.txt").write_text(text, encoding="utf-8")
    write_pdf(text, OUT_DIR / "industrial_ai_report.pdf")
    print("Saved", OUT_DIR / "industrial_ai_report.txt", "and .pdf")


if __name__ == "__main__":
    main()
