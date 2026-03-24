/*
 * C++ Class Binding Example for Mobius
 * 
 * This example demonstrates how to bind C++ classes to the Mobius scripting
 * language using the userdata type. It shows:
 * 
 * - Creating C++ objects accessible from Mobius
 * - Binding C++ methods as Mobius functions
 * - Automatic cleanup when objects go out of scope
 * - Type safety and error handling
 */

#include <iostream>
#include <string>
#include <cmath>
#include <cstring>

#include <mobius/mobius.h>
#include <mobius/mobius_plugin.h>

// ============================================================================
// EXAMPLE C++ CLASSES
// ============================================================================

class Vector3 {
private:
    double x_, y_, z_;

public:
    Vector3(double x = 0, double y = 0, double z = 0) : x_(x), y_(y), z_(z) {}
    
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
    
    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
    void setZ(double z) { z_ = z; }
    
    double length() const {
        return std::sqrt(x_ * x_ + y_ * y_ + z_ * z_);
    }
    
    void normalize() {
        double len = length();
        if (len > 0) {
            x_ /= len;
            y_ /= len;
            z_ /= len;
        }
    }
    
    Vector3 add(const Vector3& other) const {
        return Vector3(x_ + other.x_, y_ + other.y_, z_ + other.z_);
    }
    
    double dot(const Vector3& other) const {
        return x_ * other.x_ + y_ * other.y_ + z_ * other.z_;
    }
    
    std::string toString() const {
        return "Vector3(" + std::to_string(x_) + ", " + 
               std::to_string(y_) + ", " + std::to_string(z_) + ")";
    }
};

class PlayerObj {
private:
    std::string name_;
    Vector3 position_;
    int health_;
    
public:
    PlayerObj(const std::string& name) : name_(name), position_(0, 0, 0), health_(100) {}
    
    const std::string& name() const { return name_; }
    const Vector3& position() const { return position_; }
    int health() const { return health_; }
    
    void setPosition(const Vector3& pos) { position_ = pos; }
    void setHealth(int health) { health_ = std::max(0, std::min(100, health)); }
    
    void move(double dx, double dy, double dz) {
        position_ = Vector3(position_.x() + dx, position_.y() + dy, position_.z() + dz);
    }
    
    void takeDamage(int damage) {
        health_ = std::max(0, health_ - damage);
    }
    
    bool isAlive() const { return health_ > 0; }
    
    std::string toString() const {
        return "Player(" + name_ + ", " + position_.toString() + ", HP: " + std::to_string(health_) + ")";
    }
};

// ============================================================================
// DESTRUCTOR FUNCTIONS
// ============================================================================

void vector3_destructor(void* ptr) {
    if (ptr) {
        delete static_cast<Vector3*>(ptr);
    }
}

void player_destructor(void* ptr) {
    if (ptr) {
        delete static_cast<PlayerObj*>(ptr);
    }
}

// ============================================================================
// HELPER: Extract typed userdata from the stack
// ============================================================================

static void* get_userdata_of_type(MobiusState* state, int idx, const char* expected_type) {
    if (!mobius_stack_isUserdata(state, idx))
        return nullptr;
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!type_name || std::strcmp(type_name, expected_type) != 0)
        return nullptr;
    return ptr;
}

// ============================================================================
// VECTOR3 MOBIUS BINDINGS
// ============================================================================

int vector3_new(MobiusState* state, int arg_count) {
    double x = 0, y = 0, z = 0;
    
    if (arg_count >= 1 && mobius_stack_isNumber(state, -arg_count))
        x = mobius_stack_asFloat64(state, -arg_count);
    if (arg_count >= 2 && mobius_stack_isNumber(state, -arg_count + 1))
        y = mobius_stack_asFloat64(state, -arg_count + 1);
    if (arg_count >= 3 && mobius_stack_isNumber(state, -arg_count + 2))
        z = mobius_stack_asFloat64(state, -arg_count + 2);
    
    if (arg_count > 0)
        mobius_stack_pop(state, arg_count);
    
    Vector3* vec = new Vector3(x, y, z);
    mobius_stack_pushUserdata(state, vec, vector3_destructor, "Vector3", sizeof(Vector3));
    return 1;
}

int vector3_length_fn(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "vector3_length() expects 1 argument");
    
    Vector3* vec = static_cast<Vector3*>(get_userdata_of_type(state, -1, "Vector3"));
    if (!vec)
        return mobius_error(state, "vector3_length() expects a Vector3");
    
    double len = vec->length();
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, len);
    return 1;
}

int vector3_normalize_fn(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "vector3_normalize() expects 1 argument");
    
    Vector3* vec = static_cast<Vector3*>(get_userdata_of_type(state, -1, "Vector3"));
    if (!vec)
        return mobius_error(state, "vector3_normalize() expects a Vector3");
    
    vec->normalize();
    mobius_stack_pop(state, 1);
    mobius_stack_pushNil(state);
    return 1;
}

int vector3_add_fn(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "vector3_add() expects 2 arguments");
    
    Vector3* vec1 = static_cast<Vector3*>(get_userdata_of_type(state, -2, "Vector3"));
    Vector3* vec2 = static_cast<Vector3*>(get_userdata_of_type(state, -1, "Vector3"));
    if (!vec1 || !vec2)
        return mobius_error(state, "vector3_add() expects two Vector3 arguments");
    
    Vector3* result = new Vector3(vec1->add(*vec2));
    mobius_stack_pop(state, 2);
    mobius_stack_pushUserdata(state, result, vector3_destructor, "Vector3", sizeof(Vector3));
    return 1;
}

int vector3_dot_fn(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "vector3_dot() expects 2 arguments");
    
    Vector3* vec1 = static_cast<Vector3*>(get_userdata_of_type(state, -2, "Vector3"));
    Vector3* vec2 = static_cast<Vector3*>(get_userdata_of_type(state, -1, "Vector3"));
    if (!vec1 || !vec2)
        return mobius_error(state, "vector3_dot() expects two Vector3 arguments");
    
    double result = vec1->dot(*vec2);
    mobius_stack_pop(state, 2);
    mobius_stack_pushFloat64(state, result);
    return 1;
}

int vector3_tostring_fn(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "vector3_tostring() expects 1 argument");
    
    Vector3* vec = static_cast<Vector3*>(get_userdata_of_type(state, -1, "Vector3"));
    if (!vec)
        return mobius_error(state, "vector3_tostring() expects a Vector3");
    
    std::string s = vec->toString();
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, s.c_str());
    return 1;
}

// ============================================================================
// PLAYER MOBIUS BINDINGS
// ============================================================================

int player_new(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "Player() expects 1 argument (name)");
    if (!mobius_stack_isString(state, -1))
        return mobius_error(state, "Player() expects a string name");
    
    const char* name = mobius_stack_asString(state, -1);
    mobius_stack_pop(state, 1);
    
    PlayerObj* player = new PlayerObj(std::string(name));
    mobius_stack_pushUserdata(state, player, player_destructor, "Player", sizeof(PlayerObj));
    return 1;
}

int player_get_health(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "player_get_health() expects 1 argument");
    
    PlayerObj* player = static_cast<PlayerObj*>(get_userdata_of_type(state, -1, "Player"));
    if (!player)
        return mobius_error(state, "player_get_health() expects a Player");
    
    int hp = player->health();
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, hp);
    return 1;
}

int player_take_damage(MobiusState* state, int arg_count) {
    if (arg_count != 2)
        return mobius_error(state, "player_take_damage() expects 2 arguments");
    
    PlayerObj* player = static_cast<PlayerObj*>(get_userdata_of_type(state, -2, "Player"));
    if (!player)
        return mobius_error(state, "player_take_damage() first arg must be a Player");
    if (!mobius_stack_isNumber(state, -1))
        return mobius_error(state, "player_take_damage() second arg must be a number");
    
    int damage = (int)mobius_stack_asInt64(state, -1);
    mobius_stack_pop(state, 2);
    
    player->takeDamage(damage);
    mobius_stack_pushNil(state);
    return 1;
}

int player_is_alive(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "player_is_alive() expects 1 argument");
    
    PlayerObj* player = static_cast<PlayerObj*>(get_userdata_of_type(state, -1, "Player"));
    if (!player)
        return mobius_error(state, "player_is_alive() expects a Player");
    
    bool alive = player->isAlive();
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, alive);
    return 1;
}

int player_tostring(MobiusState* state, int arg_count) {
    if (arg_count != 1)
        return mobius_error(state, "player_tostring() expects 1 argument");
    
    PlayerObj* player = static_cast<PlayerObj*>(get_userdata_of_type(state, -1, "Player"));
    if (!player)
        return mobius_error(state, "player_tostring() expects a Player");
    
    std::string s = player->toString();
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, s.c_str());
    return 1;
}

// ============================================================================
// MAIN EXAMPLE
// ============================================================================

int main() {
    std::cout << "=== Mobius C++ Class Binding Example ===\n" << std::endl;
    
    MobiusState* state = mobius_new_state(NULL);
    if (!state) {
        std::cerr << "Failed to create Mobius state" << std::endl;
        return 1;
    }
    
    if (mobius_init_stdlib(state) != MOBIUS_OK) {
        std::cerr << "Failed to initialize Mobius stdlib" << std::endl;
        mobius_free_state(state);
        return 1;
    }
    
    mobius_register_function(state, "Vector3", vector3_new);
    mobius_register_function(state, "vector3_length", vector3_length_fn);
    mobius_register_function(state, "vector3_normalize", vector3_normalize_fn);
    mobius_register_function(state, "vector3_add", vector3_add_fn);
    mobius_register_function(state, "vector3_dot", vector3_dot_fn);
    mobius_register_function(state, "vector3_tostring", vector3_tostring_fn);
    
    mobius_register_function(state, "Player", player_new);
    mobius_register_function(state, "player_get_health", player_get_health);
    mobius_register_function(state, "player_take_damage", player_take_damage);
    mobius_register_function(state, "player_is_alive", player_is_alive);
    mobius_register_function(state, "player_tostring", player_tostring);
    
    const char* test_script = R"(
        print("=== Vector3 Tests ===");
        var v1 = Vector3(1.0, 2.0, 3.0);
        var v2 = Vector3(4.0, 5.0, 6.0);
        
        print("v1:", vector3_tostring(v1));
        print("v2:", vector3_tostring(v2));
        print("v1 length:", vector3_length(v1));
        
        var v3 = vector3_add(v1, v2);
        print("v1 + v2 =", vector3_tostring(v3));
        
        var dot_product = vector3_dot(v1, v2);
        print("v1 . v2 =", dot_product);
        
        print("\n=== Player Tests ===");
        var player = Player("Hero");
        print("Created:", player_tostring(player));
        print("Health:", player_get_health(player));
        print("Alive?", player_is_alive(player));
        
        player_take_damage(player, 30);
        print("After taking 30 damage:");
        print("Health:", player_get_health(player));
        print("Alive?", player_is_alive(player));
        
        player_take_damage(player, 80);
        print("After taking 80 more damage:");
        print("Health:", player_get_health(player));
        print("Alive?", player_is_alive(player));
        
        print("\n=== Automatic Cleanup Test ===");
        print("Objects will be automatically destroyed when they go out of scope...");
    )";
    
    std::cout << "Executing Mobius script with C++ objects:\n" << std::endl;
    
    int result = mobius_exec_string(state, test_script);
    if (result != MOBIUS_OK) {
        std::cerr << "Script execution failed" << std::endl;
    }
    
    std::cout << "\nCleaning up Mobius state..." << std::endl;
    
    mobius_free_state(state);
    
    std::cout << "\nExample completed!" << std::endl;
    return 0;
}
