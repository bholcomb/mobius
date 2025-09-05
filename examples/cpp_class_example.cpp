/*
 * C++ Class Binding Example for Mobius
 * 
 * This example demonstrates how to bind C++ classes to the Mobius scripting
 * language using the new userdata type. It shows:
 * 
 * - Creating C++ objects accessible from Mobius
 * - Binding C++ methods as Mobius functions
 * - Automatic cleanup when objects go out of scope
 * - Type safety and error handling
 * 
 * Compile with: g++ -o bin/cpp_example examples/cpp_class_example.cpp -Lbuild -lmobius -lm -ldl
 */

#include <iostream>
#include <string>
#include <cmath>
#include <memory>

extern "C" {
    #include "../src/mobius/mobius.h"
}

// ============================================================================
// EXAMPLE C++ CLASSES
// ============================================================================

/**
 * Simple Vector3 class for 3D math
 */
class Vector3 {
private:
    double x_, y_, z_;

public:
    Vector3(double x = 0, double y = 0, double z = 0) : x_(x), y_(y), z_(z) {}
    
    // Getters
    double x() const { return x_; }
    double y() const { return y_; }
    double z() const { return z_; }
    
    // Setters
    void setX(double x) { x_ = x; }
    void setY(double y) { y_ = y; }
    void setZ(double z) { z_ = z; }
    
    // Vector operations
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

/**
 * Simple Player class for game objects
 */
class Player {
private:
    std::string name_;
    Vector3 position_;
    int health_;
    
public:
    Player(const std::string& name) : name_(name), position_(0, 0, 0), health_(100) {}
    
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
        std::cout << "Vector3 destroyed" << std::endl;
    }
}

void player_destructor(void* ptr) {
    if (ptr) {
        Player* player = static_cast<Player*>(ptr);
        std::cout << "Player '" << player->name() << "' destroyed" << std::endl;
        delete player;
    }
}

// ============================================================================
// VECTOR3 MOBIUS BINDINGS
// ============================================================================

int vector3_new(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    // vector3_new(x, y, z) -> Vector3 userdata
    double x = 0, y = 0, z = 0;
    
    if (arg_count >= 1 && mobius_is_float(args[0])) x = mobius_to_float(args[0]);
    if (arg_count >= 2 && mobius_is_float(args[1])) y = mobius_to_float(args[1]);
    if (arg_count >= 3 && mobius_is_float(args[2])) z = mobius_to_float(args[2]);
    
    Vector3* vec = new Vector3(x, y, z);
    *result = mobius_create_userdata(state, vec, vector3_destructor, "Vector3", sizeof(Vector3));
    
    return MOBIUS_OK;
}

int vector3_length(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    
    if (!mobius_is_userdata_type(args[0], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Vector3");
    }
    
    Vector3* vec = static_cast<Vector3*>(mobius_to_userdata(args[0]));
    *result = mobius_create_float(state, vec->length());
    
    return MOBIUS_OK;
}

int vector3_normalize(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    
    if (!mobius_is_userdata_type(args[0], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Vector3");
    }
    
    Vector3* vec = static_cast<Vector3*>(mobius_to_userdata(args[0]));
    vec->normalize();
    *result = mobius_create_nil(state);
    
    return MOBIUS_OK;
}

int vector3_add(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(2);
    
    if (!mobius_is_userdata_type(args[0], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Vector3");
    }
    if (!mobius_is_userdata_type(args[1], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "Second argument must be a Vector3");
    }
    
    Vector3* vec1 = static_cast<Vector3*>(mobius_to_userdata(args[0]));
    Vector3* vec2 = static_cast<Vector3*>(mobius_to_userdata(args[1]));
    
    Vector3* result_vec = new Vector3(vec1->add(*vec2));
    *result = mobius_create_userdata(state, result_vec, vector3_destructor, "Vector3", sizeof(Vector3));
    
    return MOBIUS_OK;
}

int vector3_dot(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(2);
    
    if (!mobius_is_userdata_type(args[0], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Vector3");
    }
    if (!mobius_is_userdata_type(args[1], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "Second argument must be a Vector3");
    }
    
    Vector3* vec1 = static_cast<Vector3*>(mobius_to_userdata(args[0]));
    Vector3* vec2 = static_cast<Vector3*>(mobius_to_userdata(args[1]));
    
    *result = mobius_create_float(state, vec1->dot(*vec2));
    
    return MOBIUS_OK;
}

int vector3_tostring(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    
    if (!mobius_is_userdata_type(args[0], "Vector3")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Vector3");
    }
    
    Vector3* vec = static_cast<Vector3*>(mobius_to_userdata(args[0]));
    *result = mobius_create_string(state, vec->toString().c_str());
    
    return MOBIUS_OK;
}

// ============================================================================
// PLAYER MOBIUS BINDINGS
// ============================================================================

int player_new(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    MOBIUS_CHECK_ARG_TYPE(0, mobius_is_string, "string");
    
    const char* name = mobius_to_string(args[0]);
    Player* player = new Player(std::string(name));
    *result = mobius_create_userdata(state, player, player_destructor, "Player", sizeof(Player));
    
    return MOBIUS_OK;
}

int player_get_health(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    
    if (!mobius_is_userdata_type(args[0], "Player")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Player");
    }
    
    Player* player = static_cast<Player*>(mobius_to_userdata(args[0]));
    *result = mobius_create_integer(state, player->health());
    
    return MOBIUS_OK;
}

int player_take_damage(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(2);
    
    if (!mobius_is_userdata_type(args[0], "Player")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Player");
    }
    if (!mobius_is_integer(args[1])) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "Second argument must be an integer");
    }
    
    Player* player = static_cast<Player*>(mobius_to_userdata(args[0]));
    int damage = (int)mobius_to_integer(args[1]);
    
    player->takeDamage(damage);
    *result = mobius_create_nil(state);
    
    return MOBIUS_OK;
}

int player_is_alive(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    
    if (!mobius_is_userdata_type(args[0], "Player")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Player");
    }
    
    Player* player = static_cast<Player*>(mobius_to_userdata(args[0]));
    *result = mobius_create_bool(state, player->isAlive());
    
    return MOBIUS_OK;
}

int player_tostring(MobiusState* state, MobiusValue** args, size_t arg_count, MobiusValue** result) {
    MOBIUS_CHECK_ARG_COUNT(1);
    
    if (!mobius_is_userdata_type(args[0], "Player")) {
        return mobius_set_error(state, MOBIUS_ERROR_TYPE, "First argument must be a Player");
    }
    
    Player* player = static_cast<Player*>(mobius_to_userdata(args[0]));
    *result = mobius_create_string(state, player->toString().c_str());
    
    return MOBIUS_OK;
}

// ============================================================================
// MAIN EXAMPLE
// ============================================================================

int main() {
    std::cout << "=== Mobius C++ Class Binding Example ===\n" << std::endl;
    
    // Create Mobius interpreter
    MobiusState* state = mobius_new_state();
    if (!state) {
        std::cerr << "Failed to create Mobius state" << std::endl;
        return 1;
    }
    
    // Initialize core systems
    if (mobius_init_core(state) != MOBIUS_OK) {
        std::cerr << "Failed to initialize Mobius core" << std::endl;
        mobius_free_state(state);
        return 1;
    }
    
    // Register Vector3 functions
    mobius_register_function(state, "Vector3", vector3_new, SIZE_MAX, "Create a new Vector3(x, y, z)");
    mobius_register_function(state, "vector3_length", vector3_length, 1, "Get vector length");
    mobius_register_function(state, "vector3_normalize", vector3_normalize, 1, "Normalize vector in-place");
    mobius_register_function(state, "vector3_add", vector3_add, 2, "Add two vectors");
    mobius_register_function(state, "vector3_dot", vector3_dot, 2, "Dot product of two vectors");
    mobius_register_function(state, "vector3_tostring", vector3_tostring, 1, "Convert vector to string");
    
    // Register Player functions
    mobius_register_function(state, "Player", player_new, 1, "Create a new Player(name)");
    mobius_register_function(state, "player_get_health", player_get_health, 1, "Get player health");
    mobius_register_function(state, "player_take_damage", player_take_damage, 2, "Player takes damage");
    mobius_register_function(state, "player_is_alive", player_is_alive, 1, "Check if player is alive");
    mobius_register_function(state, "player_tostring", player_tostring, 1, "Convert player to string");
    
    // Test script demonstrating C++ class usage
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
        print("v1 · v2 =", dot_product);
        
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
    
    // Execute the test script
    std::cout << "Executing Mobius script with C++ objects:\n" << std::endl;
    
    int result = mobius_exec_string(state, test_script);
    if (result != MOBIUS_OK) {
        MobiusError* error = mobius_get_last_error(state);
        if (error) {
            std::cerr << "Script execution failed: " << error->message << std::endl;
            mobius_free_error(error);
        }
    }
    
    std::cout << "\nCleaning up Mobius state..." << std::endl;
    
    // Clean up
    mobius_free_state(state);
    
    std::cout << "\nExample completed!" << std::endl;
    return 0;
}
