# SlimeProject

SlimeProject is a game development project consisting of two main components: SlimeOdyssey (a game engine library) and SlimeGame (a game using the SlimeOdyssey engine).

## Project Structure

```
SlimeProject/
├── SlimeOdyssey/  # Game engine library
├── SlimeGame/     # Game implementation
```

## Prerequisites

- CMake (version 3.20 or higher)
- C++20 compatible compiler
- Vulkan SDK

## Building the Project

1. Clone the repository:
   ```
   git clone https://github.com/yourusername/SlimeProject.git
   cd SlimeProject/SlimeGame
   ```

2. Create a build directory:
   ```
   mkdir build && cd build
   ```

3. Configure the project with CMake:
   ```
   cmake ..
   ```

4. Build the project:
   ```
   cmake --build .
   ```

## Running the Game

After building, you can find the SlimeGame executable in the `build/bin` directory.

## Development

- `SlimeOdyssey/`: Contains the game engine library code.
- `SlimeGame/`: Contains the game-specific code using the SlimeOdyssey engine.

To make changes, edit the files in these directories and rebuild the project.
