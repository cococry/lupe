# lupe
simple, lightweight, fast zoom for X 

**Lupe** is a simple tool that can be used with any X window manager¹ to provide 
smooth and responsive zooming/panning capabilities.

¹ As long as your system supports the widely available OpenGL compatibility profile.

## Building
The build dependencies are libGL, libX11 and libm.
Make sure you have those installed before building.

Building:
```console
gcc -o nob nob.c
./nob
./build/lupe
```

## Installing
Use the `install` option of the nob recipe. You can optionally specify the install
prefix as an argument to the `install` option.

Installing (to /usr/local/bin):
```console
sudo ./nob install
```
## CLI Usage

```console
lupe [options]
```

## Options

| Option | Description |
|---|---|
| `-nl`, `--no-lerp` | Disable smooth scrolling and panning |
| `-h`, `--help` | Show the help message |


## Keybinds

| Keybind | Description |
|---|---|
| `Space` | Toggle torch highlight mode |
| `Escape` | Quit and return to desktop |
| `Shift + Mousewheel` | Adjust torch highlight radius |
| `Mousewheel` | Adjust zoom |
| `Left Click + Drag` | Pan around |
