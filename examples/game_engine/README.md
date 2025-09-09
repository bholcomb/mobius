# Game Engine Example

This example demonstrates how to embed Mobius in a real-world application - a simple 2D game engine where game logic is scripted in Mobius.

## Files

- `game_engine.c` - Main C implementation of the game engine
- `game_init.mob` - Mobius script for game initialization and event handlers

## Features Demonstrated

- **Application Integration**: Embedding Mobius in a C application
- **Custom API Exposure**: Registering C functions callable from scripts
- **Event-Driven Architecture**: Handling game events through scripts
- **Dynamic Script Loading**: Loading and executing scripts at runtime
- **State Management**: Sharing application state with scripts
- **Error Handling**: Robust error handling in embedded scenarios

## Architecture Overview

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

The example exposes these C functions to Mobius scripts:

| Function | Parameters | Returns | Description |
|----------|------------|---------|-------------|
| `get_player_pos()` | none | string | Get player position as "x,y" |
| `set_player_pos(x, y)` | float, float | nil | Set player position |
| `get_player_health()` | none | integer | Get current health |
| `set_player_health(health)` | integer | nil | Set health (0-100) |
| `get_score()` | none | integer | Get current score |
| `add_score(points)` | integer | integer | Add points to score |
| `spawn_enemy(x, y, type)` | float, float, int | integer | Spawn enemy at position |
| `get_level()` | none | integer | Get current level |
| `game_log(message)` | string | nil | Log a game message |

## Script Example

The engine automatically loads `game_init.mob`:

```javascript
// Game Initialization Script
game_log("Initializing Mobius Adventure...");

// Set up initial game state
set_player_pos(25, 25);
set_player_health(100);

// Define event handlers
func on_player_move(direction) {
    var pos = get_player_pos();
    game_log("Player moved " + direction + " to " + pos);
    
    // Award points for movement
    add_score(10);
}

func on_damage(amount) {
    var current_health = get_player_health();
    var new_health = current_health - int(amount);
    set_player_health(new_health);
    
    if (new_health <= 0) {
        game_log("Game Over! Final score: " + str(get_score()));
    } else {
        game_log("Player took " + amount + " damage. Health: " + str(new_health));
    }
}

func on_powerup(type) {
    if (type == "health") {
        var current_health = get_player_health();
        set_player_health(min(100, current_health + 25));
        game_log("Health restored!");
    } else if (type == "score") {
        add_score(500);
        game_log("Bonus points!");
    }
}
```

## Key Implementation Details

### 1. State Management

```c
typedef struct {
    Player player;
    Enemy enemies[10];
    int enemy_count;
    int game_running;
    int level;
    MobiusState* script_state;  // Embedded interpreter
} GameEngine;
```

### 2. Function Registration

```c
void init_game_engine(GameEngine* game) {
    // Create interpreter
    game->script_state = mobius_new_state();
    mobius_init_core(game->script_state);
    
    // Register game API
    mobius_register_function(game->script_state, "get_player_pos", 
                           game_get_player_pos, 0, "Get player position");
    mobius_register_function(game->script_state, "set_player_health", 
                           game_set_player_health, 1, "Set player health");
    // ... register other functions
}
```

### 3. Event Handling

```c
void run_game_event(GameEngine* game, const char* event_name, const char* event_data) {
    // Set event variables in script context
    char script[512];
    snprintf(script, sizeof(script), 
        "var event_name = \"%s\";\n"
        "var event_data = \"%s\";\n", 
        event_name, event_data ? event_data : "");
    mobius_exec_string(game->script_state, script);
    
    // Call event handler if it exists
    snprintf(script, sizeof(script), 
        "if (typeof(on_%s) == \"function\") {\n"
        "    on_%s(event_data);\n"
        "}", event_name, event_name);
    
    mobius_exec_string(game->script_state, script);
}
```

## Building and Running

### Prerequisites
- GCC with C99 support
- Mobius library (built with `make`)

### Building
```bash
make examples
```

### Running
```bash
./bin/game_engine
```

## Use Cases

This example is perfect for:
- Game development with scriptable logic
- Application automation
- Configuration-driven behavior
- Plugin architectures
- Educational purposes (embedding 101)

The pattern demonstrated here scales to:
- Larger game engines
- Web servers with scriptable routing
- Database systems with user-defined functions
- Any application needing runtime customization
