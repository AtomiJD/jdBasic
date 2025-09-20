# Compiling jdBasic on Linux & macOS

This guide provides instructions for compiling the jdBasic interpreter from source on Linux and macOS systems using the provided `CMakeLists.txt` file.

## 1\. Prerequisites

Before you begin, you must install several development tools and libraries.

### Core Build Tools

You will need a C++20 compatible compiler, CMake, and Git.

* **On macOS:** Install the Xcode Command Line Tools and Homebrew:

    ```bash
    xcode-select --install
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    ```

    Then, install CMake:

    ```bash
    brew install cmake pkg-config
    ```

* **On Debian/Ubuntu Linux:**

    ```bash
    sudo apt-get update
    sudo apt-get install build-essential cmake pkg-config git
    ```

### Library Dependencies

The `CMakeLists.txt` file specifies several libraries that jdBasic depends on.

* **On macOS (using Homebrew):**

    ```bash
    # SDL3, ncurses, readline, and fmt
    brew install sdl3 sdl3_image sdl3_ttf ncurses readline fmt

    # OpenSSL is often keg-only, so install and link it
    brew install openssl@3
    export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig"
    ```

* **On Debian/Ubuntu Linux (using APT):**

    ```bash
    sudo apt-get install \
        libssl-dev \
        libsdl3-dev \
        libsdl3-image-dev \
        libsdl3-ttf-dev \
        libncurses-dev \
        libreadline-dev \
        libfmt-dev
    ```

## 2\. Compilation Steps

The project uses a standard out-of-source CMake build process. This keeps the build files separate from your source code.

**Step 1: Clone the Repository**
If you haven't already, clone the project source code to your local machine.

```bash
git clone https://github.com/AtomiJD/jdBasic.git
cd jdbasic
```

**Step 2: Create a Build Directory**
Create a directory to contain the build files and navigate into it.

```bash
mkdir build
cd build
```

**Step 3: Configure the Project with CMake**
Run `cmake` from the `build` directory. It will find the dependencies and generate the necessary Makefiles.

```bash
# This command points CMake to the parent directory where CMakeLists.txt is located
cmake ..
```

If you encounter errors about missing packages, double-check that all prerequisites were installed correctly.

**Step 4: Compile the Source Code**
Run `make` to start the compilation process. This will build the `jdBasic` executable.

```bash
make
```

To speed up compilation on multi-core systems, you can use the `-j` flag (e.g., `make -j4` for 4 cores).

## 3\. Running jdBasic

After a successful compilation, the `jdBasic` executable will be located in the `build` directory.

You can run it directly from the terminal:

```bash
./jdBasic
```

## 4\. Installation (Optional)

The `CMakeLists.txt` file includes an installation step. This is useful if you want to install jdBasic system-wide or into a specific prefix.

From the `build` directory, run:

```bash
sudo make install
```

By default, this will install the `jdBasic` executable into `/usr/local/bin`, making it available to run from any directory without specifying the full path.
