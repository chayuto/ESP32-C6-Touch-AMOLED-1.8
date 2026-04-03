# MCP Tool Design Checklist

Use this when designing or reviewing MCP tool schemas for any project — embedded or cloud.
Based on: *Advanced Architectural Paradigms and Optimization Strategies for MCP Implementations* (2026-03)

---

## 1. Tool Budget

- **5–8 tools maximum** per server for optimal LLM attention and 80% task success rate
- Every tool added beyond 8 degrades selection accuracy — consolidate or remove before adding
- Group related operations behind a single higher-level tool rather than exposing granular endpoints

## 2. Six-Component Description Framework

Every tool description MUST include all six. 97.1% of production tools fail at least one.

```
[PURPOSE]      One sentence: what it does and its primary goal.
[GUIDANCE]     When to call it. What to call before it (prerequisites). Sequential order if required.
[LIMITATIONS]  Hard bounds: max array size, max string length, coordinate range, unsupported modes.
[PARAMETERS]   Type, range, default, and format for every input field.
[COMPLETENESS] Any side effects (e.g., "this clears all previous drawing").
[EXEMPLAR]     Brief example parameter values — not full JSON, just values. Omit if description is already clear.
```

**Anti-pattern:** Over-augmented descriptions inflate LLM execution steps by 67%.
Keep descriptions concise — purpose + limits + parameter transparency is the minimum viable set.

## 3. Negative Guidance (Critical)

State explicitly when NOT to use each tool. Missing this is the #1 cause of LLM dead-end paths.

Template:
```
"Do NOT use [tool] for [wrong use case]. Use [correct tool] instead."
"Do NOT pass more than [N] items — excess are silently dropped."
"Do NOT call this if [precondition not met]."
```

## 4. Naming Conventions

| Rule | Good | Bad |
|---|---|---|
| verb_noun format | `draw_rect`, `clear_screen` | `rectangle`, `screenClear` |
| No jargon or acronyms | `search_products` | `prod_lookup_v2` |
| Consistent field names across tools | all use `r,g,b` | one uses `color`, another `rgb` |
| Outputs pipeline into next tool inputs | tool A returns `user_id`, tool B takes `user_id` | tool A returns `userId`, tool B takes `id` |

## 5. Parameter Schema Requirements

In the JSON Schema `inputSchema` object:
- Mark all required fields in the `"required"` array at root level
- Add `"minimum"` / `"maximum"` for all numeric fields with bounds
- Add `"maxLength"` for all string fields
- Add `"maxItems"` for all array fields
- Add an `"examples"` or `"default"` key for optional parameters

```json
"x": {
  "type": "integer",
  "description": "Left edge of rectangle. Range: 0-367.",
  "minimum": 0,
  "maximum": 367
}
```

## 6. Error Response Design

Errors must be **self-correcting** — give the LLM enough info to fix and retry autonomously.

**Bad:** `"error": "invalid params"`
**Good:** `"x out of range: got 400, must be 0-367"`

### Two distinct error channels — do not mix them:

| Error type | When | Response shape |
|---|---|---|
| Protocol error | Parse failure, unknown method, server crash | JSON-RPC `"error"` object at top level |
| Tool error | Bad args, resource unavailable, operation failed | `"result"` with `"isError": true` in content |

**Tool error (correct):**
```json
{
  "jsonrpc": "2.0", "id": 1,
  "result": {
    "content": [{"type": "text", "text": "x out of range: got 400, must be 0-367"}],
    "isError": true
  }
}
```

## 7. Tool Ordering in `tools/list`

LLM attention is front-weighted. Order matters:
1. Discovery/info tool first (e.g., `get_screen_info`, `list_sensors`)
2. Reset/init tool second (e.g., `clear_screen`, `create_session`)
3. Most commonly used tools next
4. Specialized or rare tools last

## 8. Visual Feedback — Image Content Type

For any MCP server driving a display, add a snapshot tool.
On this board: downsample 2× (184×224) before JPEG encoding to fit in RAM.
Use `espressif/esp_new_jpeg` (software encoder, works on C6).
Static-allocate RGB888 and JPEG buffers in BSS to avoid heap fragmentation.
Protect pixel buffer with mutex — rendering and snapshot run in different tasks.

## 9. Discovery Tool Pattern

For any server with bounded resources, expose a no-parameter discovery tool first:
- Screen dimensions (368×448), available fonts
- Sensor ranges (accelerometer ±4g, gyroscope ±512°/s)
- Available peripherals and their state
- "Call this before any other tool"

## 10. Board-Specific MCP Considerations

This board has rich peripherals that make interesting MCP tools:
- **Display canvas** — draw/text/shapes on 368×448 AMOLED
- **Touch input** — report touch events, gesture recognition
- **IMU data** — accelerometer/gyroscope readings, orientation
- **Audio** — play tones, record audio clips
- **Battery** — voltage, percentage, charging status
- **RTC** — get/set time, set alarms
- **Power** — brightness control, sleep modes

**GPIO tools are very limited** on this board — almost all GPIOs are used by onboard peripherals.
Unlike the LCD-1.47 which had 8 safe GPIO pins, this board has nearly zero free.
SD card pins (6,10,11,18) or audio pins (19-23) could be repurposed if those features aren't needed.

---

## Quick Self-Review Checklist

Before shipping any `tools/list` response:

- [ ] Total tool count ≤ 8
- [ ] Every description has: purpose, guidance, limitations, parameter transparency
- [ ] Every tool has at least one "Do NOT" negative guidance statement
- [ ] All tools use consistent field names
- [ ] All numeric params have `minimum`/`maximum` in schema
- [ ] All string params have `maxLength`
- [ ] Error messages include the bad value and the valid range
- [ ] Discovery/info tool listed first
- [ ] Snapshot tool exists if server drives a display
- [ ] `GET /ping` health endpoint exists
