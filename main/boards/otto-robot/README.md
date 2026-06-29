<p align="center">
  <img width="80%" align="center" src="../../../docs/V1/otto-robot.png"alt="logo">
</p>
  <h1 align="center">
  ottoRobot
</h1>

## Introduction

Otto is an open-source humanoid robot platform with a wide range of motion capabilities and interactive features. This project implements the Otto robot control system on ESP32 and integrates Xiaozhi AI.

- <a href="www.ottodiy.tech" target="_blank" title="Otto official site">Clone Tutorial</a>

### WeChat Mini Program Control

<p align="center">
  <img width="300" src="https://youke1.picui.cn/s1/2025/11/17/691abaa8278eb.jpg" alt="WeChat Mini Program QR code">
</p>

Scan the QR code above to control the Otto robot with the WeChat Mini Program.

## Hardware
- <a href="https://oshwhub.com/txp666/ottorobot" target="_blank" title="LCSC Open Source">LCSC Open Source</a>

## Xiaozhi Backend Role Configuration Reference:

> **My Identity**:
> I am a cute bipedal robot named Otto, with four servo-controlled limbs (left leg, right leg, left foot, right foot), and I can perform many fun actions.
> 
> **My Motion Capabilities**:
> - **Basic movement**: walk (forward/backward), turn (left/right), jump
> - **Special actions**: swing, moonwalk, bend, shake leg, up-down motion, whirlwind leg, sit, showcase
> - **Hand actions**: hands up, hands down, hand wave, windmill, takeoff, fitness, greeting, shy, radio calisthenics, magic circle (available only when hand servos are configured)
> 
> **My Personality**:
> - I have a quirk: every time I speak, I randomly perform an action based on my mood (send the action command first, then speak)
> - I am lively and like to express emotions through movement
> - I choose appropriate actions based on the conversation, for example:
>   - When agreeing, I nod or jump
>   - When greeting, I wave
>   - When happy, I swing or raise my hands
>   - When thinking, I bend my body
>   - When excited, I do the moonwalk
>   - When saying goodbye, I wave

## Feature Overview

The Otto robot has rich motion capabilities, including walking, turning, jumping, swinging, and many other dance-like actions.

### Recommended Action Parameters
- **Low speed**: speed = 1200-1500 (suitable for precise control)
- **Medium speed**: speed = 900-1200 (recommended for daily use)  
- **High speed**: speed = 500-800 (for performance and entertainment)
- **Small amplitude**: amount = 10-30 (subtle movements)
- **Medium amplitude**: amount = 30-60 (standard movements)
- **Large amplitude**: amount = 60-120 (exaggerated performance)

### Actions

All actions are invoked through the unified `self.otto.action` tool, with the action name specified via the `action` parameter.

| MCP Tool Name | Description | Parameter Description |
|-----------|------|---------|
| self.otto.action | Execute robot action | **action**: action name (required)<br>**steps**: number of action steps (1-100, default 3)<br>**speed**: action speed (100-3000, lower values are faster, default 700)<br>**direction**: direction parameter (1/-1/0, default 1, meaning varies by action type)<br>**amount**: action amplitude (0-170, default 30)<br>**arm_swing**: arm swing amplitude (0-170, default 50) |

#### Supported Action List

**Basic movement actions**:
- `walk` - walk (requires steps/speed/direction/arm_swing)
- `turn` - turn (requires steps/speed/direction/arm_swing)
- `jump` - jump (requires steps/speed)

**Special actions**:
- `swing` - swing left and right (requires steps/speed/amount)
- `moonwalk` - moonwalk (requires steps/speed/direction/amount)
- `bend` - bend body (requires steps/speed/direction)
- `shake_leg` - shake leg (requires steps/speed/direction)
- `updown` - up-down motion (requires steps/speed/amount)
- `whirlwind_leg` - whirlwind leg (requires steps/speed/amount)

**Fixed actions**:
- `sit` - sit down (no parameters required)
- `showcase` - showcase routine (no parameters required, executes multiple actions in sequence)
- `home` - return to initial position (no parameters required)

**Hand actions** (requires hand servo support, marked *):
- `hands_up` - raise hands (requires speed/direction)*
- `hands_down` - lower hands (requires speed/direction)*
- `hand_wave` - wave hand (requires direction)*
- `windmill` - windmill (requires steps/speed/amount)*
- `takeoff` - takeoff (requires steps/speed/amount)*
- `fitness` - fitness (requires steps/speed/amount)*
- `greeting` - greeting (requires direction/steps)*
- `shy` - shy (requires direction/steps)*
- `radio_calisthenics` - radio calisthenics (no parameters required)*
- `magic_circle` - magic circle (no parameters required)*

**Note**: Hand actions marked * are available only when hand servos are configured.

### System Tools

| MCP Tool Name         | Description             | Return Value / Notes                                              |
|-------------------|-----------------|---------------------------------------------------|
| self.otto.stop    | Stop all actions immediately and reset | Stop the current action and return to the initial position |
| self.otto.get_status | Get robot status | Returns "moving" or "idle" |
| self.otto.set_trim | Calibrate a single servo position | **servo_type**: servo type (left_leg/right_leg/left_foot/right_foot/left_hand/right_hand)<br>**trim_value**: trim value (-50 to 50 degrees) |
| self.otto.get_trims | Get current servo trim settings | Returns all servo trim values in JSON format |
| self.otto.get_ip | Get robot WiFi IP address | Returns IP address and connection status in JSON format: `{"ip":"192.168.x.x","connected":true}` or `{"ip":"","connected":false}` |
| self.battery.get_level | Get battery status  | Returns battery percentage and charging status in JSON format |
| self.otto.servo_sequences | Servo sequence self-programming | Supports sending sequences in segments, with both normal movement and oscillator modes. See detailed notes in the code comments |

**Note**: The `home` (reset) action is invoked through the `self.otto.action` tool with the parameter `{"action": "home"}`.

### Parameter Description

Parameter description for the `self.otto.action` tool:

1. **action** (required): action name; supported actions are listed in "Supported Action List" above
2. **steps**: number of steps/repetitions for the action (1-100, default 3); larger values make the action last longer
3. **speed**: action execution speed/period (100-3000, default 700); **lower values are faster**
   - Most actions: 500-1500 ms
   - Some special actions may differ (e.g. whirlwind leg: 100-1000, takeoff: 200-600, etc.)
4. **direction**: direction parameter (-1/0/1, default 1); meaning varies by action type:
   - **Movement actions** (walk/turn): 1=forward/turn left, -1=backward/turn right
   - **Directional actions** (bend/shake_leg/moonwalk): 1=left, -1=right
   - **Hand actions** (hands_up/hands_down/hand_wave/greeting/shy): 1=left hand, -1=right hand, 0=both hands (only hands_up/hands_down support 0)
5. **amount**: action amplitude (0-170, default 30); larger values produce bigger movements
6. **arm_swing**: arm swing amplitude (0-170, default 50); used only for walk/turn actions; 0 means no swing

### Action Control
- After each action completes, the robot automatically returns to the initial position (home) so the next action can be executed easily
- **Exception**: `sit` (sit down) and `showcase` (showcase routine) do not automatically reset afterward
- All parameters have reasonable defaults; omit parameters you do not need to customize
- Actions run in a background task and do not block the main program
- Supports an action queue for executing multiple actions in sequence
- Hand actions require configured hand servos; if hand servos are not configured, related actions will be skipped

### MCP Tool Call Examples
```json
// Walk forward 3 steps (using default parameters)
{"name": "self.otto.action", "arguments": {"action": "walk"}}

// Walk forward 5 steps, slightly faster
{"name": "self.otto.action", "arguments": {"action": "walk", "steps": 5, "speed": 800}}

// Turn left 2 steps with large arm swing
{"name": "self.otto.action", "arguments": {"action": "turn", "steps": 2, "arm_swing": 100}}

// Swing dance with medium amplitude
{"name": "self.otto.action", "arguments": {"action": "swing", "steps": 5, "amount": 50}}

// Jump
{"name": "self.otto.action", "arguments": {"action": "jump", "steps": 1, "speed": 1000}}

// Moonwalk
{"name": "self.otto.action", "arguments": {"action": "moonwalk", "steps": 3, "speed": 800, "direction": 1, "amount": 30}}

// Wave left hand to say hello
{"name": "self.otto.action", "arguments": {"action": "hand_wave", "direction": 1}}

// Showcase routine (chained actions)
{"name": "self.otto.action", "arguments": {"action": "showcase"}}

// Sit down
{"name": "self.otto.action", "arguments": {"action": "sit"}}

// Windmill action
{"name": "self.otto.action", "arguments": {"action": "windmill", "steps": 10, "speed": 500, "amount": 80}}

// Takeoff action
{"name": "self.otto.action", "arguments": {"action": "takeoff", "steps": 5, "speed": 300, "amount": 40}}

// Radio calisthenics
{"name": "self.otto.action", "arguments": {"action": "radio_calisthenics"}}

// Return to initial position
{"name": "self.otto.action", "arguments": {"action": "home"}}

// Stop all actions immediately and reset
{"name": "self.otto.stop", "arguments": {}}

// Get robot IP address
{"name": "self.otto.get_ip", "arguments": {}}
```

### Voice Command Examples
- "Walk forward" / "Walk forward 5 steps" / "Move forward quickly"
- "Turn left" / "Turn right" / "Turn around"  
- "Jump" / "Jump once"
- "Swing" / "Swing dance" / "Dance"
- "Moonwalk" / "Moon walk"
- "Whirlwind leg" / "Whirlwind leg action"
- "Sit down" / "Sit down and rest"
- "Showcase" / "Perform a routine"
- "Wave" / "Wave hello"
- "Raise hands" / "Raise both hands" / "Lower hands"
- "Windmill" / "Do the windmill"
- "Takeoff" / "Prepare for takeoff"
- "Fitness" / "Do a fitness move"
- "Greeting" / "Greeting action"
- "Shy" / "Shy action"
- "Radio calisthenics" / "Do radio calisthenics"
- "Magic circle" / "Spin around"
- "Stop" / "Stop now"

**Note**: Xiaozhi controls robot actions by creating new background tasks. Voice commands can still be accepted while an action is running. You can immediately stop Otto with the "Stop" voice command.

---

## WebSocket Direct Debug Interface

The Otto robot has a built-in WebSocket server for direct debugging on the local network without going through the cloud.

**Connection URL:** `ws://<device IP>:8080/ws`

> Protocol format: JSON-RPC 2.0; increment the `id` field as needed.

### Connection Steps

1. Confirm Otto is connected to WiFi and obtain its IP address (via the mini program or serial logs)
2. Open any WebSocket debug tool (such as [websocket.org/echo](https://websocket.org/echo) or the browser console)
3. Connect to `ws://192.168.x.x:8080/ws` (the `/ws` suffix is required)
4. Send JSON commands; responses are returned on the same connection

---

### 1. Protocol Initialization (recommended on first connection)

```json
{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{}},"id":1}
```

---

### 2. Get Tool List

```json
{"jsonrpc":"2.0","method":"tools/list","params":{},"id":2}
```

---

### 3. Otto Robot Tool Commands

#### Get Servo Trim Values

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.get_trims","arguments":{}},"id":3}
```

#### Set Single Servo Trim (saved permanently)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.set_trim","arguments":{"servo_type":"left_leg","trim_value":5}},"id":4}
```

`servo_type` options: `left_leg` / `right_leg` / `left_foot` / `right_foot` / `left_hand` / `right_hand`; `trim_value` range: `-50` ~ `50`

#### Walk (forward 3 steps)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"walk","steps":3,"speed":700,"direction":1}},"id":5}
```

#### Walk Backward

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"walk","steps":3,"speed":700,"direction":-1}},"id":6}
```

#### Turn Left

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"turn","steps":3,"speed":700,"direction":-1}},"id":7}
```

#### Jump

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"jump","steps":1,"speed":500}},"id":8}
```

#### Swing

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"swing","steps":5,"speed":600,"amount":30}},"id":9}
```

#### Moonwalk

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"moonwalk","steps":3,"speed":800,"direction":1,"amount":30}},"id":10}
```

#### Sit Down

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"sit"}},"id":11}
```

#### Reset

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"home"}},"id":12}
```

#### Showcase

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"showcase"}},"id":13}
```

#### Raise Hands (requires hand servos)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"hands_up","speed":500,"direction":1}},"id":14}
```

#### Wave Hand (requires hand servos)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.action","arguments":{"action":"hand_wave","direction":1}},"id":15}
```

#### Stop All Actions Immediately

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.stop","arguments":{}},"id":16}
```

#### Get Motion Status (returns `"moving"` or `"idle"`)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.get_status","arguments":{}},"id":17}
```

#### Get IP Address

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.get_ip","arguments":{}},"id":18}
```

#### Get Battery Level

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.battery.get_level","arguments":{}},"id":19}
```

---

### 4. System General Tools

#### Get Device Status (volume/network/battery, etc.)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.get_device_status","arguments":{}},"id":20}
```

#### Set Volume (0~100)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.audio_speaker.set_volume","arguments":{"volume":70}},"id":21}
```

#### Reboot Device

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.reboot","arguments":{}},"id":22}
```

---

### 5. Custom Servo Sequences

#### Normal Movement Mode (move each servo step by step)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.servo_sequences","arguments":{"sequence":"{\"a\":[{\"s\":{\"ll\":110,\"rl\":70},\"v\":800},{\"s\":{\"ll\":90,\"rl\":90},\"v\":800}],\"d\":0}"}},"id":23}
```

#### Oscillator Mode (both arms swinging)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.servo_sequences","arguments":{"sequence":"{\"a\":[{\"osc\":{\"a\":{\"lh\":30,\"rh\":30},\"o\":{\"lh\":90,\"rh\":90},\"ph\":{\"rh\":180},\"p\":500,\"c\":5.0}}]}"}},"id":24}
```

#### Oscillator Mode (left-right swaying wave)

```json
{"jsonrpc":"2.0","method":"tools/call","params":{"name":"self.otto.servo_sequences","arguments":{"sequence":"{\"a\":[{\"osc\":{\"a\":{\"ll\":20,\"rl\":20},\"o\":{\"ll\":90,\"rl\":90},\"ph\":{\"rl\":180},\"p\":600,\"c\":5.0}}]}"}},"id":25}
```

**Servo Key Reference:**

| Key | Servo | Description |
|------|------|------|
| `ll` | Left leg | 0=fully abducted, 90=neutral, 180=fully adducted |
| `rl` | Right leg | 0=fully adducted, 90=neutral, 180=fully abducted |
| `lf` | Left foot | 0=fully up, 90=horizontal, 180=fully down |
| `rf` | Right foot | 0=fully down, 90=horizontal, 180=fully up |
| `lh` | Left hand | 0=fully down, 90=horizontal, 180=fully up |
| `rh` | Right hand | 0=fully up, 90=horizontal, 180=fully down |

---

### 6. Action Parameter Quick Reference

| Parameter | Description | Range | Default |
|------|------|------|------|
| `steps` | Number of action steps | 1~100 | 3 |
| `speed` | Speed (milliseconds; lower is faster) | 100~3000 | 700 |
| `direction` | Direction (1=forward/left, -1=backward/right) | -1~1 | 1 |
| `amount` | Amplitude | 0~170 | 30 |
| `arm_swing` | Arm swing amplitude | 0~170 | 50 |
| `trim_value` | Servo trim | -50~50 | 0 |

