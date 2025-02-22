# DIYJVM

DIYJVM is a simple Java Virtual Machine (JVM) implementation written in C. It is designed for educational purposes, allowing users to understand the inner workings of a JVM by providing a straightforward and minimalistic codebase.

## Features

- **Class File Parsing**: Reads and interprets Java `.class` files, extracting essential information such as the magic number, version, constant pool entries, and methods.
- **Debugging Mode**: Offers a debugging option to output detailed logs during class file parsing, aiding in learning and troubleshooting.

## Requirements

- **Compiler**: GCC (GNU Compiler Collection)
- **Operating System**: Unix-like systems (Linux, macOS)

## Building the Project

### Using GCC

To compile the program with debugging support and all warnings enabled:

```sh
gcc -DDEBUG -Wall -Wextra -I./include src/main.c -o diyjvm
```

## Running the JVM

To execute the JVM with a Java class file:

```sh
./diyjvm path/to/YourClass.class
```

For example, to run the provided `HelloWorld` test class:

```sh
./diyjvm test/HelloWorld.class
```

Expected output:

```
Class file: test/HelloWorld.class
Magic: 0xCAFEBABE
Version: 65.0
Constant pool entries: 29
Methods: 2
```

## Debugging Mode

For more detailed logs during class file parsing, enable the debugging mode:

```sh
./diyjvm -d path/to/YourClass.class
```

This will provide step-by-step insights into how the class file is being processed.

## Project Structure

- `include/`: Header files
- `src/`: Source code
- `test/`: Test class files
- `CMakeLists.txt`: Build configuration for CMake

## Contributing

Contributions are welcome! Feel free to fork the repository, make improvements, and submit pull requests.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

---

*Note: This project is intended for educational purposes and may not support all features of a full-fledged JVM.* 
