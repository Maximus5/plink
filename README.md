# plink fork

The aim of this fork is creating working console ssh client,
which does not depend on cygwin or msys.


## plink in [ConEmu](https://conemu.github.io)

![xterm256 mode](https://conemu.github.io/img/plink-256.png)


## Some links

* [Sources](https://github.com/Maximus5/plink)
* [Binaries](https://github.com/Maximus5/plink/releases)
* [Official page](http://www.chiark.greenend.org.uk/~sgtatham/putty/)


## Changes in this plink fork

* Plink survives on `Ctrl+C` and is transmitted the keypress to server instead.
* Keyboard fixes.
  * Arrows are working: `Up`/`Down` for history, `Left`/`Right` for moving in prompt.
  * `Esc` keypress transmitted to server (Vim and so on).
* ssh terminal size is properly initialized on startup (on-the-fly resize is not supported yet).
