# CS304 Computer Architecture - Project
Design and Implementation of a Bare-Metal Kernel with Bootloader and Direct VGA I/O

## Getting Started
In order to successfully install and use the OS, the following packages are required to be installed on your device.

1. Ubuntu/Debian
```
sudo apt install make nasm gcc binutils qemu-system-x86
```
2. Arch
```
sudo pacman -S make nasm gcc binutils qemu
```
3. Fedora
```
sudo dnf install make nasm gcc binutils qemu-system-x86
```
4. MacOS
```
brew install nasm
brew install x86_64-elf-binutils   # gives you x86_64-elf-ld, x86_64-elf-objcopy
brew install x86_64-elf-gcc        # gives you a real cross gcc targeting ELF
brew install qemu
```
To check if everything is installed run:
```
x86_64-elf-gcc --version
x86_64-elf-ld --version
```
`x86_64-elf-gcc --version` should say something like `"gcc 13.x.x"` and `x86_64-elf-ld --version` should mention `"ELF"`.

Alternatively if `brew install x86_64-elf-gcc` isn't found, try:
```
brew tap nativeos/i386-elf-toolchain
brew install i386-elf-binutils i386-elf-gcc
```
Then inside `Makefile` replace the following lines
```
CC      = x86_64-elf-gcc
LD      = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy
```
with
```
CC      = i386-elf-gcc
LD      = i386-elf-ld
OBJCOPY = i386-elf-objcopy
```

## Build Instructions
Build and run the OS via QEMU
```
make run
```
Alternative is to separately build `os.img` using
```
make
```
and then run it manually via terminal using QEMU
```
qemu-system-i386 -drive format=raw,file=build/os.img
```
> [!WARNING]
> In case `make` is not installed on your device, you'll first need to run the following command in order to be able to build the `os.img` file
> ```
> sudo apt install make     # Ubuntu/Debian
> sudo pacman -S make       # Arch
> sudo dnf install make     # Fedora
> xcode-select --install    # MacOS
> ```
Running QEMU with 128MB RAM and serial output in terminal
```
qemu-system-i386 -drive format=raw,file=build/os.img -m 128M -serial stdio
```
Running QEMU with no GUI window, terminal only
```
qemu-system-i386 -drive format=raw,file=build/os.img -nographic
```