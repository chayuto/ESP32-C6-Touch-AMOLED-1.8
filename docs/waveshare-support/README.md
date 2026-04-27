# Waveshare Support Correspondence Archive

Verbatim records of technical support exchanges with Waveshare about the
ESP32-C6-Touch-AMOLED-1.8 board. Kept here so anyone working on this repo can
see what we've already asked, what answers we got, and whether those answers
were useful — without re-running round-trips that take days.

## Conventions

- One file per support ticket, named `<ticket-id>-<short-slug>.md`.
- File contains: ticket metadata, every message verbatim (ours and theirs, in
  chronological order), and an **Assessment** section summarizing whether the
  vendor answer was correct / useful / authoritative.
- Do not paraphrase vendor replies — copy them exactly. First-line support
  sometimes gives answers that look plausible but are wrong at the level
  we're asking about, and the exact wording matters for future re-reads.
- Link each ticket from the relevant technical doc under
  `docs/board-research/` (e.g. `18-sd-display-spi-sharing.md`) so readers
  hitting the topic can find the correspondence without knowing it exists.

## General observations

- **Waveshare first-line support answers at the schematic / pin-map level.**
  They are reliable for questions like "which GPIO is the touch interrupt?"
  but struggle with questions about SoC peripheral internals, driver
  architecture, or ESP-IDF specifics. For those, prefer ESP-IDF docs,
  Espressif GitHub issues, and the `esp-idf` / `esp-bsp` source trees.
- **Language round-trip matters.** Replies are translated from Chinese and
  sometimes lose the distinction we care about (e.g. "SPI peripheral host"
  vs. "SPI protocol mode"). Quote the exact English reply and be precise in
  follow-ups.
- **Support and sales are separate channels.** Technical questions →
  `support@waveshare.com`. Order / shipping / RMA → `sales@waveshare.com`.

## Index

| Ticket  | Topic                                 | Replying engineer | Dates                    | Outcome                               | File                                                                 |
|---------|---------------------------------------|-------------------|--------------------------|---------------------------------------|----------------------------------------------------------------------|
| #235269 | SD card + AMOLED display SPI2 sharing | 赵玉娇 (Zhao Yujiao) | 2026-04-05 → 2026-04-18  | Confirmed after ESP-IDF-source escalation | [235269-sd-display-spi-sharing.md](235269-sd-display-spi-sharing.md) |
