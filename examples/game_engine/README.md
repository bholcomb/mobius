# Game Engine Example

This example demonstrates how to embed Mobius in a real-world application — a
simple 2D game engine where game logic is scripted in Mobius.

## Files

- `game_engine.cpp` - Game engine implementation with embedded Mobius
- `scripts/game_init.mob` - Mobius script for game initialization and event
  handlers (generated at runtime)

## Headers Used

```cpp
#include <mobius/mobius.h>          // state lifecycle, execution
#include <mobius/mobius_plugin.h>   // stack API, native functions
```

## Architecture

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Game Engine   │◄──►│  Mobius State   │◄──►│  Game Scripts   │
│   (C Code)      │    │  (Interpreter)  │    │  (.mob files)   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Game State    │    │   Custom API    │    │ Event Handlers  │
│ - Player pos    │    │ - get_health()  │    │ - on_damage()   │
│ - Health/Score  │    │ - spawn_enemy() │    │ - on_powerup()  │
│ - Enemies       │    │ - game_log()    │    │ - on_move()     │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Game API Functions

These C functions are registered via `mobius_register_function` and callable
from Mobius scripts:

| Function | Parameters | Returns | Description |
|----------|------------|---------|-------------|
| `get_player_pos()` | none | string | Player position as "x,y" |
| `set_player_pos(x, y)` | number, number | nil | Set player position |
| `get_player_health()` | none | integer | Current health |
| `set_player_health(h)` | number | nil | Set health (clamped 0–100) |
| `get_score()` | none | integer | Current score |
| `add_score(points)` | number | integer | Add points, return new score |
| `spawn_enemy(x, y, type)` | number, number, number | integer | Spawn enemy |
| `get_level()` | none | integer | Current level |
| `game_log(message)` | string | nil | Log a game message |

## Native Function Pattern

All game API functions use only the public stack API:

```cpp
int game_get_player_health(MobiusState* state, int arg_count) {
    if (arg_count != 0)
        return mobius_error(state, "get_player_health takes no arguments");
    if (!g_game)
        return mobius_error(state, "Game not initialized");

    mobius_stack_pushInt64(state, (int64_t)g_game->player.health);
    return 1;
}
```

## Script Example

The engine loads `scripts/game_init.mob` at startup:

```mobius
game_log("Initializing Mobius Adventure...");
set_player_pos(25, 25);
set_player_health(100);

func on_damage(amount) {
    var current_health = get_player_health();
    var new_health = current_health - int(amount);
    set_player_health(new_health);

    if (new_health <= 0) {
        game_log("Game Over! Final score: " + str(get_score()));
    } else {
        game_log("Took " + amount + " damage. Health: " + str(new_health));
    }
}
```

## Building and Running

```bash
./buildy -r -n 3
./bin/game_engine
```

## Use Cases

This pattern scales to larger game engines, application automation, and any
scenario requiring runtime-customizable behavior through scripts.
